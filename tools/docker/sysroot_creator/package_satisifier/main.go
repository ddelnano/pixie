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

package main

import (
	"flag"
	"fmt"
	"os"
	"strings"

	log "github.com/sirupsen/logrus"
)

// stringSliceFlag is a flag.Value implementation for string slices.
type stringSliceFlag []string

func (s *stringSliceFlag) String() string {
	return strings.Join(*s, ",")
}

func (s *stringSliceFlag) Set(value string) error {
	*s = append(*s, value)
	return nil
}

var (
	packageDatabaseFile string
	specs               stringSliceFlag
)

func init() {
	flag.StringVar(&packageDatabaseFile, "pkgdb", "", "Debian distribution extracted Packages.xz file.")
	flag.Var(&specs, "specs", "Yaml file specifying packages to include/exclude in the sysroot (can be specified multiple times)")
}

func main() {
	flag.Parse()
	log.SetOutput(os.Stderr)

	if packageDatabaseFile == "" {
		log.Fatal("must specify pkgdb")
	}
	if len(specs) == 0 {
		log.Fatal("must specify at least one spec")
	}

	db, err := parsePackageDatabase(packageDatabaseFile)
	if err != nil {
		log.WithError(err).Fatal("failed to parse package database")
	}

	combinedSpec, err := parseAndCombineSpecs(specs)
	if err != nil {
		log.WithError(err).Fatal("failed to parse specs")
	}
	filteredSpec := combinedSpec.removeIncludesFromExclude()

	dependencySatisfier := newDepSatisfier(db, filteredSpec)

	debs, err := dependencySatisfier.listRequiredDebs()
	if err != nil {
		log.WithError(err).Fatal("failed to find all required packages")
	}
	for _, filename := range debs {
		fmt.Println(filename)
	}
}
