#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <3ds-ip-address>"
    echo "Example: $0 192.168.1.50"
    exit 1
fi

IP="$1"
shift

BINARY="outputs/romm3ds.3dsx"
if [ ! -f "${PROJECT_ROOT}/${BINARY}" ]; then
    echo "${BINARY} not found. Building first..."
    "${SCRIPT_DIR}/make.sh"
fi

echo "Sending ${BINARY} to 3DS at ${IP} (target: sdmc:/3ds/romm3ds/romm3ds.3dsx)..."
docker run --rm --net=host -v "${PROJECT_ROOT}:/project" -w /project devkitpro/devkitarm:latest 3dslink -a "${IP}" -0 "sdmc:/3ds/romm3ds/romm3ds.3dsx" "${BINARY}" "$@"
