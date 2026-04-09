#!/bin/bash
set -euo pipefail

FIFO=/tmp/ws90.fifo

if [ ! -p "$FIFO" ]; then
    echo "[feed] error: FIFO $FIFO does not exist"
    exit 1
fi

exec /usr/bin/rtl_433 \
    -d serial=WS90 \
    -f 433920000 \
    -Y classic \
    -s 250k \
    -g 30 \
    -M time:iso \
    -F json:"$FIFO"
