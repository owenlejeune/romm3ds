# romm3ds

**romm3ds** is a bidirectional save synchronization client for Nintendo 3DS that connects directly to your self-hosted [RomM](https://github.com/rommapp/romm) (Rom Manager) instance.

It automatically detects installed Nintendo 3DS and CIA games (SD/Cartridge/NAND), along with standalone ROMs from any ROM directories and subdirectories on your SD card, compares save timestamps with your RomM server, and syncs progress seamlessly.

---

## Features

- **Installed Title & ROM Directory Scanning**: Detects all installed 3DS and CIA games (SD, Game Card, and NAND), along with ROMs and saves in any ROM directories and subdirectories on your SD card (such as `/roms`, `/roms/gba`, `/files/roms`, `/mGBA/`, and user-configured paths).
- **Smart Save Priority**: Checks the ROM's immediate folder and local emulator subdirectories first before falling back to global searches.
- **Top Screen Game Details & Logos**: Displays game metadata and icons/logos:
  - **3DS**: Embedded SMDH banners and icons.
  - **Nintendo DS**: Embedded 4bpp banner icons extracted directly from `.nds` ROM headers.
  - **Game Boy Advance**: Cover art (`/mGBA/covers/`, `/_nds/TWiLightMenu/boxart/`, adjacent `.png` files), forwarder SMDH icons, or retro cartridge badges.
- **Bidirectional Syncing**: Upload local saves to RomM or download newer remote saves.
- **Custom Save Location Overrides**: Set a custom save file path for any title (ideal for custom 3DSX forwarders and multi-path emulators).
- **Manual RomM Matching**: Manually search and link local games to specific RomM library entries if auto-matching needs adjustment.
- **Conflict Resolution Dialog**: Visual side-by-side comparison of local vs. remote timestamps with user-selected resolution strategies.
- **Filter & Sort Pickers**: Filter your local library by status (`All`, `Matched`, `Has Saves`, `Outdated (Remote Newer)`, `Updated (Local Newer)`, `Hidden`), by console type (`3DS`, `DS`, `GBA`, etc.), or by text search, and sort by `Name`, `Has Save`, or `Matched`.
- **Filtered Batch Sync**: Synchronize all games in your currently active filter/view with a single tap or button press.
- **Real-Time Operation Log**: Live onscreen log window displaying sync progress and network requests with soft word-wrapping.

---

## Basic Usage

### 1. Initial Setup & Configuration

1. Launch **romm3ds** on your 3DS (via the Homebrew Launcher or the installed HOME Menu forwarder).
2. On first launch, you will be prompted to configure your RomM connection:
   - **RomM Server Endpoint**: Your RomM server address (e.g., `http://192.168.1.100:8080`). If using a reverse proxy with a subpath, include it (e.g., `https://romm.example.com/romm`).
   - **API Key / Credentials**: Your RomM API Key / Personal Access Token (generated in the RomM web dashboard under your user profile / API tokens).
3. Tap **`[ Save Configuration ]`** to test the connection and save your settings to `sdmc:/3ds/romm3ds/config.json`.
4. *(Optional)* To scan additional custom ROM directories on your SD card, add a `"rom_directories"` array in `sdmc:/3ds/romm3ds/config.json` (e.g. `["sdmc:/roms", "sdmc:/custom/roms"]`).

---

### 2. Navigating Your Library

- **Top Screen**: Shows detailed information for the currently selected game:
  - Game logo / boxart banner on the left.
  - Game Title, Platform badge (e.g. `[3DS]`, `[GBA]`, `[DS]`), and RomM Match status.
  - Title ID or full SD card ROM path and active local save file path.
  - Save timestamps and relative age indicators (`*REMOTE IS NEWER*`, `*LOCAL IS NEWER*`, or `*IN SYNC*`).
- **Bottom Screen**: Scrollable list of detected local games and ROMs, filter/sort controls, and action buttons:
  - **Sort Button (`[Sort: ...]`)**: Opens the Sort Picker (`Name`, `Has Save`, `Matched`).
  - **Filter Button (`[Filter: ...]`)**: Opens the Filter Picker (`All (Unhidden)`, `Has Saves`, `Matched`, `Outdated (Remote Newer)`, `Updated (Local Newer)`, available console filters, and `Hidden / Ignored`).
  - **Search Bar & `[ Clr ]`**: Filter visible games by title substring.
  - **`[ Sync (A) ]`**: Initiates synchronization for the highlighted game.
  - **`[ Sync All (X) ]`**: Queues and batch-synchronizes all matched games in the **currently filtered list**.
  - **`[ Match ]`**: Manually link or change the linked RomM library entry (or refresh the remote console library).
  - **`[ Save Path ]`**: Browse the SD card to select or override the local save file path for the highlighted title.
  - **`[ Hide ]` / `[ Unhide ]`**: Exclude or re-include the highlighted title from the main list and batch sync.

---

### 3. Syncing Saves & Conflict Resolution

#### Single Game & Batch Sync
Highlight a game and press **(A)** or tap **`[ Sync (A) ]`** (or press **(X)** / tap **`[ Sync All (X) ]`** to batch sync all games in the current filter):
- If a save exists only on the server, `romm3ds` downloads and injects it locally.
- If a save exists only locally, `romm3ds` uploads it to your RomM server.
- You can also force an immediate upload with **(L)** or force an immediate download with **(R)** from the main list.

#### Conflict Resolution
When a save exists both locally and remotely:
1. A dialog displays the **Local Save Date** and **Remote Save Date** side by side and highlights which save is newer.
2. Select your choice:
   - **`[ Download Save (Overwrite Local) ]`**: Downloads the server save to your 3DS.
   - **`[ Upload Save (Use Local Save) ]`**: Uploads your 3DS save to the server.
   - **`[ Skip ]`**: Skips synchronization for this game.

#### Custom Save Path Overrides
For custom CIA forwarders (such as an installed forwarder that runs a 3DSX located in `/3ds/emerald3ds/` alongside its `.sav` file):
1. Select the title in the list and tap **`[ Save Path ]`**.
2. Navigate the SD card browser to locate your save file (`.sav`, `.srm`, `.dsv`, etc.).
3. Confirm the selection to permanently bind that save path to the title in `sdmc:/3ds/romm3ds/mapping.json`.

---

### 4. Controls Reference

| Button / Action | Function |
|---|---|
| **D-Pad Up / Down** | Navigate game list / menu options |
| **D-Pad Left / Right** | Page Up / Page Down through the list |
| **(A)** | Sync highlighted game / Confirm selection |
| **(B)** | Cancel dialog / Go back / Dismiss log |
| **(X)** | Batch sync all matched games in the current filter |
| **(Y)** | Search / filter games by title |
| **(L)** | Force upload local save for highlighted game |
| **(R)** | Force download remote save for highlighted game |
| **(SELECT)** | Open Manual RomM Match screen for highlighted game |
| **Touch Screen** | Tap buttons, pickers, search bar, or game list rows |
| **(START)** | Exit `romm3ds` to Homebrew Launcher / HOME Menu |

---

## Build & Installation

### Helper Scripts Overview

All common build and deployment workflows are wrapped in helper scripts under [`scripts/`](file:///Users/owen.lejeune/Documents/dev/romm3ds/scripts):

| Script | Purpose |
|---|---|
| [`./scripts/make.sh`](file:///Users/owen.lejeune/Documents/dev/romm3ds/scripts/make.sh) | Compiles `outputs/romm3ds.3dsx` and `outputs/romm3ds.smdh` using Docker |
| [`./scripts/make-clean.sh`](file:///Users/owen.lejeune/Documents/dev/romm3ds/scripts/make-clean.sh) | Cleans build artifacts and object files using Docker |
| [`./scripts/build-cia.sh`](file:///Users/owen.lejeune/Documents/dev/romm3ds/scripts/build-cia.sh) | Builds the forwarder ELF and packages `outputs/romm3ds.cia` |
| [`./scripts/3dslink-install.sh <IP>`](file:///Users/owen.lejeune/Documents/dev/romm3ds/scripts/3dslink-install.sh) | Compiles (if needed) and sends `romm3ds.3dsx` to your 3DS over Wi-Fi |

---

### 1. Prerequisites

You can compile `romm3ds` either using **Docker** (recommended, zero host toolchain required) or via a **native devkitPro** environment.

#### Option A: Docker (Recommended)

Requires [Docker Desktop](https://www.docker.com/) installed and running. No local Nintendo 3DS toolchain or libraries are needed.

To build the standard `.3dsx` homebrew executable:

```bash
./scripts/make.sh
```

#### Option B: Native Compilation via devkitPro

1. Install devkitPro via the [devkitPro Getting Started guide](https://devkitpro.org/wiki/Getting_Started).
2. Install 3DS development packages and portlibs:
   ```bash
   sudo dkp-pacman -S 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib 3ds-libpng 3ds-citro2d 3ds-citro3d
   ```
3. Set your environment variables (e.g. in `~/.zshrc` or `~/.bashrc`):
   ```bash
   export DEVKITPRO=/opt/devkitpro
   export DEVKITARM=$DEVKITPRO/devkitARM
   export PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH
   ```
4. Build using `make`:
   ```bash
   make
   ```

---

### 2. Packaging the HOME Menu Forwarder (.cia)

To generate the optional HOME Menu forwarder CIA (`outputs/romm3ds.cia`):

1. Ensure `bannertool` and `makerom` are present on your host system (e.g., in `~/.local/bin` or your system `PATH`).
2. Run the CIA build script:
   ```bash
   ./scripts/build-cia.sh
   ```

Outputs are placed in the `outputs/` directory:
- `outputs/romm3ds.3dsx` — Main 3DS executable
- `outputs/romm3ds.smdh` — Icon and application metadata
- `outputs/romm3ds.elf` — ELF executable
- `outputs/romm3ds.cia` — Lightweight HOME Menu forwarder CIA

---

### 3. Installing on Your 3DS

Ensure your 3DS is running modern custom firmware ([Luma3DS](https://github.com/LumaTeam/Luma3DS)).

#### Step 1: Install the .3dsx Binary (Required)

The executable must reside on your SD card at `sdmc:/3ds/romm3ds/romm3ds.3dsx`.

##### Method 1: SD Card Copy
Copy `outputs/romm3ds.3dsx` to your SD card:
```text
SD Card
└── 3ds/
    └── romm3ds/
        └── romm3ds.3dsx
```

##### Method 2: Wireless NetLoader (`3dslink`)
1. Connect your 3DS and computer to the same Wi-Fi network.
2. Open the **Homebrew Launcher** on your 3DS and press **(Y)** to activate NetLoader.
3. Deploy the binary using the helper script:
   ```bash
   ./scripts/3dslink-install.sh <3DS_IP_ADDRESS>
   ```

---

#### Step 2: Install the HOME Menu Forwarder .cia (Optional)

If you prefer launching `romm3ds` directly from your 3DS HOME Menu without opening the Homebrew Launcher:

> [!NOTE]
> The `.cia` is a lightweight **forwarder**, not a monolithic bundle. It delegates execution to `sdmc:/3ds/romm3ds/romm3ds.3dsx`. You only need to install the `.cia` once—future updates only require replacing `romm3ds.3dsx`.

1. Verify `romm3ds.3dsx` is on your SD card at `sdmc:/3ds/romm3ds/romm3ds.3dsx`.
2. Copy `outputs/romm3ds.cia` to your SD card (e.g. `sdmc:/cias/`).
3. Open **FBI** on your 3DS, navigate to `sdmc:/cias/romm3ds.cia`, and choose **Install and delete CIA**.
4. Press **HOME** to return to the HOME Menu, unwrap your new **RomM 3DS** gift, and launch it!

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
  2. In the **Save Conflict Resolution** screen, select **`[ Upload Save (Use Local Save) ]`**.
  3. `romm3ds` will read the raw save from your SD card (which contains your newest in-game progress) and upload it to your RomM server.

---

### Security & Self-Hosting Notes

- **SD Card Configuration**: Your RomM endpoint URL and API key are stored locally on your 3DS SD card at `sdmc:/3ds/romm3ds/config.json`. Treat this file as sensitive and avoid sharing it when posting logs or SD card backups.
- **TLS / HTTPS Certificates**: Because the Nintendo 3DS system firmware does not include an up-to-date CA certificate bundle for modern authorities (such as Let's Encrypt) or self-signed homelab certificates, `romm3ds` disables strict TLS peer verification (`CURLOPT_SSL_VERIFYPEER = 0L`) so HTTPS reverse-proxy endpoints work out of the box.

---

## AI Disclaimer

This project was developed and maintained with the assistance of Generative AI tools (including Google Antigravity and Gemini). While I am a professional software developer and have some familiarity with C++, I am by no means an expert. While all code has been tested, reviewed, and verified on real Nintendo 3DS hardware and clean build environments, community contributions, bug reports, and feedback are always welcome.

---

## License

This project is licensed under the [MIT License](LICENSE).
