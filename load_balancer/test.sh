#!/bin/bash
trap "echo 'Stopping...'; kill 0" SIGINT

send_heartbeat() {
  PORT=$1
  NAME=$2
  while true; do
    # Randomly decide to skip sending (simulate missed heartbeat)
    if (( RANDOM % 5 == 0 )); then
      echo "🔇 $NAME is skipping a heartbeat"
      sleep 1
    else
      echo -n "0.0.0.0:7000" | nc -u -w0 127.0.0.1 5001 -p "$PORT"
      echo "💗 $NAME sent heartbeat from port $PORT"
      sleep 0.5
    fi
  done
}

# Run each backend in the background
send_heartbeat 6000 "Backend A" &
send_heartbeat 6001 "Backend B" &
send_heartbeat 6002 "Backend C" &

# Wait forever
wait
