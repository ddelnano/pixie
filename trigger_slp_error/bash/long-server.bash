#!/bin/bash
set -u -e
set -o pipefail

SLEEP="10s"

/src/probes-test -addr "localhost:8080" -sid "$i" -once=false >> server.out 2>> server.err &
for i in $(seq 0 29)
do
  curl \
    -d '{"request_id": '"$i"', "sleep": "'"${SLEEP}"'"}' \
    -H "Content-Type: application/json" \
    -X POST \
    --retry 1000 \
    --retry-all-errors localhost:8080/ \
    2>> client.err | tee -a /client.out &
  wait $!
  sleep 0.1
done
