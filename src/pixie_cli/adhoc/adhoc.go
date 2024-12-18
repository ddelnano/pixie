package main

import (
	"flag"
	"fmt"

	"github.com/gofrs/uuid"
	"px.dev/pixie/src/pixie_cli/pkg/cmd"
)

func main() {
	err := cmd.RunSimpleHealthCheckScript("getcosmic.ai:443", uuid.FromStringOrNil("1c0a2c4f-17d3-4df1-bd31-7c9c80a75019"))
	cloudAddr := flag.String("cloud_addr", "", "The address of the cloud endpoint")
	clusterID := flag.String("cluster_id", "", "The ID of the cluster")

	// Parse the flags
	flag.Parse()

	// Validate the flags
	if *cloudAddr == "" {
		fmt.Println("Error: --cloud_addr is required")
		flag.Usage()
		return
	}

	if *clusterID == "" {
		fmt.Println("Error: --cluster_id is required")
		flag.Usage()
		return
	}

	err = cmd.RunSimpleHealthCheckScript(*cloudAddr, uuid.FromStringOrNil(*clusterID))

	if err != nil {
		fmt.Println(err)
	}
}
