#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

# Add ~/.local/bin to PATH if not already present
export PATH="${HOME}/.local/bin:${PATH}"

OUTPUT_DIR="outputs"
CIA_DIR="cia"
TITLE_ID="0004000000F17E00"

mkdir -p "${OUTPUT_DIR}"

echo "==> Ensuring romm3ds binary is built..."
"${SCRIPT_DIR}/make.sh"

echo "==> Building forwarder ELF (${OUTPUT_DIR}/forwarder.elf)..."
docker run --rm -v "${PROJECT_ROOT}:/project" -w /project devkitpro/devkitarm:latest \
  /opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc \
  -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
  -O2 -Wall -Wextra -ffunction-sections -fdata-sections -D__3DS__ \
  -I/opt/devkitpro/libctru/include -specs=3dsx.specs -Wl,--gc-sections \
  -L/opt/devkitpro/libctru/lib cia/forwarder/main.c -lctru \
  -o "${OUTPUT_DIR}/forwarder.elf"

if ! command -v bannertool >/dev/null 2>&1; then
  echo "Error: bannertool not found in PATH or ~/.local/bin" >&2
  exit 1
fi

if ! command -v makerom >/dev/null 2>&1; then
  echo "Error: makerom not found in PATH or ~/.local/bin" >&2
  exit 1
fi

echo "==> Generating banner..."
bannertool makebanner -i "${CIA_DIR}/banner.png" -a "${CIA_DIR}/audio.wav" -o "${CIA_DIR}/banner.bnr"

echo "==> Building CXI (${OUTPUT_DIR}/romm3ds.cxi)..."
makerom -f cxi -o "${OUTPUT_DIR}/romm3ds.cxi" \
  -target t \
  -desc app:7 \
  -rsf "${CIA_DIR}/app.rsf" \
  -elf "${OUTPUT_DIR}/forwarder.elf" \
  -icon "${OUTPUT_DIR}/romm3ds.smdh" \
  -banner "${CIA_DIR}/banner.bnr"

echo "==> Packaging Forwarder CIA (${OUTPUT_DIR}/romm3ds.cia)..."
makerom -f cia -o "${OUTPUT_DIR}/romm3ds.cia" \
  -content "${OUTPUT_DIR}/romm3ds.cxi:0:${TITLE_ID}"

echo "==> Successfully created forwarder ${OUTPUT_DIR}/romm3ds.cia ($(du -h "${OUTPUT_DIR}/romm3ds.cia" | cut -f1 | xargs))"
