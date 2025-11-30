/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package docker

import (
	"bytes"
	"fmt"
	"time"

	"github.com/olivere/elastic/v7"
	"github.com/ory/dockertest/v3"
	"github.com/ory/dockertest/v3/docker"
	log "github.com/sirupsen/logrus"
)

func connectElastic(esURL string, esUser string, esPass string) (*elastic.Client, error) {
	es, err := elastic.NewClient(elastic.SetURL(esURL),
		elastic.SetBasicAuth(esUser, esPass),
		elastic.SetSniff(false))
	if err != nil {
		return nil, err
	}
	return es, nil
}

// SetupElastic starts up an embedded elastic server on some free ports.
func SetupElastic() (*elastic.Client, func(), error) {
	cleanup := func() {}
	pool, err := dockertest.NewPool("")
	if err != nil {
		return nil, cleanup, fmt.Errorf("Could not connect to docker: %s", err)
	}

	esPass := "password"
	resource, err := pool.RunWithOptions(&dockertest.RunOptions{
		Repository: "elasticsearch",
		Tag:        "7.6.0",
		Env: []string{
			"discovery.type=single-node",
			fmt.Sprintf("ELASTIC_PASSWORD=%s", esPass),
			"xpack.security.http.ssl.enabled=false",
			"xpack.security.transport.ssl.enabled=false",
			"indices.lifecycle.poll_interval=5s",
			"ES_JAVA_OPTS=-Xms128m -Xmx128m -server",
			"ES_HEAP_SIZE=128m",
		},
	}, func(config *docker.HostConfig) {
		config.AutoRemove = false
		config.RestartPolicy = docker.RestartPolicy{Name: "no"}
		config.CPUCount = 1
		config.Memory = 1024 * 1024 * 1024
		config.MemorySwap = 0
		config.MemorySwappiness = 0
	})
	if err != nil {
		return nil, cleanup, err
	}
	// Set a 5 minute expiration on resources.
	err = resource.Expire(300)
	if err != nil {
		return nil, cleanup, err
	}

	// Increase retry timeout (default is 1 minute)
	pool.MaxWait = 1 * time.Minute
	clientPort := resource.GetPort("9200/tcp")
	var esHost string

	// Log network debugging info
	log.Infof("Container ID: %s", resource.Container.ID)
	log.Infof("Container IPAddress: %s", resource.Container.NetworkSettings.IPAddress)
	log.Infof("Container Gateway: %s", resource.Container.NetworkSettings.Gateway)
	log.Infof("Mapped port 9200/tcp: %s", resource.GetPort("9200/tcp"))
	for netName, netSettings := range resource.Container.NetworkSettings.Networks {
		esHost = netSettings.Gateway
		log.Infof("Setting ES host to gateway %s for network %s", esHost, netName)
		break
	}

	esURL := fmt.Sprintf("http://%s:%s", esHost, clientPort)
	log.Infof("Will attempt to connect to Elasticsearch at: %s", esURL)

	var client *elastic.Client
	err = pool.Retry(func() error {
		var err error
		client, err = connectElastic(esURL, "elastic", esPass)
		if err != nil {
			log.WithError(err).Errorf("Failed to connect to elasticsearch at %s", esURL)
		}
		return err
	})
	if err != nil {
		// Dump container logs on failure for debugging
		var stdout, stderr bytes.Buffer
		logsErr := pool.Client.Logs(docker.LogsOptions{
			Container:    resource.Container.ID,
			OutputStream: &stdout,
			ErrorStream:  &stderr,
			Stdout:       true,
			Stderr:       true,
			Tail:         "100",
		})
		if logsErr != nil {
			log.WithError(logsErr).Error("Failed to get container logs")
		} else {
			log.Errorf("Elasticsearch container stdout:\n%s", stdout.String())
			log.Errorf("Elasticsearch container stderr:\n%s", stderr.String())
		}

		purgeErr := pool.Purge(resource)
		if purgeErr != nil {
			log.WithError(err).Error("Failed to purge pool")
		}
		return nil, cleanup, fmt.Errorf("Cannot start elasticsearch: %s", err)
	}

	log.Info("Successfully connected to elastic.")

	cleanup = func() {
		client.Stop()
		err = pool.Purge(resource)
		if err != nil {
			log.WithError(err).Error("Failed to purge pool")
		}
	}

	return client, cleanup, nil
}
