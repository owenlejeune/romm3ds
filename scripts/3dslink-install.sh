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

if [ ! -f "${PROJECT_ROOT}/romm3ds.3dsx" ]; then
    echo "romm3ds.3dsx not found. Building first..."
    "${SCRIPT_DIR}/make.sh"
fi

echo "Sending romm3ds.3dsx to 3DS at ${IP}..."
docker run --rm --net=host -v "${PROJECT_ROOT}:/project" -w /project devkitpro/devkitarm:latest 3dslink -a "${IP}" romm3ds.3dsx "$@"
