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
- `outputs/romm3ds.3dsx` — Core executable homebrew binary (placed on SD card)
- `outputs/romm3ds.smdh` — Application icon and metadata
- `outputs/romm3ds.elf` — ELF executable binary
- `outputs/romm3ds.cia` — Optional HOME Menu forwarder CIA (launches `romm3ds.3dsx` from the HOME Menu)

To build the optional HOME Menu forwarder CIA (uses assets in `/cia`):

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

#### Step 1: Install the .3dsx Executable (Required)

`romm3ds.3dsx` is the application executable and must be located on your SD card.

##### Option A: SD Card Transfer
1. Copy `outputs/romm3ds.3dsx` to `sdmc:/3ds/romm3ds/`:
   ```text
   SD Card
   └── 3ds/
       └── romm3ds/
           └── romm3ds.3dsx
   ```
2. You can now launch **romm3ds** directly from the **Homebrew Launcher**.

##### Option B: Wireless NetLoader (`3dslink`)
1. Connect your 3DS and computer to the same Wi-Fi network.
2. Open the **Homebrew Launcher** on your 3DS and press **(Y)** to launch NetLoader.
3. Run the deployment script from your terminal:
   ```bash
   ./scripts/3dslink-install.sh <3DS_IP_ADDRESS>
   ```
   Or using 3dslink directly:
   ```bash
   3dslink -a <3DS_IP_ADDRESS> -0 "sdmc:/3ds/romm3ds/romm3ds.3dsx" romm3ds.3dsx
   ```

#### Step 2: Install HOME Menu Forwarder .cia (Optional)

If you prefer launching `romm3ds` directly from your 3DS HOME Menu, install the forwarder CIA:

> [!NOTE]
> The `.cia` is a lightweight **forwarder**, not a standalone bundle. It loads and runs `sdmc:/3ds/romm3ds/romm3ds.3dsx` via Luma3DS. You only need to install the `.cia` once—whenever you update `romm3ds` in the future, simply update `romm3ds.3dsx` without having to reinstall the `.cia`.

1. Ensure `romm3ds.3dsx` is present on your SD card at `sdmc:/3ds/romm3ds/romm3ds.3dsx` (Step 1).
2. Copy `outputs/romm3ds.cia` to `sdmc:/cias/` on your SD card (or use FBI network install).
3. Open **FBI** on your 3DS.
4. Navigate to **SD** -> **cias** -> `romm3ds.cia` and select **Install and delete CIA**.
5. Press **HOME** to return to the HOME Menu, unwrap the newly installed **RomM 3DS** gift, and launch it!

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

#### 3. Web Player `.srm` Saves & Syncing Back to 3DS

When saving in the RomM in-browser player, EmulatorJS/RetroArch saves to a **`.srm`** (Save RAM) file instead of `.sav`. Both files may appear in RomM:

- **Are `.srm` and `.sav` compatible?** Yes. For Nintendo DS (melonDS/libretro), `.srm` and `.sav` files are **format-identical** raw cartridge EEPROM/Flash memory dumps. Only the file extension differs.
- **How `romm3ds` handles this:**
  - When syncing, `romm3ds` compares timestamps across all remote saves (`.sav`, `.srm`, etc.) and downloads the **most recent** save file.
  - It writes the downloaded save data directly into your 3DS save location as a standard **`.sav`** (e.g. `sdmc:/roms/nds/saves/<game>.sav`).
  - When you launch the game on your 3DS through TWiLight Menu++ or an `nds-bootstrap` forwarder, your progress from the web browser loads seamlessly.

#### 4. TWiLight Menu++ (`nds-bootstrap`) Save Timestamps & Syncing Back to RomM

When you play Nintendo DS games on your 3DS and save in-game, you may notice that the local save does not appear newer or that its timestamp does not reflect your latest save session:

- **Why timestamps do not update in-game:** `nds-bootstrap` uses direct sector I/O to write save data directly to the SD card clusters to prevent lag and audio stuttering during gameplay. Because it bypasses the FAT32 filesystem directory table, **the file's "Date Modified" timestamp is not updated when saving in-game**. The timestamp remains set to when the file was first created or written by an application (e.g. when downloaded by `romm3ds`).
- **UTC vs. Local Timezone:** RomM reports save timestamps in UTC (`Z`), whereas the 3DS system clock and FAT32 SD card filesystem store times in local wall-clock time.
- **How to sync progress back to RomM:** When you have made progress in a DS game on your 3DS and open `romm3ds` to sync:
  1. Trigger the sync as usual.
  2. In the **Save Conflict Resolution** screen, select **`[ Overwrite Remote (Use Local Save) ]`**.
  3. `romm3ds` will read the raw save from your SD card (which contains your newest in-game progress) and upload it to your RomM server.

