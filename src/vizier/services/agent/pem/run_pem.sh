#!/bin/sh

# Counter for memory threshold events
memory_threshold_count=0

# Record startup time
startup_time=$(date +%s)

# Get hostname
hostname=$(hostname)

# Function to get time since startup in seconds
get_uptime() {
    current_time=$(date +%s)
    uptime=$((current_time - startup_time))
    echo "$uptime"
}

# Function to format uptime as HH:MM:SS
format_uptime() {
    seconds=$1
    hours=$((seconds / 3600))
    minutes=$(((seconds % 3600) / 60))
    secs=$((seconds % 60))
    printf "%02d:%02d:%02d" $hours $minutes $secs
}

# Function to run PEM and check its exit code
run_pem() {
    while true; do
        /pem "$@"
        exit_code=$?

        if [ "$exit_code" -eq 100 ] || [ "$exit_code" -eq 134 ]; then
            memory_threshold_count=$((memory_threshold_count + 1))
            uptime_seconds=$(get_uptime)
            uptime_formatted=$(format_uptime $uptime_seconds)
            echo "$(date '+%Y-%m-%d %H:%M:%S') - [$hostname] Memory threshold reached (count: $memory_threshold_count, uptime: $uptime_formatted) with code: $exit_code" >&2
            sleep 1
        else
            uptime_seconds=$(get_uptime)
            uptime_formatted=$(format_uptime $uptime_seconds)
            echo "[$hostname] PEM exited with code $exit_code (uptime: $uptime_formatted), not restarting." >&2
            exit "$exit_code"
        fi
    done
}

trap "echo '[$hostname] Received termination signal, exiting.'; exit" SIGTERM SIGINT

run_pem "$@"
