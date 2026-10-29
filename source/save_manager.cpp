#include "save_manager.hpp"
#include "config_manager.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <set>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <map>
#include <png.h>
#include <nlohmann/json.hpp>

#if defined(__3DS__) || defined(_3DS)
#include <3ds.h>
#include <3ds/util/utf.h>

struct smdh_title_s {
    u16 shortDescription[64];
    u16 longDescription[128];
    u16 publisher[64];
};

struct smdh_s {
    char magic[4];
    u16 version;
    u16 reserved;
    smdh_title_s titles[16];
    u8 ratings[16];
    u32 region;
    u32 matchMakerId;
    u64 matchMakerBitFlag;
    u32 flags;
    u16 eulaVersion;
    u16 reserved2;
    u32 optimalBannerFrame;
    u32 streetpassId;
    u64 reserved3;
    u8 smallIcon[0x480];
    u8 largeIcon[0x1200];
};
#endif

namespace RomM3DS {

namespace {

#if defined(__3DS__) || defined(_3DS)
std::string decodeUtf16(const u16* src, size_t maxUnits, bool sanitize = false) {
    if (!src || src[0] == 0) {
        return "";
    }

    std::vector<u16> temp(maxUnits + 1, 0);
    size_t actualLen = 0;
    while (actualLen < maxUnits && src[actualLen] != 0) {
        temp[actualLen] = src[actualLen];
        actualLen++;
    }
    temp[actualLen] = 0;
    if (actualLen == 0) {
        return "";
    }

    std::vector<uint8_t> outBuf(actualLen * 4 + 1, 0);
    ssize_t units = utf16_to_utf8(outBuf.data(), temp.data(), outBuf.size() - 1);
    if (units <= 0) {
        return "";
    }
    outBuf[units] = '\0';
    std::string out(reinterpret_cast<char*>(outBuf.data()));

    if (sanitize) {
        std::string cleaned;
        bool inSpace = false;
        for (char c : out) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (!inSpace) {
                    cleaned += ' ';
                    inSpace = true;
                }
            } else {
                cleaned += c;
                inSpace = false;
            }
        }
        size_t first = cleaned.find_first_not_of(" ");
        if (first == std::string::npos) {
            return "";
        }
        size_t last = cleaned.find_last_not_of(" ");
        return cleaned.substr(first, last - first + 1);
    }

    return out;
}

std::string extractTitleString(const smdh_title_s& titleEntry) {
    std::string name = decodeUtf16(titleEntry.longDescription, 128, true);
    if (name.empty()) {
        name = decodeUtf16(titleEntry.shortDescription, 64, true);
    }
    return name;
}
#endif

} // namespace

static std::map<std::string, time_t> s_timestampCache;
static bool s_timestampCacheLoaded = false;
static const std::string s_timestampCachePath = "sdmc:/3ds/romm3ds/timestamp_cache.json";

SaveManager::SaveManager() {
    loadTimestampCache();
}

SaveManager::~SaveManager() {
    saveTimestampCache();
}

void SaveManager::loadTimestampCache(const std::string& path) {
    std::string filePath = path.empty() ? s_timestampCachePath : path;
    s_timestampCache.clear();
    s_timestampCacheLoaded = true;

    std::ifstream f(filePath);
    if (!f.is_open()) {
        if (filePath == s_timestampCachePath) {
            std::ifstream fLocal("./timestamp_cache.json");
            if (fLocal.is_open()) {
                nlohmann::json j = nlohmann::json::parse(fLocal, nullptr, false);
                if (j.is_object()) {
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        if (it.value().is_number()) {
                            s_timestampCache[it.key()] = it.value().get<time_t>();
                        }
                    }
                }
            }
        }
        return;
    }

    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_number()) {
                s_timestampCache[it.key()] = it.value().get<time_t>();
            }
        }
    }
}

void SaveManager::saveTimestampCache(const std::string& path) {
    if (!s_timestampCacheLoaded) {
        return;
    }
    std::string filePath = path.empty() ? s_timestampCachePath : path;
    ConfigManager::ensureDirectories(filePath);

    std::ofstream f(filePath);
    if (!f.is_open()) {
        if (filePath == s_timestampCachePath) {
            std::ofstream fLocal("./timestamp_cache.json");
            if (fLocal.is_open()) {
                nlohmann::json j = s_timestampCache;
                fLocal << j.dump(4);
            }
        }
        return;
    }

    nlohmann::json j = s_timestampCache;
    f << j.dump(4);
}

time_t SaveManager::getCachedSaveTime(const std::string& titleIdHex) {
    if (titleIdHex.empty()) return 0;
    if (!s_timestampCacheLoaded) {
        loadTimestampCache();
    }
    auto it = s_timestampCache.find(titleIdHex);
    if (it != s_timestampCache.end()) {
        return it->second;
    }
    return 0;
}

void SaveManager::setCachedSaveTime(const std::string& titleIdHex, time_t t) {
    if (titleIdHex.empty() || t <= 0) return;
    if (!s_timestampCacheLoaded) {
        loadTimestampCache();
    }
    s_timestampCache[titleIdHex] = t;
}

std::string SaveManager::formatTitleId(uint64_t titleId) {
    std::ostringstream ss;
    ss << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << titleId;
    return ss.str();
}

uint64_t SaveManager::parseTitleId(const std::string& hexStr) {
    return std::strtoull(hexStr.c_str(), nullptr, 16);
}

std::string SaveManager::determineConsole(const std::string& productCode, uint64_t titleId) {
    if (!productCode.empty()) {
        std::string p = productCode;
        for (char& c : p) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (p.rfind("CTR", 0) == 0 || p.rfind("KTR", 0) == 0) return "3DS";
        if (p.rfind("TWL", 0) == 0 || p.rfind("NTR", 0) == 0 || p.rfind("DS", 0) == 0) return "DS";
        if (p.rfind("AGB", 0) == 0) return "GBA";
        if (p.rfind("DMG", 0) == 0) return "GB";
        if (p.rfind("CGB", 0) == 0) return "GBC";
        if (p.rfind("NES", 0) == 0 || p.rfind("FAM", 0) == 0 || p.rfind("FC", 0) == 0) return "NES";
        if (p.rfind("SNS", 0) == 0 || p.rfind("SHVC", 0) == 0 || p.rfind("SFC", 0) == 0 || p.rfind("SNES", 0) == 0) return "SNES";
        if (p.rfind("GG", 0) == 0 || p.rfind("SEG", 0) == 0) return "GG";
        if (p.rfind("PCE", 0) == 0 || p.rfind("TGX", 0) == 0) return "PCE";
    }

    uint32_t tidHigh = static_cast<uint32_t>((titleId >> 32) & 0xFFFFFFFF);
    if (tidHigh == 0x00048004 || tidHigh == 0x00048005 || tidHigh == 0x0004800F) {
        return "DS";
    }

    return "3DS";
}

#if defined(__3DS__) || defined(_3DS)

std::string SaveManager::getTitleNameFromSmdh(uint64_t titleId, uint8_t mediaType) {
    u32 archPath[4] = {
        static_cast<u32>(titleId & 0xFFFFFFFF),
        static_cast<u32>((titleId >> 32) & 0xFFFFFFFF),
        static_cast<u32>(mediaType),
        0x00000000
    };
    static const u32 filePath[5] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000};

    FS_Path apath = { PATH_BINARY, sizeof(archPath), archPath };
    FS_Path fpath = { PATH_BINARY, sizeof(filePath), filePath };

    Handle file = 0;
    Result res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, apath, fpath, FS_OPEN_READ, 0);
    if (R_FAILED(res)) {
        return "";
    }

    smdh_s smdh;
    std::memset(&smdh, 0, sizeof(smdh));
    u32 bytesRead = 0;
    res = FSFILE_Read(file, &bytesRead, 0, &smdh, sizeof(smdh_s));
    FSFILE_Close(file);

    if (R_SUCCEEDED(res) && bytesRead >= 0x2040 && std::memcmp(smdh.magic, "SMDH", 4) == 0) {
        u8 sysLang = 1;
        if (R_SUCCEEDED(CFGU_GetSystemLanguage(&sysLang)) && sysLang < 16) {
            std::string name = extractTitleString(smdh.titles[sysLang]);
            if (!name.empty()) return name;
        }

        std::string enName = extractTitleString(smdh.titles[1]);
        if (!enName.empty()) return enName;

        std::string jpName = extractTitleString(smdh.titles[0]);
        if (!jpName.empty()) return jpName;

        for (int i = 0; i < 16; ++i) {
            std::string anyName = extractTitleString(smdh.titles[i]);
            if (!anyName.empty()) return anyName;
        }
    }
    return "";
}

namespace {

inline uint32_t picaTileOffset(int px, int py) {
    return ((py & 4) << 3) | ((px & 4) << 2) |
           ((py & 2) << 2) | ((px & 2) << 1) |
           ((py & 1) << 1) | (px & 1);
}

bool extractNdsBannerIcon(const std::string& romPath, std::vector<uint8_t>& outIconData) {
    FILE* f = fopen(romPath.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0x200) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0x68, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    uint32_t bannerOffset = 0;
    if (fread(&bannerOffset, sizeof(uint32_t), 1, f) != 1 || bannerOffset == 0 ||
        bannerOffset + 0x240 > static_cast<uint32_t>(fsize)) {
        fclose(f);
        return false;
    }

    if (fseek(f, bannerOffset + 0x20, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    uint8_t iconTiles[512];
    if (fread(iconTiles, 1, 512, f) != 512) {
        fclose(f);
        return false;
    }

    if (fseek(f, bannerOffset + 0x220, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    uint16_t rawPalette[16];
    if (fread(rawPalette, sizeof(uint16_t), 16, f) != 16) {
        fclose(f);
        return false;
    }
    fclose(f);

    uint16_t paletteRgb565[16];
    paletteRgb565[0] = 0x31A9; // Background color for transparent pixels (CLR_BORDER_OFF)
    for (int i = 1; i < 16; ++i) {
        uint16_t bgr = rawPalette[i];
        uint16_t r5 = bgr & 0x1F;
        uint16_t g5 = (bgr >> 5) & 0x1F;
        uint16_t b5 = (bgr >> 10) & 0x1F;
        uint16_t g6 = (g5 << 1) | (g5 >> 4);
        paletteRgb565[i] = (r5 << 11) | (g6 << 5) | b5;
    }

    outIconData.assign(0x1200, 0);
    uint16_t* outWords = reinterpret_cast<uint16_t*>(outIconData.data());

    for (int Y = 0; Y < 48; ++Y) {
        int srcY = (Y * 32) / 48;
        int ndsTileY = srcY / 8;
        int ndsPy = srcY % 8;
        int outTy = Y / 8;
        int outPy = Y % 8;

        for (int X = 0; X < 48; ++X) {
            int srcX = (X * 32) / 48;
            int ndsTileX = srcX / 8;
            int ndsPx = srcX % 8;

            int ndsTileIdx = ndsTileY * 4 + ndsTileX;
            int byteOffset = ndsTileIdx * 32 + (ndsPy * 4) + (ndsPx / 2);
            uint8_t byteVal = iconTiles[byteOffset];
            uint8_t colorIdx = (ndsPx & 1) ? ((byteVal >> 4) & 0x0F) : (byteVal & 0x0F);
            uint16_t color = paletteRgb565[colorIdx];

            int outTx = X / 8;
            int outPx = X % 8;
            int outTileIdx = outTy * 6 + outTx;
            int tileOffset = picaTileOffset(outPx, outPy);

            outWords[outTileIdx * 64 + tileOffset] = color;
        }
    }

    return true;
}

bool loadPngToTiledRgb565(const std::string& path, std::vector<uint8_t>& outIconData) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(fp); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); fclose(fp); return false; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_png(png, info, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND, nullptr);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_bytep* row_pointers = png_get_rows(png, info);

    if (width <= 0 || height <= 0 || !row_pointers) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    outIconData.assign(0x1200, 0);
    uint16_t* outWords = reinterpret_cast<uint16_t*>(outIconData.data());

    int channels = (color_type == PNG_COLOR_TYPE_RGBA) ? 4 : (color_type == PNG_COLOR_TYPE_RGB ? 3 : 1);

    for (int Y = 0; Y < 48; ++Y) {
        int srcY = (Y * height) / 48;
        png_bytep row = row_pointers[srcY];
        int outTy = Y / 8;
        int outPy = Y % 8;

        for (int X = 0; X < 48; ++X) {
            int srcX = (X * width) / 48;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (channels >= 3) {
                r = row[srcX * channels];
                g = row[srcX * channels + 1];
                b = row[srcX * channels + 2];
                if (channels == 4) a = row[srcX * channels + 3];
            } else {
                r = g = b = row[srcX];
            }

            uint16_t rgb565;
            if (a < 64) {
                rgb565 = 0x31A9;
            } else {
                uint16_t r5 = (r * 31 + 127) / 255;
                uint16_t g6 = (g * 63 + 127) / 255;
                uint16_t b5 = (b * 31 + 127) / 255;
                rgb565 = (r5 << 11) | (g6 << 5) | b5;
            }

            int outTx = X / 8;
            int outPx = X % 8;
            int outTileIdx = outTy * 6 + outTx;
            int tileOffset = picaTileOffset(outPx, outPy);

            outWords[outTileIdx * 64 + tileOffset] = rgb565;
        }
    }

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);
    return true;
}

bool generateConsoleBadgeIcon(const std::string& console, std::vector<uint8_t>& outIconData) {
    uint16_t grid[48][48];
    constexpr uint16_t BG_COLOR = 0x31A9; // Matches CLR_BORDER_OFF
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            grid[y][x] = BG_COLOR;
        }
    }

    std::string upperConsole = console;
    for (char& c : upperConsole) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (upperConsole == "GBA" || upperConsole == "AGB") {
        constexpr uint16_t BODY_COLOR = 0x49F1;
        constexpr uint16_t BODY_DARK  = 0x316B;
        constexpr uint16_t GRIP_COLOR = 0x290C;
        constexpr uint16_t LABEL_BG   = 0x08A6;
        constexpr uint16_t LABEL_EDGE = 0xBDF7;
        constexpr uint16_t TEXT_COLOR = 0xFFFF;
        constexpr uint16_t TEXT_SHAD  = 0x52AA;

        for (int y = 6; y <= 42; ++y) {
            for (int x = 5; x <= 42; ++x) {
                if ((y == 6 && (x <= 6 || x >= 41)) ||
                    (y == 7 && (x == 5 || x == 42)) ||
                    (y == 42 && (x <= 6 || x >= 41)) ||
                    (y == 41 && (x == 5 || x == 42))) {
                    continue;
                }
                grid[y][x] = (x == 42 || y == 42) ? BODY_DARK : BODY_COLOR;
            }
        }
        for (int y = 8; y <= 10; ++y) {
            for (int x = 10; x <= 37; ++x) {
                grid[y][x] = GRIP_COLOR;
            }
        }
        for (int y = 14; y <= 37; ++y) {
            for (int x = 8; x <= 39; ++x) {
                if (x == 8 || x == 39 || y == 14 || y == 37) {
                    grid[y][x] = LABEL_EDGE;
                } else {
                    grid[y][x] = LABEL_BG;
                }
            }
        }
        static const char* fontGBA[7] = {
            " ###   ####    ### ",
            "#   #  #   #  #   #",
            "#      #   #  #   #",
            "# ###  ####   #####",
            "#   #  #   #  #   #",
            "#   #  #   #  #   #",
            " ###   ####   #   #"
        };
        for (int r = 0; r < 7; ++r) {
            for (int c = 0; c < 19; ++c) {
                if (fontGBA[r][c] == '#') {
                    grid[23 + r][15 + c] = TEXT_COLOR;
                    if (grid[24 + r][16 + c] == LABEL_BG) {
                        grid[24 + r][16 + c] = TEXT_SHAD;
                    }
                }
            }
        }
    } else if (upperConsole == "DS" || upperConsole == "NDS") {
        constexpr uint16_t BODY_COLOR = 0x2965;
        constexpr uint16_t BODY_DARK  = 0x18C3;
        constexpr uint16_t LABEL_BG   = 0xF7DE;
        constexpr uint16_t LABEL_EDGE = 0x8C71;
        constexpr uint16_t TEXT_COLOR = 0xB000;
        constexpr uint16_t TEXT_DARK  = 0x2104;

        for (int y = 6; y <= 42; ++y) {
            for (int x = 6; x <= 41; ++x) {
                if ((y == 6 && (x <= 7 || x >= 40)) ||
                    (y == 42 && (x <= 7 || x >= 40))) {
                    continue;
                }
                grid[y][x] = (x == 41 || y == 42) ? BODY_DARK : BODY_COLOR;
            }
        }
        for (int y = 6; y <= 9; ++y) {
            for (int x = 16; x <= 31; ++x) {
                grid[y][x] = BODY_DARK;
            }
        }
        for (int y = 14; y <= 38; ++y) {
            for (int x = 9; x <= 38; ++x) {
                if (x == 9 || x == 38 || y == 14 || y == 38) {
                    grid[y][x] = LABEL_EDGE;
                } else {
                    grid[y][x] = LABEL_BG;
                }
            }
        }
        static const char* fontDS[7] = {
            "####    #### ",
            "#   #  #     ",
            "#   #   ###  ",
            "#   #      # ",
            "#   #      # ",
            "#   #  #   # ",
            "####    ###  "
        };
        for (int r = 0; r < 7; ++r) {
            for (int c = 0; c < 13; ++c) {
                if (fontDS[r][c] == '#') {
                    grid[23 + r][18 + c] = (c < 6) ? TEXT_COLOR : TEXT_DARK;
                }
            }
        }
    } else {
        constexpr uint16_t BODY_COLOR = 0xCE79;
        constexpr uint16_t BODY_DARK  = 0x8410;
        constexpr uint16_t LABEL_BG   = 0x18C5;
        constexpr uint16_t LABEL_EDGE = 0x4A69;
        constexpr uint16_t TEXT_COLOR = 0xFFFF;

        for (int y = 6; y <= 42; ++y) {
            for (int x = 6; x <= 41; ++x) {
                if ((y == 6 && (x <= 7 || x >= 40)) ||
                    (y == 42 && (x <= 7 || x >= 40))) {
                    continue;
                }
                grid[y][x] = (x == 41 || y == 42) ? BODY_DARK : BODY_COLOR;
            }
        }
        for (int y = 14; y <= 38; ++y) {
            for (int x = 9; x <= 38; ++x) {
                if (x == 9 || x == 38 || y == 14 || y == 38) {
                    grid[y][x] = LABEL_EDGE;
                } else {
                    grid[y][x] = LABEL_BG;
                }
            }
        }
        for (int r = 0; r < 5; ++r) {
            grid[24 + r][23] = TEXT_COLOR;
            grid[24 + r][24] = TEXT_COLOR;
        }
    }

    outIconData.assign(0x1200, 0);
    uint16_t* outWords = reinterpret_cast<uint16_t*>(outIconData.data());
    for (int Y = 0; Y < 48; ++Y) {
        int outTy = Y / 8;
        int outPy = Y % 8;
        for (int X = 0; X < 48; ++X) {
            int outTx = X / 8;
            int outPx = X % 8;
            int outTileIdx = outTy * 6 + outTx;
            int tileOffset = picaTileOffset(outPx, outPy);
            outWords[outTileIdx * 64 + tileOffset] = grid[Y][X];
        }
    }
    return true;
}

} // namespace

bool SaveManager::getTitleIcon(const InstalledTitle& title, std::vector<uint8_t>& outIconData) {
    outIconData.clear();

    // 1. For installed CTR titles (non-file ROMs with a valid titleId), query system content archive
#if defined(__3DS__) || defined(_3DS)
    if (!title.isFileRom && title.titleId != 0) {
        FS_MediaType mediaTypesToTry[] = {
            static_cast<FS_MediaType>(title.mediaType),
            MEDIATYPE_SD,
            MEDIATYPE_NAND
        };
        for (auto mType : mediaTypesToTry) {
            u32 archPath[4] = {
                static_cast<u32>(title.titleId & 0xFFFFFFFF),
                static_cast<u32>((title.titleId >> 32) & 0xFFFFFFFF),
                static_cast<u32>(mType),
                0x00000000
            };
            static const u32 filePath[5] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000}; // "icon"

            FS_Path apath = { PATH_BINARY, sizeof(archPath), archPath };
            FS_Path fpath = { PATH_BINARY, sizeof(filePath), filePath };

            Handle file = 0;
            Result res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, apath, fpath, FS_OPEN_READ, 0);
            if (R_SUCCEEDED(res)) {
                smdh_s smdh;
                std::memset(&smdh, 0, sizeof(smdh));
                u32 bytesRead = 0;
                res = FSFILE_Read(file, &bytesRead, 0, &smdh, sizeof(smdh_s));
                FSFILE_Close(file);

                if (R_SUCCEEDED(res) && bytesRead >= 0x2040 && std::memcmp(smdh.magic, "SMDH", 4) == 0) {
                    outIconData.assign(smdh.largeIcon, smdh.largeIcon + sizeof(smdh.largeIcon));
                    return true;
                }
            }
        }
    }
#endif

    // 2. For Nintendo DS games, extract embedded banner icon from .nds/.dsi ROM
    if (!title.romPath.empty()) {
        std::string lowerRom = title.romPath;
        for (char& c : lowerRom) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lowerRom.size() >= 4 && (lowerRom.substr(lowerRom.size() - 4) == ".nds" || lowerRom.substr(lowerRom.size() - 4) == ".dsi")) {
            if (extractNdsBannerIcon(title.romPath, outIconData)) {
                return true;
            }
        }
    }

    // 3. Check for SMDH files (embedded in 3DSX or adjacent .smdh)
    auto readSmdhFromFile = [&](const std::string& path) -> bool {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0x2040) {
            fclose(f);
            return false;
        }

        uint32_t header[10] = {0};
        if (fread(header, sizeof(uint32_t), 10, f) == 10 && header[0] == 0x58534433) { // '3DSX'
            u32 smdhOffset = header[5];
            if (smdhOffset != 0 && fseek(f, smdhOffset, SEEK_SET) == 0) {
#if defined(__3DS__) || defined(_3DS)
                smdh_s smdh;
                if (fread(&smdh, 1, sizeof(smdh_s), f) >= 0x2040 && std::memcmp(smdh.magic, "SMDH", 4) == 0) {
                    outIconData.assign(smdh.largeIcon, smdh.largeIcon + sizeof(smdh.largeIcon));
                    fclose(f);
                    return true;
                }
#endif
            }
        } else {
            fseek(f, 0, SEEK_SET);
#if defined(__3DS__) || defined(_3DS)
            smdh_s smdh;
            if (fread(&smdh, 1, sizeof(smdh_s), f) >= 0x2040 && std::memcmp(smdh.magic, "SMDH", 4) == 0) {
                outIconData.assign(smdh.largeIcon, smdh.largeIcon + sizeof(smdh.largeIcon));
                fclose(f);
                return true;
            }
#endif
        }
        fclose(f);
        return false;
    };

    if (!title.romPath.empty()) {
        if (readSmdhFromFile(title.romPath)) return true;
        size_t dot = title.romPath.find_last_of('.');
        if (dot != std::string::npos) {
            if (readSmdhFromFile(title.romPath.substr(0, dot) + ".smdh")) return true;
        }
        size_t slash = title.romPath.find_last_of("/\\");
        if (slash != std::string::npos) {
            if (readSmdhFromFile(title.romPath.substr(0, slash + 1) + "icon.smdh")) return true;
        }
    }

    if (!title.savePath.empty()) {
        size_t slash = title.savePath.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string dir = title.savePath.substr(0, slash);
            DIR* d = opendir(dir.c_str());
            if (d) {
                struct dirent* e = nullptr;
                while ((e = readdir(d)) != nullptr) {
                    if (e->d_name[0] == '.') continue;
                    std::string fn = e->d_name;
                    std::string lowerFn = fn;
                    for (char& c : lowerFn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (lowerFn.size() >= 5 && (lowerFn.substr(lowerFn.size() - 5) == ".3dsx" || lowerFn.substr(lowerFn.size() - 5) == ".smdh")) {
                        std::string cand = dir + "/" + fn;
                        if (readSmdhFromFile(cand)) {
                            closedir(d);
                            return true;
                        }
                    }
                    if (lowerFn.size() >= 4 && (lowerFn.substr(lowerFn.size() - 4) == ".nds" || lowerFn.substr(lowerFn.size() - 4) == ".dsi")) {
                        std::string cand = dir + "/" + fn;
                        if (extractNdsBannerIcon(cand, outIconData)) {
                            closedir(d);
                            return true;
                        }
                    }
                }
                closedir(d);
            }
        }
    }

    // 4. Check for PNG image files
    auto tryPng = [&](const std::string& p) -> bool {
        if (p.empty()) return false;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && st.st_size > 0) {
            return loadPngToTiledRgb565(p, outIconData);
        }
        return false;
    };

    if (!title.romPath.empty()) {
        size_t dot = title.romPath.find_last_of('.');
        if (dot != std::string::npos) {
            if (tryPng(title.romPath.substr(0, dot) + ".png")) return true;
        }
        size_t slash = title.romPath.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string dir = title.romPath.substr(0, slash);
            if (tryPng(dir + "/icon.png")) return true;
            if (tryPng(dir + "/cover.png")) return true;
        }
    }

    if (!title.savePath.empty()) {
        size_t slash = title.savePath.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string dir = title.savePath.substr(0, slash);
            if (tryPng(dir + "/icon.png")) return true;
            if (tryPng(dir + "/cover.png")) return true;
        }
    }

    if (!title.name.empty()) {
        if (tryPng("sdmc:/3ds/romm3ds/covers/" + title.name + ".png")) return true;
        if (tryPng("sdmc:/mGBA/covers/" + title.name + ".png")) return true;
        if (tryPng("sdmc:/mGBA/boxart/" + title.name + ".png")) return true;
        if (tryPng("sdmc:/_nds/TWiLightMenu/boxart/" + title.name + ".png")) return true;
    }

    // 5. Fallback: generate high-quality retro console badge icon
    return generateConsoleBadgeIcon(title.console, outIconData);
}

#if defined(__3DS__) || defined(_3DS)
static std::string formatResultHex(Result res) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "0x%08lX (L=%u, S=%u, M=%u, D=%u)",
                  static_cast<unsigned long>(static_cast<u32>(res)),
                  static_cast<unsigned>(R_LEVEL(res)),
                  static_cast<unsigned>(R_SUMMARY(res)),
                  static_cast<unsigned>(R_MODULE(res)),
                  static_cast<unsigned>(R_DESCRIPTION(res)));
    return std::string(buf);
}

static std::vector<std::pair<std::string, u64>> listArchiveFiles(FS_Archive archive, const std::string& archivePath) {
    std::vector<std::pair<std::string, u64>> results;
    Handle dirHandle = 0;
    FS_Path dPath = fsMakePath(PATH_ASCII, archivePath.c_str());
    if (R_FAILED(FSUSER_OpenDirectory(&dirHandle, archive, dPath))) {
        return results;
    }

    constexpr u32 ENTRY_COUNT = 32;
    std::vector<FS_DirectoryEntry> entries(ENTRY_COUNT);
    u32 readCount = 0;

    while (R_SUCCEEDED(FSDIR_Read(dirHandle, &readCount, ENTRY_COUNT, entries.data())) && readCount > 0) {
        for (u32 i = 0; i < readCount; ++i) {
            std::string entryName = decodeUtf16(entries[i].name, 256);
            if (entryName == "." || entryName == "..") continue;
            std::string itemPath = (archivePath == "/") ? ("/" + entryName) : (archivePath + "/" + entryName);
            if (entries[i].attributes & FS_ATTRIBUTE_DIRECTORY) {
                auto sub = listArchiveFiles(archive, itemPath);
                results.insert(results.end(), sub.begin(), sub.end());
            } else {
                results.push_back({itemPath, entries[i].fileSize});
            }
        }
    }
    FSDIR_Close(dirHandle);
    return results;
}
#endif

bool SaveManager::copyArchiveDirToFs(uint64_t archiveHandle, const std::string& archivePath, const std::string& fsDestPath) {
    FS_Archive archive = static_cast<FS_Archive>(archiveHandle);
    Handle dirHandle = 0;
    FS_Path dPath = fsMakePath(PATH_ASCII, archivePath.c_str());

    Result res = FSUSER_OpenDirectory(&dirHandle, archive, dPath);
    if (R_FAILED(res)) {
        return false;
    }

    ConfigManager::ensureDirectories(fsDestPath + "/");

    constexpr u32 ENTRY_COUNT = 32;
    std::vector<FS_DirectoryEntry> entries(ENTRY_COUNT);
    u32 readCount = 0;

    bool success = true;

    while (R_SUCCEEDED(FSDIR_Read(dirHandle, &readCount, ENTRY_COUNT, entries.data())) && readCount > 0) {
        for (u32 i = 0; i < readCount; ++i) {
            std::string entryName = decodeUtf16(entries[i].name, 256);
            if (entryName == "." || entryName == "..") {
                continue;
            }

            std::string nextArchivePath = (archivePath == "/") ? ("/" + entryName) : (archivePath + "/" + entryName);
            std::string nextFsPath = fsDestPath + "/" + entryName;

            if (entries[i].attributes & FS_ATTRIBUTE_DIRECTORY) {
                ConfigManager::ensureDirectories(nextFsPath + "/");
                if (!copyArchiveDirToFs(archiveHandle, nextArchivePath, nextFsPath)) {
                    success = false;
                }
            } else {
                Handle srcFile = 0;
                FS_Path fPath = fsMakePath(PATH_ASCII, nextArchivePath.c_str());
                if (R_FAILED(FSUSER_OpenFile(&srcFile, archive, fPath, FS_OPEN_READ, 0))) {
                    success = false;
                    continue;
                }

                ConfigManager::ensureDirectories(nextFsPath);
                FILE* dstFile = fopen(nextFsPath.c_str(), "wb");
                if (!dstFile) {
                    FSFILE_Close(srcFile);
                    success = false;
                    continue;
                }

                constexpr u32 BUFFER_SIZE = 0x10000;
                std::vector<u8> buffer(BUFFER_SIZE);
                u64 offset = 0;
                u32 bytesRead = 0;

                while (R_SUCCEEDED(FSFILE_Read(srcFile, &bytesRead, offset, buffer.data(), BUFFER_SIZE)) && bytesRead > 0) {
                    fwrite(buffer.data(), 1, bytesRead, dstFile);
                    offset += bytesRead;
                }

                fclose(dstFile);
                FSFILE_Close(srcFile);
            }
        }
    }

    FSDIR_Close(dirHandle);
    return success;
}

bool SaveManager::copyFsDirToArchive(uint64_t archiveHandle, const std::string& fsSrcPath, const std::string& archiveDestPath, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
    FS_Archive archive = static_cast<FS_Archive>(archiveHandle);
    DIR* dir = opendir(fsSrcPath.c_str());
    if (!dir) {
        emit("[ERROR] Could not open staging directory: " + fsSrcPath);
        return false;
    }

    bool success = true;
    struct dirent* entry = nullptr;
    int itemsFound = 0;
    int filesWritten = 0;

    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        // Skip macOS dotfiles, hidden files, and desktop metadata (._*, .DS_Store)
        if (name[0] == '.') {
            emit("[DEBUG] Skipping hidden file: " + name);
            continue;
        }

        itemsFound++;
        std::string srcItem = fsSrcPath + "/" + name;
        std::string dstItem = (archiveDestPath == "/") ? ("/" + name) : (archiveDestPath + "/" + name);

        struct stat st;
        if (stat(srcItem.c_str(), &st) != 0) {
            emit("[WARN] Could not stat " + srcItem);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            emit("Creating directory in archive: " + dstItem);
            FS_Path dPath = fsMakePath(PATH_ASCII, dstItem.c_str());
            Result resDir = FSUSER_CreateDirectory(archive, dPath, 0);
            if (R_FAILED(resDir)) {
                // If directory already exists, it is not fatal
                emit("[DEBUG] FSUSER_CreateDirectory returned " + formatResultHex(resDir) + " for " + dstItem);
            }
            if (!copyFsDirToArchive(archiveHandle, srcItem, dstItem, log)) {
                success = false;
            }
        } else {
            FILE* srcFile = fopen(srcItem.c_str(), "rb");
            if (!srcFile) {
                emit("[ERROR] Failed opening staging file: " + srcItem);
                success = false;
                continue;
            }

            fseek(srcFile, 0, SEEK_END);
            u64 fileSize = ftell(srcFile);
            fseek(srcFile, 0, SEEK_SET);

            emit("Writing file: " + dstItem + " (" + std::to_string(fileSize) + " B)");

            FS_Path fPath = fsMakePath(PATH_ASCII, dstItem.c_str());
            Result resDel = FSUSER_DeleteFile(archive, fPath);
            if (R_FAILED(resDel)) {
                emit("[DEBUG] FSUSER_DeleteFile for " + dstItem + " returned " + formatResultHex(resDel));
            }

            Result resCreate = FSUSER_CreateFile(archive, fPath, 0, fileSize);
            if (R_FAILED(resCreate)) {
                emit("[ERROR] FSUSER_CreateFile failed for " + dstItem + " (size: " + std::to_string(fileSize) + " B): " + formatResultHex(resCreate));
                fclose(srcFile);
                success = false;
                continue;
            }

            Handle dstFile = 0;
            Result resOpen = FSUSER_OpenFile(&dstFile, archive, fPath, FS_OPEN_WRITE, 0);
            if (R_FAILED(resOpen)) {
                emit("[ERROR] FSUSER_OpenFile failed for " + dstItem + ": " + formatResultHex(resOpen));
                fclose(srcFile);
                success = false;
                continue;
            }

            constexpr size_t BUFFER_SIZE = 0x10000;
            std::vector<u8> buffer(BUFFER_SIZE);
            u64 offset = 0;
            size_t bytesRead = 0;
            bool writeOk = true;

            while ((bytesRead = fread(buffer.data(), 1, BUFFER_SIZE, srcFile)) > 0) {
                u32 bytesWritten = 0;
                Result resWrite = FSFILE_Write(dstFile, &bytesWritten, offset, buffer.data(), bytesRead, FS_WRITE_FLUSH);
                if (R_FAILED(resWrite)) {
                    emit("[ERROR] FSFILE_Write failed for " + dstItem + " at offset " + std::to_string(offset) + ": " + formatResultHex(resWrite));
                    writeOk = false;
                    break;
                }
                if (bytesWritten != bytesRead) {
                    emit("[ERROR] Short write for " + dstItem + ": wrote " + std::to_string(bytesWritten) + " of " + std::to_string(bytesRead) + " bytes");
                    writeOk = false;
                    break;
                }
                offset += bytesWritten;
            }

            FSFILE_Close(dstFile);
            fclose(srcFile);

            if (!writeOk) {
                success = false;
            } else {
                filesWritten++;
                emit("[OK] Successfully wrote " + dstItem + " (" + std::to_string(offset) + " B)");
            }
        }
    }

    closedir(dir);
    if (itemsFound == 0) {
        emit("[WARN] No files or directories found in staging: " + fsSrcPath);
    } else {
        emit("[INFO] Archive transfer complete: " + std::to_string(filesWritten) + " file(s) written.");
    }
    return success;
}

#endif

uint64_t SaveManager::hashRomPath(const std::string& path) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : path) {
        hash ^= static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(c)));
        hash *= 1099511628211ULL;
    }
    return hash;
}

namespace {

std::vector<std::string> s_discoveredSavePaths;

std::vector<std::string> getMgbaSaveDirectories() {
    static std::vector<std::string> cachedDirs;
    static bool resolved = false;
    if (resolved) return cachedDirs;

    std::vector<std::string> dirs;

    const std::vector<std::string> configPaths = {
        "sdmc:/3ds/mgba/config.ini",
        "sdmc:/3ds/mGBA/config.ini",
        "sdmc:/mgba/config.ini",
        "sdmc:/mGBA/config.ini"
    };

    for (const auto& cp : configPaths) {
        std::ifstream file(cp);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                size_t eqPos = line.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = line.substr(0, eqPos);
                    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(key.begin());
                    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();

                    if (key == "savegamePath") {
                        std::string val = line.substr(eqPos + 1);
                        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
                        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) val.pop_back();
                        if (!val.empty()) {
                            if (val.front() == '"' && val.back() == '"') {
                                val = val.substr(1, val.length() - 2);
                            }
                            std::string fullDir;
                            if (val.rfind("sdmc:", 0) == 0) {
                                fullDir = val;
                            } else if (val.front() == '/') {
                                fullDir = "sdmc:" + val;
                            } else {
                                size_t cSlash = cp.find_last_of('/');
                                std::string baseDir = (cSlash != std::string::npos) ? cp.substr(0, cSlash) : "sdmc:/3ds/mgba";
                                fullDir = baseDir + "/" + val;
                            }
                            if (!fullDir.empty() && fullDir.back() != '/') {
                                fullDir += '/';
                            }
                            if (std::find(dirs.begin(), dirs.end(), fullDir) == dirs.end()) {
                                dirs.push_back(fullDir);
                            }
                        }
                    }
                }
            }
        }
    }

    const std::vector<std::string> defaults = {
        "sdmc:/3ds/mgba/saves/",
        "sdmc:/3ds/mGBA/saves/",
        "sdmc:/3ds/mgba/",
        "sdmc:/3ds/mGBA/",
        "sdmc:/mgba/saves/",
        "sdmc:/mgba/",
        "sdmc:/mGBA/saves/",
        "sdmc:/mGBA/"
    };
    for (const auto& def : defaults) {
        if (std::find(dirs.begin(), dirs.end(), def) == dirs.end()) {
            dirs.push_back(def);
        }
    }

    cachedDirs = dirs;
    resolved = true;
    return cachedDirs;
}

} // namespace

std::string SaveManager::findSaveFileForRom(const std::string& romPath, const std::string& console) {
    size_t slashPos = romPath.find_last_of("/\\");
    std::string dir = (slashPos != std::string::npos) ? romPath.substr(0, slashPos) : ".";
    std::string filename = (slashPos != std::string::npos) ? romPath.substr(slashPos + 1) : romPath;

    size_t dotPos = filename.find_last_of('.');
    std::string baseName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;

    std::string lowerBase = baseName;
    for (char& c : lowerBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto pathEqual = [](const std::string& a, const std::string& b) -> bool {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            char ca = a[i];
            char cb = b[i];
            if (ca == '\\') ca = '/';
            if (cb == '\\') cb = '/';
            if (std::tolower(static_cast<unsigned char>(ca)) != std::tolower(static_cast<unsigned char>(cb))) {
                return false;
            }
        }
        return true;
    };

    auto pickNewestExisting = [](const std::vector<std::string>& paths) -> std::string {
        std::string best;
        time_t bestTime = 0;
        for (const auto& p : paths) {
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && st.st_size > 0) {
                if (best.empty() || st.st_mtime > bestTime) {
                    best = p;
                    bestTime = st.st_mtime;
                }
            }
        }
        return best;
    };

    // 1. FIRST PRIORITY: Look directly in the same directory as the game ROM
    std::vector<std::string> sameDirCandidates;
    sameDirCandidates.push_back(dir + "/" + baseName + ".sav");
    sameDirCandidates.push_back(dir + "/" + baseName + ".SAV");
    if (console == "DS") {
        sameDirCandidates.push_back(dir + "/" + baseName + ".dsv");
        sameDirCandidates.push_back(dir + "/" + baseName + ".DSV");
    }
    sameDirCandidates.push_back(dir + "/" + baseName + ".srm");
    sameDirCandidates.push_back(dir + "/" + baseName + ".SRM");
    sameDirCandidates.push_back(dir + "/" + baseName + ".dat");

    // Also check any discovered saves located directly in `dir` matching baseName case-insensitively
    for (const auto& sp : s_discoveredSavePaths) {
        size_t sSlash = sp.find_last_of("/\\");
        std::string spDir = (sSlash != std::string::npos) ? sp.substr(0, sSlash) : "";
        if (pathEqual(spDir, dir)) {
            std::string sFname = (sSlash != std::string::npos) ? sp.substr(sSlash + 1) : sp;
            size_t sDot = sFname.find_last_of('.');
            std::string sBase = (sDot != std::string::npos) ? sFname.substr(0, sDot) : sFname;
            for (char& c : sBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (sBase == lowerBase) {
                sameDirCandidates.push_back(sp);
            }
        }
    }

    std::string found = pickNewestExisting(sameDirCandidates);
    if (!found.empty()) {
        return found;
    }

    // 2. SECOND PRIORITY: Check immediate "saves/" or "save/" subfolder in the game's directory
    std::vector<std::string> subDirCandidates;
    subDirCandidates.push_back(dir + "/saves/" + baseName + ".sav");
    subDirCandidates.push_back(dir + "/saves/" + baseName + ".SAV");
    subDirCandidates.push_back(dir + "/save/" + baseName + ".sav");
    subDirCandidates.push_back(dir + "/save/" + baseName + ".SAV");
    if (console == "DS") {
        subDirCandidates.push_back(dir + "/saves/" + baseName + ".dsv");
        subDirCandidates.push_back(dir + "/saves/" + baseName + ".DSV");
        subDirCandidates.push_back(dir + "/save/" + baseName + ".dsv");
    }
    subDirCandidates.push_back(dir + "/saves/" + baseName + ".srm");
    subDirCandidates.push_back(dir + "/saves/" + baseName + ".SRM");

    std::string savesDir1 = dir + "/saves";
    std::string savesDir2 = dir + "/save";
    for (const auto& sp : s_discoveredSavePaths) {
        size_t sSlash = sp.find_last_of("/\\");
        std::string spDir = (sSlash != std::string::npos) ? sp.substr(0, sSlash) : "";
        if (pathEqual(spDir, savesDir1) || pathEqual(spDir, savesDir2)) {
            std::string sFname = (sSlash != std::string::npos) ? sp.substr(sSlash + 1) : sp;
            size_t sDot = sFname.find_last_of('.');
            std::string sBase = (sDot != std::string::npos) ? sFname.substr(0, sDot) : sFname;
            for (char& c : sBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (sBase == lowerBase) {
                subDirCandidates.push_back(sp);
            }
        }
    }

    found = pickNewestExisting(subDirCandidates);
    if (!found.empty()) {
        return found;
    }

    // 3. THIRD PRIORITY: Check parent directory or parent's "saves/" folder
    size_t parentSlash = dir.find_last_of("/\\");
    std::string parentDir = (parentSlash != std::string::npos) ? dir.substr(0, parentSlash) : "";
    if (!parentDir.empty() && parentDir != "sdmc:" && parentDir != "sdmc" && parentDir != "/" && parentDir != ".") {
        std::vector<std::string> parentCandidates;
        parentCandidates.push_back(parentDir + "/saves/" + baseName + ".sav");
        parentCandidates.push_back(parentDir + "/saves/" + baseName + ".SAV");
        parentCandidates.push_back(parentDir + "/" + baseName + ".sav");
        parentCandidates.push_back(parentDir + "/" + baseName + ".SAV");
        if (console == "DS") {
            parentCandidates.push_back(parentDir + "/saves/" + baseName + ".dsv");
            parentCandidates.push_back(parentDir + "/" + baseName + ".dsv");
        }
        parentCandidates.push_back(parentDir + "/saves/" + baseName + ".srm");
        parentCandidates.push_back(parentDir + "/" + baseName + ".srm");

        std::string parentSavesDir = parentDir + "/saves";
        for (const auto& sp : s_discoveredSavePaths) {
            size_t sSlash = sp.find_last_of("/\\");
            std::string spDir = (sSlash != std::string::npos) ? sp.substr(0, sSlash) : "";
            if (pathEqual(spDir, parentDir) || pathEqual(spDir, parentSavesDir)) {
                std::string sFname = (sSlash != std::string::npos) ? sp.substr(sSlash + 1) : sp;
                size_t sDot = sFname.find_last_of('.');
                std::string sBase = (sDot != std::string::npos) ? sFname.substr(0, sDot) : sFname;
                for (char& c : sBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (sBase == lowerBase) {
                    parentCandidates.push_back(sp);
                }
            }
        }

        found = pickNewestExisting(parentCandidates);
        if (!found.empty()) {
            return found;
        }
    }

    // 4. FOURTH PRIORITY: Check emulator-specific standard directories (e.g. mGBA)
    if (console == "GBA") {
        std::vector<std::string> mgbaCandidates;
        std::vector<std::string> mgbaDirs = getMgbaSaveDirectories();
        for (const auto& md : mgbaDirs) {
            mgbaCandidates.push_back(md + baseName + ".sav");
            mgbaCandidates.push_back(md + baseName + ".SAV");
        }
        found = pickNewestExisting(mgbaCandidates);
        if (!found.empty()) {
            return found;
        }
    }

    // 5. LAST RESORT: Search elsewhere across the SD card
    std::vector<std::string> elsewhereCandidates;
    for (const auto& sp : s_discoveredSavePaths) {
        size_t sSlash = sp.find_last_of("/\\");
        std::string sFname = (sSlash != std::string::npos) ? sp.substr(sSlash + 1) : sp;
        size_t sDot = sFname.find_last_of('.');
        std::string sBase = (sDot != std::string::npos) ? sFname.substr(0, sDot) : sFname;
        for (char& c : sBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (sBase == lowerBase) {
            elsewhereCandidates.push_back(sp);
        }
    }

    return pickNewestExisting(elsewhereCandidates);
}

std::string SaveManager::findSaveForInstalledTitle(const InstalledTitle& title) {
    if (!title.savePath.empty()) {
        struct stat st;
        if (stat(title.savePath.c_str(), &st) == 0 && st.st_size > 0) {
            return title.savePath;
        }
    }

    std::string cleanTitle;
    std::vector<std::string> titleTokens;
    {
        std::string currentToken;
        for (char c : title.name) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                cleanTitle += lower;
                currentToken += lower;
            } else {
                if (currentToken.length() >= 3) {
                    if (currentToken != "the" && currentToken != "version" && currentToken != "edition" &&
                        currentToken != "game" && currentToken != "rom" && currentToken != "3ds") {
                        titleTokens.push_back(currentToken);
                    }
                }
                currentToken.clear();
            }
        }
        if (currentToken.length() >= 3 && currentToken != "the" && currentToken != "version" &&
            currentToken != "edition" && currentToken != "game" && currentToken != "rom" && currentToken != "3ds") {
            titleTokens.push_back(currentToken);
        }
    }

    std::string prodCodeClean;
    for (char c : title.productCode) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            prodCodeClean += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    auto isSaveFileName = [](const std::string& name) -> bool {
        if (name == "." || name == ".." || name.empty()) return false;
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) {
            std::string ext = name.substr(dot);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return (ext == ".sav" || ext == ".srm" || ext == ".dsv" || ext == ".dat");
        }
        return (name == "main");
    };

    auto get3dsxTitle = [](const std::string& path) -> std::string {
#if defined(__3DS__) || defined(_3DS)
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return "";
        u32 header[8];
        if (fread(header, sizeof(u32), 8, f) != 8) {
            fclose(f);
            return "";
        }
        if (header[0] != 0x58534433) { // '3DSX'
            fclose(f);
            return "";
        }
        u32 smdhOffset = header[5];
        if (smdhOffset == 0) {
            fclose(f);
            return "";
        }
        if (fseek(f, smdhOffset, SEEK_SET) != 0) {
            fclose(f);
            return "";
        }
        smdh_s smdh;
        if (fread(&smdh, 1, sizeof(smdh_s), f) >= 0x2040 && std::memcmp(smdh.magic, "SMDH", 4) == 0) {
            fclose(f);
            std::string name = extractTitleString(smdh.titles[1]);
            if (name.empty()) name = extractTitleString(smdh.titles[0]);
            return name;
        }
        fclose(f);
#else
        (void)path;
#endif
        return "";
    };

    struct MatchedCandidate {
        std::string savePath;
        int score = 0;
        time_t mtime = 0;
    };
    std::vector<MatchedCandidate> candidates;

    const std::vector<std::string> searchRoots = { "sdmc:/3ds", "/3ds" };
    std::set<std::string> inspectedDirs;

    for (const auto& root : searchRoots) {
        DIR* d = opendir(root.c_str());
        if (!d) continue;

        struct dirent* ent = nullptr;
        while ((ent = readdir(d)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            std::string dirName = ent->d_name;
            std::string fullDir = root + "/" + dirName;

            struct stat st;
            if (stat(fullDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                continue;
            }

            std::string lowerDir = dirName;
            for (char& c : lowerDir) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lowerDir == "romm3ds" || lowerDir == "checkpoint" || lowerDir == "fbi" ||
                lowerDir == "luma" || lowerDir == "anemone" || lowerDir == "godmode9" ||
                lowerDir == "universal-updater" || lowerDir == "payloads") {
                continue;
            }

            if (inspectedDirs.count(lowerDir) > 0) continue;
            inspectedDirs.insert(lowerDir);

            int dirScore = 0;
            std::string cleanDir;
            for (char c : lowerDir) {
                if (std::isalnum(static_cast<unsigned char>(c))) cleanDir += c;
            }

            if (!cleanDir.empty() && !cleanTitle.empty()) {
                if (cleanTitle.find(cleanDir) != std::string::npos || cleanDir.find(cleanTitle) != std::string::npos) {
                    dirScore += 50;
                }
            }

            for (const auto& token : titleTokens) {
                if (cleanDir.find(token) != std::string::npos) {
                    dirScore += 25;
                }
            }

            if (prodCodeClean.length() >= 4 && cleanDir.find(prodCodeClean.substr(prodCodeClean.length() - 4)) != std::string::npos) {
                dirScore += 40;
            }

            DIR* subD = opendir(fullDir.c_str());
            if (!subD) continue;

            struct dirent* subEnt = nullptr;
            std::vector<std::string> dirSaveFiles;
            std::string dirFirst3dsx;

            while ((subEnt = readdir(subD)) != nullptr) {
                if (subEnt->d_name[0] == '.') continue;
                std::string fname = subEnt->d_name;
                std::string subFull = fullDir + "/" + fname;

                struct stat sst;
                if (stat(subFull.c_str(), &sst) != 0 || !S_ISREG(sst.st_mode)) {
                    continue;
                }

                if (fname.length() >= 5 && fname.rfind(".3dsx") == fname.length() - 5) {
                    if (dirFirst3dsx.empty()) dirFirst3dsx = subFull;
                    std::string smdhTitle = get3dsxTitle(subFull);
                    if (!smdhTitle.empty()) {
                        std::string cleanSmdh;
                        for (char c : smdhTitle) {
                            if (std::isalnum(static_cast<unsigned char>(c))) {
                                cleanSmdh += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            }
                        }
                        if (cleanSmdh == cleanTitle || cleanSmdh.find(cleanTitle) != std::string::npos || cleanTitle.find(cleanSmdh) != std::string::npos) {
                            dirScore += 100;
                        }
                    }
                } else if (isSaveFileName(fname)) {
                    dirSaveFiles.push_back(subFull);
                }
            }
            closedir(subD);

            if (!dirSaveFiles.empty() && dirScore > 0) {
                for (const auto& sf : dirSaveFiles) {
                    size_t slash = sf.find_last_of("/\\");
                    std::string sName = (slash != std::string::npos) ? sf.substr(slash + 1) : sf;
                    std::string sLower = sName;
                    for (char& c : sLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    int fileScore = dirScore;
                    for (const auto& token : titleTokens) {
                        if (sLower.find(token) != std::string::npos) {
                            fileScore += 20;
                        }
                    }

                    struct stat sfStat;
                    time_t mtime = 0;
                    if (stat(sf.c_str(), &sfStat) == 0) {
                        mtime = sfStat.st_mtime;
                        if (sfStat.st_size > 0) fileScore += 10;
                    }

                    MatchedCandidate cand;
                    cand.savePath = sf;
                    cand.score = fileScore;
                    cand.mtime = mtime;
                    candidates.push_back(cand);
                }
            } else if (dirSaveFiles.empty() && dirScore >= 50 && !dirFirst3dsx.empty()) {
                size_t slash = dirFirst3dsx.find_last_of("/\\");
                std::string bname = (slash != std::string::npos) ? dirFirst3dsx.substr(slash + 1) : "save";
                size_t dot = bname.find_last_of('.');
                if (dot != std::string::npos) bname = bname.substr(0, dot);

                MatchedCandidate cand;
                cand.savePath = fullDir + "/" + bname + ".sav";
                cand.score = dirScore;
                cand.mtime = 0;
                candidates.push_back(cand);
            }
        }
        closedir(d);
    }

    if (candidates.empty()) {
        const std::vector<std::string> extraDirs = {
            "sdmc:/3ds/saves", "/3ds/saves",
            "sdmc:/mGBA", "/mGBA",
            "sdmc:/gba", "/gba",
            "sdmc:/roms/gba", "/roms/gba",
            "sdmc:/saves", "/saves"
        };
        for (const auto& ed : extraDirs) {
            DIR* edDir = opendir(ed.c_str());
            if (!edDir) continue;
            struct dirent* e = nullptr;
            while ((e = readdir(edDir)) != nullptr) {
                if (e->d_name[0] == '.') continue;
                std::string fname = e->d_name;
                if (!isSaveFileName(fname)) continue;

                std::string fLower = fname;
                for (char& c : fLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                int fScore = 0;
                for (const auto& token : titleTokens) {
                    if (fLower.find(token) != std::string::npos) fScore += 25;
                }
                if (fScore >= 25) {
                    std::string fPath = ed + "/" + fname;
                    struct stat st;
                    time_t mt = (stat(fPath.c_str(), &st) == 0) ? st.st_mtime : 0;
                    candidates.push_back({fPath, fScore, mt});
                }
            }
            closedir(edDir);
        }

        for (const auto& sf : s_discoveredSavePaths) {
            size_t slash = sf.find_last_of("/\\");
            std::string fname = (slash != std::string::npos) ? sf.substr(slash + 1) : sf;
            std::string fLower = fname;
            for (char& c : fLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            int fScore = 0;
            std::string fClean;
            for (char c : fname) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    fClean += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }

            if (!cleanTitle.empty() && fClean.find(cleanTitle) != std::string::npos) {
                fScore += 80;
            }
            for (const auto& token : titleTokens) {
                if (fLower.find(token) != std::string::npos) fScore += 25;
            }
            if (fScore >= 25) {
                struct stat st;
                time_t mt = (stat(sf.c_str(), &st) == 0) ? st.st_mtime : 0;
                candidates.push_back({sf, fScore, mt});
            }
        }
    }

    if (candidates.empty()) {
        return "";
    }

    std::sort(candidates.begin(), candidates.end(), [](const MatchedCandidate& a, const MatchedCandidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.mtime > b.mtime;
    });

    return candidates[0].savePath;
}

namespace {

void scanDirectoryRecursive(
    const std::string& dirPath,
    int depth,
    int maxDepth,
    std::vector<InstalledTitle>& outTitles,
    std::set<std::string>& seenRomPaths)
{
    if (depth > maxDepth) return;

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;

    struct dirent* entry = nullptr;
    std::vector<std::string> subdirs;

    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        if (!name || name[0] == '\0') continue;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
        if (name[0] == '.') continue;

        std::string fullPath = dirPath;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += name;

        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            std::string lowerName = name;
            for (char& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name[0] == '.' ||
                name[0] == '_' ||
                lowerName == "_nds" ||
                lowerName == "_gba" ||
                lowerName == "nintendo 3ds" ||
                lowerName == "dcim" ||
                lowerName == "$recycle.bin" ||
                lowerName == "system volume information" ||
                lowerName == "cheats" ||
                lowerName == "bios" ||
                lowerName == "screenshots" ||
                lowerName == "borders" ||
                lowerName == "patch" ||
                lowerName == "temp") {
                continue;
            }
            subdirs.push_back(fullPath);
        } else if (S_ISREG(st.st_mode) && st.st_size > 0) {
            std::string filename = name;
            size_t dotPos = filename.find_last_of('.');
            if (dotPos == std::string::npos) continue;

            std::string ext = filename.substr(dotPos);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            // Discover and index any save files across the entire SD card
            if (ext == ".sav" || ext == ".srm" || ext == ".dsv" || ext == ".dat") {
                s_discoveredSavePaths.push_back(fullPath);
                continue;
            }

            std::string console;
            if (ext == ".gba" || ext == ".agb") {
                console = "GBA";
            } else if (ext == ".nds" || ext == ".dsi") {
                console = "DS";
            } else if (ext == ".gb") {
                console = "GB";
            } else if (ext == ".gbc") {
                console = "GBC";
            } else if (ext == ".nes") {
                console = "NES";
            } else if (ext == ".sfc" || ext == ".snes") {
                console = "SNES";
            }

            if (console.empty()) continue;

            if (seenRomPaths.count(fullPath) > 0) continue;
            seenRomPaths.insert(fullPath);

            InstalledTitle title;
            title.isFileRom = true;
            title.romPath = fullPath;
            title.console = console;
            title.mediaType = 1; // SD
            title.name = filename.substr(0, dotPos);

            uint64_t hash = SaveManager::hashRomPath(fullPath);
            title.titleId = hash;
            title.titleIdHex = SaveManager::formatTitleId(hash);

            FILE* f = fopen(fullPath.c_str(), "rb");
            if (f) {
                if (console == "GBA") {
                    if (fseek(f, 0xAC, SEEK_SET) == 0) {
                        char code[5] = {0};
                        if (fread(code, 1, 4, f) == 4) {
                            bool valid = true;
                            for (int c = 0; c < 4; ++c) {
                                if (!std::isalnum(static_cast<unsigned char>(code[c]))) {
                                    valid = false;
                                    break;
                                }
                            }
                            if (valid) {
                                title.productCode = std::string("AGB-") + code;
                            }
                        }
                    }
                } else if (console == "DS") {
                    if (fseek(f, 0x0C, SEEK_SET) == 0) {
                        char code[5] = {0};
                        if (fread(code, 1, 4, f) == 4) {
                            bool valid = true;
                            for (int c = 0; c < 4; ++c) {
                                if (!std::isalnum(static_cast<unsigned char>(code[c]))) {
                                    valid = false;
                                    break;
                                }
                            }
                            if (valid) {
                                title.productCode = std::string("NTR-") + code;
                            }
                        }
                    }
                }
                fclose(f);
            }

            outTitles.push_back(title);
        }
    }

    closedir(dir);

    for (const auto& sdir : subdirs) {
        scanDirectoryRecursive(sdir, depth + 1, maxDepth, outTitles, seenRomPaths);
    }
}

static void findRomDirsRecursive(const std::string& currentDir, int depth, int maxDepth, std::vector<std::string>& outDirs) {
    if (depth > maxDepth) return;

    DIR* d = opendir(currentDir.c_str());
    if (!d) return;

    struct dirent* entry = nullptr;
    std::vector<std::string> subdirsToSearch;

    while ((entry = readdir(d)) != nullptr) {
        if (!entry->d_name || entry->d_name[0] == '.' || entry->d_name[0] == '_') continue;

        std::string name = entry->d_name;
        std::string lowerName = name;
        for (char& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // Skip non-game and system folders
        if (lowerName == "_nds" ||
            lowerName == "_gba" ||
            lowerName == "nintendo 3ds" ||
            lowerName == "dcim" ||
            lowerName == "$recycle.bin" ||
            lowerName == "system volume information" ||
            lowerName == "luma" ||
            lowerName == "gm9" ||
            lowerName == "fbi" ||
            lowerName == "themes" ||
            lowerName == "cheats" ||
            lowerName == "bios" ||
            lowerName == "screenshots" ||
            lowerName == "borders" ||
            lowerName == "patch" ||
            lowerName == "temp") {
            continue;
        }

        std::string fullPath = currentDir;
        if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
        fullPath += name;

        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }

        // If directory is named "roms", "rom", or common emulator dir "mgba", "gba", "nds"
        if (lowerName == "roms" || lowerName == "rom" ||
            lowerName == "mgba" || lowerName == "gba" || lowerName == "nds") {
            outDirs.push_back(fullPath);
            // We found a ROM root directory. scanDirectoryRecursive will scan this
            // directory and all of its subdirectories (e.g. /roms/gba, /pokedaisy/roms/...)
            continue;
        }

        // For the "3ds" folder, do not recurse into all homebrew apps,
        // but check if a "3ds/roms" directory exists
        if (lowerName == "3ds") {
            std::string subRoms = fullPath + "/roms";
            struct stat subSt;
            if (stat(subRoms.c_str(), &subSt) == 0 && S_ISDIR(subSt.st_mode)) {
                outDirs.push_back(subRoms);
            }
            continue;
        }

        subdirsToSearch.push_back(fullPath);
    }
    closedir(d);

    for (const auto& sdir : subdirsToSearch) {
        findRomDirsRecursive(sdir, depth + 1, maxDepth, outDirs);
    }
}

} // namespace

std::vector<InstalledTitle> SaveManager::scanRomFiles(const std::vector<std::string>& customRomDirs) {
    std::vector<InstalledTitle> titles;
    std::set<std::string> seenRomPaths;
    s_discoveredSavePaths.clear();

    std::vector<std::string> romDirsToScan;

    // 1. User-configured custom ROM directories
    for (const auto& d : customRomDirs) {
        if (!d.empty() && std::find(romDirsToScan.begin(), romDirsToScan.end(), d) == romDirsToScan.end()) {
            romDirsToScan.push_back(d);
        }
    }

    // 2. Discover ROM directories on SD card (e.g. /roms, /pokedaisy/roms, etc.)
    DIR* dSd = opendir("sdmc:/");
    if (dSd) {
        closedir(dSd);
        findRomDirsRecursive("sdmc:/", 0, 3, romDirsToScan);
    } else {
        // Fallback for non-3DS testing environment: check local directory for "roms"
        DIR* dLocal = opendir("./roms");
        if (dLocal) {
            closedir(dLocal);
            romDirsToScan.push_back("./roms");
        }
    }

    // Also include common default paths if they exist
    const std::vector<std::string> commonDefaults = {
        "sdmc:/roms",
        "sdmc:/mGBA",
        "sdmc:/gba",
        "sdmc:/nds"
    };
    for (const auto& def : commonDefaults) {
        DIR* d = opendir(def.c_str());
        if (d) {
            closedir(d);
            if (std::find(romDirsToScan.begin(), romDirsToScan.end(), def) == romDirsToScan.end()) {
                romDirsToScan.push_back(def);
            }
        }
    }

    // De-duplicate discovered ROM directories
    std::vector<std::string> uniqueRomDirs;
    for (const auto& dir : romDirsToScan) {
        if (std::find(uniqueRomDirs.begin(), uniqueRomDirs.end(), dir) == uniqueRomDirs.end()) {
            uniqueRomDirs.push_back(dir);
        }
    }

    // 3. Scan each discovered ROM directory and all of its subdirectories (e.g. /roms, /roms/gba, /pokedaisy/roms, etc.)
    for (const auto& rdir : uniqueRomDirs) {
        scanDirectoryRecursive(rdir, 0, 8, titles, seenRomPaths);

        // Check if sibling saves directory exists (e.g. /pokedaisy/saves for /pokedaisy/roms)
        size_t slash = rdir.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string parentDir = rdir.substr(0, slash);
            std::string siblingSaves = parentDir + "/saves";
            DIR* ds = opendir(siblingSaves.c_str());
            if (ds) {
                struct dirent* se = nullptr;
                while ((se = readdir(ds)) != nullptr) {
                    if (!se->d_name || se->d_name[0] == '.') continue;
                    std::string sfn = se->d_name;
                    size_t sdot = sfn.find_last_of('.');
                    if (sdot != std::string::npos) {
                        std::string sext = sfn.substr(sdot);
                        for (char& c : sext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (sext == ".sav" || sext == ".srm" || sext == ".dsv" || sext == ".dat") {
                            s_discoveredSavePaths.push_back(siblingSaves + "/" + sfn);
                        }
                    }
                }
                closedir(ds);
            }
        }
    }

    // 4. Resolve save files for all discovered ROM titles
    for (auto& title : titles) {
        std::string foundSave = findSaveFileForRom(title.romPath, title.console);
        if (!foundSave.empty()) {
            title.savePath = foundSave;
            title.hasSave = true;
            struct stat st;
            if (stat(foundSave.c_str(), &st) == 0 && st.st_size > 0 && st.st_mtime > 0) {
                title.saveTime = st.st_mtime;
                setCachedSaveTime(title.titleIdHex, st.st_mtime);
            } else {
                title.saveTime = getCachedSaveTime(title.titleIdHex);
            }
        } else {
            title.hasSave = false;
            size_t slash = title.romPath.find_last_of("/\\");
            std::string baseDir = (slash != std::string::npos) ? title.romPath.substr(0, slash) : ".";
            if (title.console == "DS") {
                struct stat stSaves;
                if (stat((baseDir + "/saves").c_str(), &stSaves) == 0 && S_ISDIR(stSaves.st_mode)) {
                    title.savePath = baseDir + "/saves/" + title.name + ".sav";
                } else {
                    title.savePath = baseDir + "/" + title.name + ".sav";
                }
            } else {
                title.savePath = baseDir + "/" + title.name + ".sav";
            }
        }
    }

    return titles;
}

std::vector<InstalledTitle> SaveManager::scanInstalledTitles(const std::vector<std::string>& customRomDirs) {
    // Scan ROM files and discover all saves across ROM directories first
    std::vector<InstalledTitle> romTitles = scanRomFiles(customRomDirs);
    std::vector<InstalledTitle> outTitles;

#if defined(__3DS__) || defined(_3DS)
    FS_MediaType mediaTypes[] = { MEDIATYPE_SD, MEDIATYPE_GAME_CARD, MEDIATYPE_NAND };
    std::set<u64> seenTitleIds;

    for (auto mediaType : mediaTypes) {
        u32 count = 0;
        if (R_FAILED(AM_GetTitleCount(mediaType, &count)) || count == 0) {
            continue;
        }

        std::vector<u64> ids(count);
        u32 read = 0;
        if (R_FAILED(AM_GetTitleList(&read, mediaType, count, ids.data()))) {
            continue;
        }

        for (u32 i = 0; i < read; ++i) {
            u64 tid = ids[i];
            if (seenTitleIds.count(tid) > 0) continue;

            u32 tidHigh = static_cast<u32>((tid >> 32) & 0xFFFFFFFF);

            // Filter to installed 3DS games, CIAs, demos, and DSiWare
            if (tidHigh != 0x00040000 && tidHigh != 0x00040002 && tidHigh != 0x00048004) {
                continue;
            }

            // Exclude romm3ds itself
            if (tid == 0x0004000000F17E00ULL) {
                continue;
            }

            seenTitleIds.insert(tid);

            InstalledTitle title;
            title.titleId = tid;
            title.titleIdHex = formatTitleId(tid);
            title.mediaType = mediaType;
            title.name = getTitleNameFromSmdh(tid, mediaType);

            char prodCode[16] = {0};
            if (R_SUCCEEDED(AM_GetTitleProductCode(mediaType, tid, prodCode)) && prodCode[0] != '\0') {
                title.productCode = prodCode;
            }

            title.console = determineConsole(title.productCode, tid);

            if (title.name.empty()) {
                title.name = !title.productCode.empty() ? title.productCode : ("Title " + title.titleIdHex);
            }

            title.hasSave = checkSaveExists(tid, mediaType);
            if (title.hasSave) {
                title.saveTime = getSaveTime(title);
                if (title.saveTime == 0) {
                    title.saveTime = getCachedSaveTime(title.titleIdHex);
                }
            } else {
                std::string sdSave = findSaveForInstalledTitle(title);
                if (!sdSave.empty()) {
                    title.savePath = sdSave;
                    struct stat st;
                    if (stat(sdSave.c_str(), &st) == 0 && st.st_size > 0) {
                        title.hasSave = true;
                        if (st.st_mtime > 0) {
                            title.saveTime = st.st_mtime;
                            setCachedSaveTime(title.titleIdHex, st.st_mtime);
                        } else {
                            title.saveTime = getCachedSaveTime(title.titleIdHex);
                        }
                    }
                }
            }

            if (title.console == "3DS") {
                std::string checkStr = title.savePath + " " + title.romPath;
                for (char& c : checkStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (checkStr.find("mgba") != std::string::npos || checkStr.find(".gba") != std::string::npos) {
                    title.console = "GBA";
                } else if (!title.savePath.empty()) {
                    size_t slash = title.savePath.find_last_of("/\\");
                    if (slash != std::string::npos) {
                        std::string dir = title.savePath.substr(0, slash);
                        DIR* d = opendir(dir.c_str());
                        if (d) {
                            struct dirent* e = nullptr;
                            while ((e = readdir(d)) != nullptr) {
                                if (e->d_name[0] == '.') continue;
                                std::string fn = e->d_name;
                                if (fn.size() >= 4) {
                                    std::string ext = fn.substr(fn.size() - 4);
                                    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                    if (ext == ".gba") {
                                        title.console = "GBA";
                                        break;
                                    }
                                }
                            }
                            closedir(d);
                        }
                    }
                }
            }
            outTitles.push_back(title);
        }
    }
#endif

    outTitles.insert(outTitles.end(), romTitles.begin(), romTitles.end());
    saveTimestampCache();
    return outTitles;
}

#if defined(__3DS__) || defined(_3DS)
static Result openSaveArchive(FS_Archive* archive, uint64_t titleId, uint8_t mediaType) {
    if (mediaType == MEDIATYPE_NAND) {
        u32 pathData[2] = {
            static_cast<u32>(mediaType),
            static_cast<u32>(0x00020000 | (static_cast<u32>(titleId) >> 8))
        };
        FS_Path path = { PATH_BINARY, 8, pathData };
        return FSUSER_OpenArchive(archive, ARCHIVE_SYSTEM_SAVEDATA, path);
    } else {
        u32 pathData[3] = {
            static_cast<u32>(mediaType),
            static_cast<u32>(titleId & 0xFFFFFFFF),
            static_cast<u32>((titleId >> 32) & 0xFFFFFFFF)
        };
        FS_Path path = { PATH_BINARY, 12, pathData };
        return FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, path);
    }
}
#endif

bool SaveManager::checkSaveExists(uint64_t titleId, uint8_t mediaType) {
#if defined(__3DS__) || defined(_3DS)
    FS_Archive archive = 0;
    Result res = openSaveArchive(&archive, titleId, mediaType);
    if (R_SUCCEEDED(res)) {
        FSUSER_CloseArchive(archive);
        return true;
    }
    return false;
#else
    (void)titleId;
    (void)mediaType;
    return false;
#endif
}

bool SaveManager::checkSaveExists(const InstalledTitle& title) {
    if (!title.isFileRom) {
        if (checkSaveExists(title.titleId, title.mediaType)) {
            return true;
        }
        if (!title.savePath.empty()) {
            struct stat st;
            return (stat(title.savePath.c_str(), &st) == 0 && st.st_size > 0);
        }
        std::string sdSave = findSaveForInstalledTitle(title);
        if (!sdSave.empty()) {
            struct stat st;
            return (stat(sdSave.c_str(), &st) == 0 && st.st_size > 0);
        }
        return false;
    }

    if (!title.savePath.empty()) {
        struct stat st;
        if (stat(title.savePath.c_str(), &st) == 0 && st.st_size > 0) {
            return true;
        }
    }

    std::string found = findSaveFileForRom(title.romPath, title.console);
    return !found.empty();
}

time_t SaveManager::getSaveTime(const InstalledTitle& title) {
    std::string savePath = title.savePath;
    if (savePath.empty()) {
        if (title.isFileRom) {
            savePath = findSaveFileForRom(title.romPath, title.console);
        } else if (!checkSaveExists(title.titleId, title.mediaType)) {
            savePath = findSaveForInstalledTitle(title);
        }
    }
    if (!savePath.empty()) {
        struct stat st;
        if (stat(savePath.c_str(), &st) == 0 && st.st_size > 0) {
            if (st.st_mtime > 0) {
                setCachedSaveTime(title.titleIdHex, st.st_mtime);
                return st.st_mtime;
            }
            if (st.st_ctime > 0) {
                setCachedSaveTime(title.titleIdHex, st.st_ctime);
                return st.st_ctime;
            }
        }
    }

    if (title.isFileRom) {
        return getCachedSaveTime(title.titleIdHex);
    }

    std::string stagedDir = "sdmc:/3ds/romm3ds/saves/" + formatTitleId(title.titleId);
    DIR* d = opendir(stagedDir.c_str());
    if (d) {
        struct dirent* ent = nullptr;
        time_t newest = 0;
        while ((ent = readdir(d)) != nullptr) {
            if (std::strcmp(ent->d_name, ".") != 0 && std::strcmp(ent->d_name, "..") != 0 && ent->d_name[0] != '.') {
                std::string full = stagedDir + "/" + ent->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && st.st_mtime > newest) {
                    newest = st.st_mtime;
                }
            }
        }
        closedir(d);
        if (newest > 0) {
            setCachedSaveTime(title.titleIdHex, newest);
            return newest;
        }
    }

    char highHex[16];
    char lowHex[16];
    snprintf(highHex, sizeof(highHex), "%08lx", (unsigned long)((title.titleId >> 32) & 0xFFFFFFFF));
    snprintf(lowHex, sizeof(lowHex), "%08lx", (unsigned long)(title.titleId & 0xFFFFFFFF));

    DIR* root = opendir("sdmc:/Nintendo 3DS");
    if (root) {
        struct dirent* e0 = nullptr;
        while ((e0 = readdir(root)) != nullptr) {
            if (e0->d_name[0] == '.') continue;
            std::string p0 = std::string("sdmc:/Nintendo 3DS/") + e0->d_name;
            DIR* d0 = opendir(p0.c_str());
            if (!d0) continue;
            struct dirent* e1 = nullptr;
            while ((e1 = readdir(d0)) != nullptr) {
                if (e1->d_name[0] == '.') continue;
                std::string dataDir = p0 + "/" + e1->d_name + "/title/" + highHex + "/" + lowHex + "/data";
                DIR* dd = opendir(dataDir.c_str());
                if (dd) {
                    struct dirent* de = nullptr;
                    time_t newestData = 0;
                    while ((de = readdir(dd)) != nullptr) {
                        if (de->d_name[0] == '.') continue;
                        std::string df = dataDir + "/" + de->d_name;
                        struct stat dst;
                        if (stat(df.c_str(), &dst) == 0 && dst.st_mtime > newestData) {
                            newestData = dst.st_mtime;
                        }
                    }
                    closedir(dd);
                    if (newestData > 0) {
                        closedir(d0);
                        closedir(root);
                        setCachedSaveTime(title.titleIdHex, newestData);
                        return newestData;
                    }
                }
            }
            closedir(d0);
        }
        closedir(root);
    }

    return getCachedSaveTime(title.titleIdHex);
}

static std::string detectGbaSaveType(const std::string& romPath) {
    FILE* f = fopen(romPath.c_str(), "rb");
    if (!f) return "";
    std::vector<char> buf(512 * 1024);
    std::string prevTail;
    size_t bytesRead = 0;
    while ((bytesRead = fread(buf.data(), 1, buf.size(), f)) > 0) {
        std::string chunk = prevTail + std::string(buf.data(), bytesRead);
        if (chunk.find("FLASH1M_V") != std::string::npos) { fclose(f); return "FLASH1M"; }
        if (chunk.find("FLASH512_V") != std::string::npos || chunk.find("FLASH_V") != std::string::npos) { fclose(f); return "FLASH512"; }
        if (chunk.find("SRAM_V") != std::string::npos || chunk.find("SRAM_F_V") != std::string::npos) { fclose(f); return "SRAM"; }
        if (chunk.find("EEPROM_V") != std::string::npos) { fclose(f); return "EEPROM"; }
        if (chunk.length() > 64) {
            prevTail = chunk.substr(chunk.length() - 64);
        } else {
            prevTail = chunk;
        }
    }
    fclose(f);
    return "";
}

bool SaveManager::backupSave(uint64_t titleId, const std::string& outDirectory, uint8_t mediaType, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
#if defined(__3DS__) || defined(_3DS)
    FS_Archive archive = 0;
    Result res = openSaveArchive(&archive, titleId, mediaType);
    if (R_FAILED(res)) {
        emit("[ERROR] Failed to open 3DS save archive: " + formatResultHex(res));
        return false;
    }

    emit("Backing up 3DS save archive to: " + outDirectory);
    bool success = copyArchiveDirToFs(archive, "/", outDirectory);
    FSUSER_CloseArchive(archive);
    emit(success ? "[OK] Archive backup completed." : "[ERROR] Failed copying archive files.");
    return success;
#else
    (void)titleId;
    (void)outDirectory;
    (void)mediaType;
    emit("[ERROR] 3DS archive backup not supported on this platform.");
    return false;
#endif
}

bool SaveManager::backupSave(const InstalledTitle& title, const std::string& outDirectory, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
    if (!title.isFileRom) {
        if (title.savePath.empty() && checkSaveExists(title.titleId, title.mediaType)) {
            return backupSave(title.titleId, outDirectory, title.mediaType, log);
        }
    }

    std::string saveFile = title.savePath;
    struct stat sst;
    if (saveFile.empty() || stat(saveFile.c_str(), &sst) != 0 || sst.st_size == 0) {
        if (title.isFileRom) {
            saveFile = findSaveFileForRom(title.romPath, title.console);
        } else {
            saveFile = findSaveForInstalledTitle(title);
        }
    }

    if (saveFile.empty()) {
        if (!title.isFileRom) {
            return backupSave(title.titleId, outDirectory, title.mediaType, log);
        }
        emit("[ERROR] No local save file found for " + title.name);
        return false;
    }

    size_t slashPos = saveFile.find_last_of("/\\");
    std::string fileName = (slashPos != std::string::npos) ? saveFile.substr(slashPos + 1) : saveFile;
    std::string destPath = outDirectory + "/" + fileName;

    ConfigManager::ensureDirectories(destPath);
    emit("Copying save file: " + saveFile + " -> " + destPath);

    FILE* src = fopen(saveFile.c_str(), "rb");
    if (!src) {
        emit("[ERROR] Could not open source save: " + saveFile);
        return false;
    }

    FILE* dst = fopen(destPath.c_str(), "wb");
    if (!dst) {
        fclose(src);
        emit("[ERROR] Could not open destination for write: " + destPath);
        return false;
    }

    std::vector<uint8_t> buffer(64 * 1024);
    size_t bytesRead = 0;
    bool ok = true;
    while ((bytesRead = fread(buffer.data(), 1, buffer.size(), src)) > 0) {
        if (fwrite(buffer.data(), 1, bytesRead, dst) != bytesRead) {
            ok = false;
            break;
        }
    }

    fclose(src);
    fclose(dst);
    emit(ok ? "[OK] Local save copied to staging." : "[ERROR] Write error during save copy.");
    return ok;
}

bool SaveManager::injectSave(uint64_t titleId, const std::string& inDirectory, uint8_t mediaType, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
#if defined(__3DS__) || defined(_3DS)
    std::string mediaTypeName = (mediaType == MEDIATYPE_NAND) ? "NAND" :
                                (mediaType == MEDIATYPE_SD) ? "SD" :
                                (mediaType == MEDIATYPE_GAME_CARD) ? "Game Card" : ("Type " + std::to_string(mediaType));
    emit("Target Title ID: 0x" + formatTitleId(titleId) + " (" + mediaTypeName + ")");
    emit("Staging directory: " + inDirectory);

    // List and inspect files in inDirectory
    DIR* testDir = opendir(inDirectory.c_str());
    if (testDir) {
        struct dirent* de = nullptr;
        int fileCount = 0;
        while ((de = readdir(testDir)) != nullptr) {
            if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
            if (de->d_name[0] == '.') continue;
            std::string fullPath = inDirectory + "/" + de->d_name;
            struct stat fst;
            if (stat(fullPath.c_str(), &fst) == 0) {
                fileCount++;
                emit("  Staged file: " + std::string(de->d_name) + " (" + std::to_string(fst.st_size) + " B)");

                // Check for zip magic header (PK\x03\x04)
                if (fst.st_size >= 4) {
                    FILE* tf = fopen(fullPath.c_str(), "rb");
                    if (tf) {
                        uint8_t magic[4] = {0};
                        if (fread(magic, 1, 4, tf) == 4) {
                            if (magic[0] == 0x50 && magic[1] == 0x4B && magic[2] == 0x03 && magic[3] == 0x04) {
                                emit("[WARN] '" + std::string(de->d_name) + "' is a ZIP archive! Raw 3DS save data expected.");
                            }
                        }
                        fclose(tf);
                    }
                }
            }
        }
        closedir(testDir);
        if (fileCount == 0) {
            emit("[WARN] No files found in staging directory: " + inDirectory);
        }
    } else {
        emit("[ERROR] Could not open staging directory: " + inDirectory);
    }

    FS_Archive archive = 0;
    Result res = openSaveArchive(&archive, titleId, mediaType);
    if (R_FAILED(res)) {
        emit("[ERROR] Failed to open 3DS save archive for injection: " + formatResultHex(res));
        return false;
    }

    // Inspect existing files in archive for debugging comparison
    auto existingFiles = listArchiveFiles(archive, "/");
    if (!existingFiles.empty()) {
        std::string existingList;
        for (size_t i = 0; i < existingFiles.size(); ++i) {
            if (i > 0) existingList += ", ";
            existingList += existingFiles[i].first + " (" + std::to_string(existingFiles[i].second) + " B)";
            if (i >= 5 && existingFiles.size() > 6) {
                existingList += " ... (" + std::to_string(existingFiles.size() - 6) + " more)";
                break;
            }
        }
        emit("Existing archive files: " + existingList);
    } else {
        emit("Existing archive files: (empty or could not enumerate)");
    }

    emit("Writing save files into 3DS archive...");
    bool success = copyFsDirToArchive(archive, inDirectory, "/", log);
    if (success) {
        Result resCommit = FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0);
        if (R_SUCCEEDED(resCommit)) {
            emit("[OK] Committed save data to 3DS archive.");
        } else {
            emit("[ERROR] Failed committing save data to 3DS archive: " + formatResultHex(resCommit));
            success = false;
        }
    } else {
        emit("[ERROR] Failed writing save files to 3DS archive.");
    }

    FSUSER_CloseArchive(archive);
    return success;
#else
    (void)titleId;
    (void)inDirectory;
    (void)mediaType;
    emit("[ERROR] 3DS archive injection not supported on this platform.");
    return false;
#endif
}

bool SaveManager::injectSave(const InstalledTitle& title, const std::string& inDirectory, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
    if (!title.isFileRom) {
        if (title.savePath.empty() && checkSaveExists(title.titleId, title.mediaType)) {
            return injectSave(title.titleId, inDirectory, title.mediaType, log);
        }
    }

    std::string srcFile;
    struct stat inSt;
    if (stat(inDirectory.c_str(), &inSt) == 0 && !S_ISDIR(inSt.st_mode)) {
        srcFile = inDirectory;
    } else {
        DIR* d = opendir(inDirectory.c_str());
        if (d) {
            struct dirent* ent = nullptr;
            time_t newestTime = 0;
            while ((ent = readdir(d)) != nullptr) {
                if (std::strcmp(ent->d_name, ".") != 0 && std::strcmp(ent->d_name, "..") != 0 && ent->d_name[0] != '.') {
                    std::string en = ent->d_name;
                    std::string full = inDirectory + "/" + en;
                    if (en.length() >= 4 && (en.rfind(".sav") == en.length() - 4 ||
                                             en.rfind(".SAV") == en.length() - 4 ||
                                             en.rfind(".dsv") == en.length() - 4 ||
                                             en.rfind(".srm") == en.length() - 4 ||
                                             en.rfind(".SRM") == en.length() - 4)) {
                        struct stat st;
                        if (stat(full.c_str(), &st) == 0) {
                            if (srcFile.empty() || st.st_mtime > newestTime) {
                                srcFile = full;
                                newestTime = st.st_mtime;
                            }
                        }
                    }
                }
            }
            closedir(d);
        }
    }

    if (srcFile.empty()) {
        emit("[ERROR] No downloaded save file found in staging: " + inDirectory);
        return false;
    }

    emit("Source save file: " + srcFile);

    std::string targetSavePath = title.savePath;
    if (targetSavePath.empty()) {
        if (!title.isFileRom) {
            targetSavePath = findSaveForInstalledTitle(title);
        }
    }
    std::string dir = ".";
    std::string baseName = title.name;
    if (!title.romPath.empty()) {
        size_t slashPos = title.romPath.find_last_of("/\\");
        dir = (slashPos != std::string::npos) ? title.romPath.substr(0, slashPos) : ".";
        std::string filename = (slashPos != std::string::npos) ? title.romPath.substr(slashPos + 1) : title.romPath;
        size_t dotPos = filename.find_last_of('.');
        baseName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
    }

    if (targetSavePath.empty() && title.isFileRom) {
        if (title.console == "DS") {
            targetSavePath = dir + "/saves/" + baseName + ".sav";
        } else {
            targetSavePath = dir + "/" + baseName + ".sav";
        }
    }

    if (!targetSavePath.empty() && (dir == "." || title.romPath.empty())) {
        size_t slashPos = targetSavePath.find_last_of("/\\");
        dir = (slashPos != std::string::npos) ? targetSavePath.substr(0, slashPos) : ".";
        std::string filename = (slashPos != std::string::npos) ? targetSavePath.substr(slashPos + 1) : targetSavePath;
        size_t dotPos = filename.find_last_of('.');
        baseName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
    }

    if (targetSavePath.empty()) {
        if (!title.isFileRom) {
            return injectSave(title.titleId, inDirectory, title.mediaType, log);
        }
        emit("[ERROR] No destination save path known for " + title.name);
        return false;
    }

    // TWiLight Menu / nds-bootstrap and mGBA only read .sav files.
    // If targetSavePath ends with .srm or .dsv, normalize extension to .sav.
    if (title.console == "DS" || title.console == "GBA") {
        size_t dot = targetSavePath.find_last_of('.');
        if (dot != std::string::npos) {
            std::string ext = targetSavePath.substr(dot);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".srm" || ext == ".dsv") {
                targetSavePath = targetSavePath.substr(0, dot) + ".sav";
            }
        }
    }

    FILE* src = fopen(srcFile.c_str(), "rb");
    if (!src) {
        emit("[ERROR] Could not open source file: " + srcFile);
        return false;
    }

    fseek(src, 0, SEEK_END);
    long srcSize = ftell(src);
    fseek(src, 0, SEEK_SET);

    size_t readOffset = 0;
    size_t writeLimit = (srcSize > 0) ? static_cast<size_t>(srcSize) : 0;
    bool isEeprom = false;

    if (title.console == "GBA" && srcSize > 0) {
        std::string gbaType = detectGbaSaveType(title.romPath);
        if (!gbaType.empty()) {
            emit("GBA hardware save type in ROM: " + gbaType);
        }

        size_t expectedSize = 0;
        if (gbaType == "FLASH1M") expectedSize = 131072;
        else if (gbaType == "FLASH512") expectedSize = 65536;
        else if (gbaType == "SRAM") expectedSize = 32768;
        else if (gbaType == "EEPROM") expectedSize = 8192;

        if (expectedSize == 0 && !title.savePath.empty()) {
            struct stat est;
            if (stat(title.savePath.c_str(), &est) == 0 && est.st_size > 0) {
                expectedSize = static_cast<size_t>(est.st_size);
                emit("Existing local save size: " + std::to_string(expectedSize) + " B");
            }
        }

        if (srcSize == 139264) { // 0x22000 (Libretro VBA-M / VBA-Next .srm)
            emit("Detected Libretro 136KB (.srm) container.");
            if (expectedSize == 131072) {
                readOffset = 0;
                writeLimit = 131072;
                emit("Extracted 128KB Flash payload from offset 0.");
            } else if (expectedSize == 65536) {
                readOffset = 0;
                writeLimit = 65536;
                emit("Extracted 64KB Flash payload from offset 0.");
            } else if (expectedSize == 32768) {
                readOffset = 0;
                writeLimit = 32768;
                emit("Extracted 32KB SRAM payload from offset 0.");
            } else if (expectedSize == 8192 || expectedSize == 512 || gbaType == "EEPROM") {
                readOffset = 0x20000;
                writeLimit = (expectedSize > 0) ? expectedSize : 8192;
                isEeprom = true;
                emit("Extracted EEPROM payload (" + std::to_string(writeLimit) + " B) from offset 0x20000.");
            } else {
                std::vector<uint8_t> probe(139264);
                if (fread(probe.data(), 1, 139264, src) == 139264) {
                    auto scan_section = [&](size_t offset, size_t len) {
                        for (size_t i = 0; i < len; ++i) {
                            if (probe[offset + i] != 0xFF) return true;
                        }
                        return false;
                    };
                    if (scan_section(0, 0x8000) && !scan_section(0x8000, 0x1A000)) {
                        readOffset = 0;
                        writeLimit = 0x8000;
                    } else if (scan_section(0, 0x10000) && !scan_section(0x10000, 0x10000)) {
                        readOffset = 0;
                        writeLimit = 0x10000;
                    } else if (scan_section(0, 0x20000)) {
                        readOffset = 0;
                        writeLimit = 0x20000;
                    } else if (scan_section(0x20000, 512) && !scan_section(0x20000 + 512, 0x2000 - 512)) {
                        readOffset = 0x20000;
                        writeLimit = 512;
                        isEeprom = true;
                    } else if (scan_section(0x20000, 0x2000)) {
                        readOffset = 0x20000;
                        writeLimit = 0x2000;
                        isEeprom = true;
                    } else {
                        readOffset = 0;
                        writeLimit = 131072;
                    }
                }
                fseek(src, 0, SEEK_SET);
                emit("Heuristic extracted " + std::to_string(writeLimit) + " B payload from offset " + std::to_string(readOffset) + ".");
            }
        } else if (srcSize > 131072) {
            readOffset = 0;
            writeLimit = 131072;
            emit("Trimmed " + std::to_string(srcSize) + " B -> 131072 B (128KB Flash).");
        } else if (expectedSize > 0 && (size_t)srcSize > expectedSize) {
            readOffset = 0;
            writeLimit = expectedSize;
            emit("Trimmed " + std::to_string(srcSize) + " B -> expected " + std::to_string(expectedSize) + " B.");
        } else {
            readOffset = 0;
            writeLimit = static_cast<size_t>(srcSize);
            emit("Using raw save payload (" + std::to_string(writeLimit) + " B).");
        }
    }

    std::vector<uint8_t> payload(writeLimit);
    if (fseek(src, readOffset, SEEK_SET) != 0) {
        fclose(src);
        emit("[ERROR] Seek failed in source save.");
        return false;
    }
    size_t bytesRead = fread(payload.data(), 1, writeLimit, src);
    fclose(src);
    if (bytesRead != writeLimit) {
        emit("[ERROR] Incomplete read: " + std::to_string(bytesRead) + "/" + std::to_string(writeLimit));
        return false;
    }

    if (isEeprom) {
        emit("Byte-swapping 64-bit EEPROM chunks for mGBA...");
        for (size_t i = 0; i + 8 <= payload.size(); i += 8) {
            for (int j = 0; j < 4; ++j) {
                std::swap(payload[i + j], payload[i + 7 - j]);
            }
        }
    }

    auto writePayloadToFile = [&](const std::string& path) -> bool {
        ConfigManager::ensureDirectories(path);
        FILE* out = fopen(path.c_str(), "wb");
        if (!out) {
            emit("[ERROR] Could not open destination: " + path);
            return false;
        }
        bool written = (fwrite(payload.data(), 1, payload.size(), out) == payload.size());
        fclose(out);
        if (written) {
            emit("[OK] Wrote " + path + " (" + std::to_string(payload.size()) + " B)");
        } else {
            emit("[ERROR] Failed writing to " + path);
        }
        return written;
    };

    bool ok = writePayloadToFile(targetSavePath);

    if (ok && title.console == "GBA") {
        std::set<std::string> destinations;
        destinations.insert(targetSavePath);

        // 1. Primary mGBA location: always write directly alongside ROM
        std::string romDirSave = dir + "/" + baseName + ".sav";
        if (destinations.find(romDirSave) == destinations.end()) {
            writePayloadToFile(romDirSave);
            destinations.insert(romDirSave);
        }

        // 2. If dir/saves/ directory exists, also write there for TWiLight Menu
        struct stat sdir;
        std::string savesSubDir = dir + "/saves";
        if (stat(savesSubDir.c_str(), &sdir) == 0 && S_ISDIR(sdir.st_mode)) {
            std::string subSave = savesSubDir + "/" + baseName + ".sav";
            if (destinations.find(subSave) == destinations.end()) {
                writePayloadToFile(subSave);
                destinations.insert(subSave);
            }
        }

        // 3. Write to any existing mGBA save directories (e.g. sdmc:/3ds/mgba/saves/)
        std::vector<std::string> mgbaDirs = getMgbaSaveDirectories();
        for (const auto& md : mgbaDirs) {
            struct stat mdst;
            std::string cand = md + baseName + ".sav";
            if ((stat(md.c_str(), &mdst) == 0 && S_ISDIR(mdst.st_mode)) || stat(cand.c_str(), &mdst) == 0) {
                if (destinations.find(cand) == destinations.end()) {
                    writePayloadToFile(cand);
                    destinations.insert(cand);
                }
            }
        }

        // 4. Overwrite any stale .srm files on the SD card so they cannot shadow .sav
        std::vector<std::string> staleSrms = { dir + "/" + baseName + ".srm", dir + "/saves/" + baseName + ".srm" };
        for (const auto& srmp : staleSrms) {
            struct stat sst;
            if (stat(srmp.c_str(), &sst) == 0) {
                emit("Updating local .srm with new save: " + srmp);
                writePayloadToFile(srmp);
            }
        }

        // 5. In case mGBA has an old savestate (.ss0 / .state) that autoloads, backup stale states
        std::vector<std::string> stateExts = {".ss0", ".ss1", ".ss2", ".state"};
        std::vector<std::string> stateDirs = {dir};
        for (const auto& md : mgbaDirs) {
            stateDirs.push_back(md);
            stateDirs.push_back(md + "states/");
        }
        for (const auto& sdirPath : stateDirs) {
            for (const auto& sext : stateExts) {
                std::string sPath = sdirPath;
                if (!sPath.empty() && sPath.back() != '/') sPath += '/';
                std::string stateFile = sPath + baseName + sext;
                struct stat sst;
                if (stat(stateFile.c_str(), &sst) == 0) {
                    std::string bakFile = stateFile + ".bak";
                    remove(bakFile.c_str());
                    if (rename(stateFile.c_str(), bakFile.c_str()) == 0) {
                        emit("Backed up savestate: " + stateFile + " -> .bak");
                    }
                }
            }
        }
    }

    return ok;
}

} // namespace RomM3DS
