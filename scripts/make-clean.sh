#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

docker run --rm -v "${PROJECT_ROOT}:/project" -w /project devkitpro/devkitarm:latest make clean
