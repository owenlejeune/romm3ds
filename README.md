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

Build outputs (placed in the `outputs/` folder):
- `outputs/romm3ds.3dsx` — Executable homebrew binary
- `outputs/romm3ds.smdh` — Application icon and metadata
- `outputs/romm3ds.elf` — ELF executable binary
- `outputs/romm3ds.cxi` — CTR executable image container
- `outputs/romm3ds.cia` — Installable CIA package for the 3DS HOME Menu

To build the CIA package (uses configuration and assets in `/cia`):

```bash
./scripts/build-cia.sh
# or if bannertool and makerom are installed in PATH:
make cia
```

To clean build artifacts:

```bash
make clean
```

---

### 3. Installation & Running on 3DS

Make sure your 3DS is running custom firmware (Luma3DS).

#### Method A: Install CIA to HOME Menu via FBI (Recommended)

1. Copy `outputs/romm3ds.cia` to `sdmc:/cias/` on your SD card (or use FBI network install).
2. Open **FBI** on your 3DS.
3. Navigate to **SD** -> **cias** -> `romm3ds.cia` and select **Install and delete CIA**.
4. Press **HOME** to return to the HOME Menu, unwrap the newly installed **RomM 3DS** gift, and launch it directly!

#### Method B: SD Card (Homebrew Launcher)

1. Insert your 3DS SD card into your computer (or connect via FTPD / microSD Management).
2. Copy `outputs/romm3ds.3dsx` to `sdmc:/3ds/romm3ds/`:
   ```text
   SD Card
   └── 3ds/
       └── romm3ds/
           └── romm3ds.3dsx
   ```
3. Insert the SD card back into the 3DS and launch **romm3ds** from the **Homebrew Launcher**.

#### Method C: Wireless NetLoader (`3dslink`)

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

---

## Important Information

### Nintendo DS (NDS) Saves & RomM Setup

When syncing Nintendo DS saves (such as games played on 3DS via TWiLight Menu++ or `nds-bootstrap`), `romm3ds` transfers raw `.sav` files directly to RomM. Keep the following in mind depending on the emulator used:

#### 1. RomM In-Browser Player (EmulatorJS with melonDS)

RomM's in-browser player uses **melonDS** by default for Nintendo DS, which natively reads raw `.sav` files. However, certain titles (such as *Pokémon HeartGold/SoulSilver* and *Diamond/Pearl/Platinum*) perform hardware wireless and firmware checks on save load:

- **Why a BIOS bundle is needed:** RomM passes only a single firmware file to EmulatorJS. If BIOS files are uploaded to RomM individually, melonDS only receives one file and falls back to an internal HLE BIOS for the others. This causes titles like *HeartGold* to crash with *"A communication error has occurred"* right after pressing Start.
- **How to setup RomM:** Bundle all three Nintendo DS system files together into a single `.zip` file at the root:
  ```text
  nds.zip
  ├── bios7.bin
  ├── bios9.bin
  └── firmware.bin
  ```
  Place `nds.zip` in your RomM server's BIOS directory:
  ```text
  /romm/library/bios/nds/nds.zip
  ```
  *(Remove any loose `.bin` files in that folder so RomM only registers the zip).*
  Trigger a **Scan** in RomM to index `nds.zip`, and in the RomM player setup select **`nds.zip`** under **Firmware** with core set to **`melonds`**.

#### 2. Emulators That Do Not Support Raw `.sav` (e.g. DeSmuME)

Some emulators and libretro cores (such as **DeSmuME**) do not recognize raw `.sav` files natively and require their proprietary `.dsv` (DeSmuME Save) format:

- **Symptom:** If you load a raw `.sav` into DeSmuME without conversion, DeSmuME will fail to detect the save structure and start a **"New Game"** instead of showing "Continue".
- **Converting `.sav` to `.dsv`:**
  - **In standalone DeSmuME:** Launch the game, go to **File -> Import Backup Memory...**, select your `.sav` file, and save once in-game to write out a native `.dsv`.
  - **Using PKHeX:** Open your `.sav` file in [PKHeX](https://github.com/kwsch/PKHeX) and export it via **File -> Export SAV... -> Export .dsv**.
  - **Using `dsv2sav` / `dsv-tool`:** Use automated conversion scripts to append the 40-byte DeSmuME footer (`|-DESMUME SAVE-|`) to the 512 KB `.sav` data.
