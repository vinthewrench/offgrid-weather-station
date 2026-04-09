#!/bin/bash
set -euo pipefail

BASE="$(cd "$(dirname "$0")" && pwd)"
exec "$BASE/bin/ws90_api" --id 52127
