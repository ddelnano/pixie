package main

import (
	"fmt"

	"github.com/gofrs/uuid"
	"px.dev/pixie/src/pixie_cli/pkg/cmd"
)

func main() {
	err := cmd.RunSimpleHealthCheckScript("getcosmic.ai:443", uuid.FromStringOrNil("1c0a2c4f-17d3-4df1-bd31-7c9c80a75019"))
	if err != nil {
		fmt.Println(err)
	}
}
