#!/bin/sh
set -e

FIFO=/tmp/ws90.fifo

echo "[entrypoint] starting, fifo=$FIFO"


# ----------------------------------------------------------------------
# WS90 TRANSMIT FREQUENCY
#
# The Ecowitt WS90 sensor transmits on different ISM bands depending
# on the region where it was sold:
#
#   United States:       915000000 Hz
#   Europe:              868000000 Hz
#   Some other regions:  433920000 Hz
#
# This container defaults to the US frequency of 915 MHz.
# Users can override by setting the FREQ environment variable in
# docker-compose.yml, for example:
#
#     environment:
#       - FREQ=433920000
#
# ----------------------------------------------------------------------
FREQ="${FREQ:-915000000}"

echo "[entrypoint] using WS90 frequency: $FREQ"

if [ -e "$FIFO" ] && [ ! -p "$FIFO" ]; then
    echo "[entrypoint] $FIFO exists but is not a fifo, removing"
    rm -f "$FIFO"
fi

if [ ! -p "$FIFO" ]; then
    echo "[entrypoint] creating fifo $FIFO"
    mkfifo "$FIFO"
fi

echo "[entrypoint] launching rtl_433"
/usr/local/bin/rtl_433 \
    -d serial=WS90 \
    -f "$FREQ" \
    -M time:iso \
    -F json:"$FIFO" &
RTL_PID=$!

cleanup() {
    echo "[entrypoint] cleanup: killing rtl_433 pid=$RTL_PID"
    kill "$RTL_PID" 2>/dev/null || true
    wait "$RTL_PID" 2>/dev/null || true
}

trap cleanup INT TERM

echo "[entrypoint] starting ws90_api"
/usr/local/bin/ws90_api --id 52127

echo "[entrypoint] ws90_api exited, cleaning up"
cleanup
exit 0
