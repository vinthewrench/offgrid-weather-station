WS90 local runtime

Overview:
Local, non-Docker pipeline for Ecowitt WS90.
rtl_433 runs on the host and feeds JSON into a FIFO.
ws90_api reads the FIFO and serves HTTP on port 7890.

Directory layout:

- run_ws90_api.sh start API server
- run_ws90_feed.sh start rtl_433 feed (writes to FIFO)
- bin/ws90_api compiled API binary
- src/ source code
- build/ build artifacts
- Makefile build rules
- systemd/ service unit files
- install-systemd.sh installs/updates systemd services

Data flow:
rtl_433 (host)
-> /tmp/ws90.fifo
-> ws90_api
-> http://localhost:7890/

Manual run:

1. ./run_ws90_api.sh
2. ./run_ws90_feed.sh

Test:
curl http://127.0.0.1:7890/

Notes:

- API must start before feed (it owns the FIFO)
- Do NOT delete/recreate /tmp/ws90.fifo while running
- rtl_433 uses device serial=WS90 at 433.92 MHz

Systemd:
Install and enable services:
./install-systemd.sh

Check status:
systemctl status ws90-api.service ws90-feed.service

Logs:
journalctl -u ws90-api.service
journalctl -u ws90-feed.service
