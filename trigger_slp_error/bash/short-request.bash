#!/bin/bash
set -e -u
set -o pipefail

for i in $(seq 0 999)
do
  /src/tiny-sleep-server -addr localhost:8080 -sid "$i" -once=true >> server.out 2>> server.err &
  curl \
    -d '{"request_id": '"$i"', "sleep": "0ns"}' \
    -H "Content-Type: application/json" \
    -X POST \
    --retry 1000 \
    --retry-all-errors localhost:8080/ \
    2>> client.err | tee -a /client.out  &
  wait
  sleep 0.1
done
