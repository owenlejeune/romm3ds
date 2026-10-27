# romm3ds

RomM save synchronization client for Nintendo 3DS.

## Build & Installation

### 1. Prerequisites

You will need the `devkitARM` toolchain and 3DS development portlibs. You can build natively or using Docker.

#### Option A: Docker (Recommended)

No local toolchain installation required:

```bash
docker run --rm -v "$(pwd):/project" -w /project devkitpro/devkitarm:latest \
  bash -c "dkp-pacman -S --noconfirm 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib && make"
```

#### Option B: Native Installation via devkitPro

1. Install devkitPro via the [devkitPro Getting Started guide](https://devkitpro.org/wiki/Getting_Started).
2. Install the 3DS packages:
   ```bash
   sudo dkp-pacman -S 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib
   ```
3. Set your environment variables (e.g. in `~/.zshrc` or `~/.bashrc`):
   ```bash
   export DEVKITPRO=/opt/devkitpro
   export DEVKITARM=$DEVKITPRO/devkitARM
   export PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH
   ```

---

### 2. Building

Run `make` from the repository root:

```bash
make
```

Build outputs:
- `romm3ds.3dsx` — Executable homebrew binary
- `romm3ds.smdh` — Application icon and metadata

To clean build artifacts:

```bash
make clean
```

---

### 3. Installation & Running on 3DS

Make sure your 3DS is running custom firmware (Luma3DS).

#### Method A: SD Card

1. Insert your 3DS SD card into your computer (or connect via FTPD / microSD Management).
2. Copy `romm3ds.3dsx` to `sdmc:/3ds/romm3ds/`:
   ```text
   SD Card
   └── 3ds/
       └── romm3ds/
           └── romm3ds.3dsx
   ```
3. Insert the SD card back into the 3DS and launch **romm3ds** from the **Homebrew Launcher**.

#### Method B: Wireless NetLoader (`3dslink`)

1. Connect your 3DS and computer to the same Wi-Fi network.
2. Open the **Homebrew Launcher** on your 3DS and press **(Y)** to launch NetLoader.
3. Run the following command from your terminal:
   ```bash
   ./scripts/3dslink-install.sh <3DS_IP_ADDRESS>
   ```
   Or using 3dslink directly:
   ```bash
   3dslink -a <3DS_IP_ADDRESS> -0 "sdmc:/3ds/romm3ds/romm3ds.3dsx" romm3ds.3dsx
   ```
