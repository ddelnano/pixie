#!/bin/bash
set -u -e
set -o pipefail

for i in $(seq 0 14)
do
  /src/probes-test -addr "localhost:8080" -sid "$i" -once=true >> server.out 2>> server.err &
  curl \
    -d '{"request_id": '"$i"', "sleep": "10s"}' \
    -H "Content-Type: application/json" \
    -X POST \
    --retry 1000 \
    --retry-all-errors localhost:8080/ \
    2>> client.err | tee -a /client.out &
  wait
  sleep 0.1
done
