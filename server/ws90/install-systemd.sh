#!/bin/bash
set -euo pipefail

BASE="$(cd "$(dirname "$0")" && pwd)"

echo "[install] copying unit files"
sudo cp "$BASE/systemd/ws90-api.service" /etc/systemd/system/
sudo cp "$BASE/systemd/ws90-feed.service" /etc/systemd/system/

echo "[install] reloading systemd"
sudo systemctl daemon-reload

echo "[install] enabling services"
sudo systemctl enable ws90-api.service ws90-feed.service

echo "[install] restarting services"
sudo systemctl restart ws90-api.service
sudo systemctl restart ws90-feed.service

echo "[install] done"
systemctl status ws90-api.service ws90-feed.service --no-pager || true
