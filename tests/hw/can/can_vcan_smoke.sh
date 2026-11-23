#!/usr/bin/env bash
# Optional real vcan smoke. The probe is test-only and uses Linux SocketCAN.
set -euo pipefail
probe="$1"
if [[ ! -e /sys/class/net/vcan0 ]]; then
    echo "vcan smoke: vcan0 is not available"
    exit 77
fi
"$probe" vcan0
