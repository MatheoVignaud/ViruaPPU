#pragma once
// ============================================================================
// Mode 1 — GBA Mode 0 : 4 Text BGs + OBJ
//
// Reads directly from the emulated GBA memory arrays:
//   - gIoMem[]   (I/O registers: DISPCNT, BGxCNT, BGxHOFS/VOFS, blend, window …)
//   - gVram[]    (96 KB tile data + tilemaps)
//   - gBgPltt[]  (256 BG palette entries, RGB555)
//   - gObjPltt[] (256 OBJ palette entries, RGB555)
//   - gOamMem[]  (128 OAM entries)
// ============================================================================
#include <PPUMemory.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

// GBA memory externs (defined in port_gba_mem.c)
extern "C" {
extern uint8_t gIoMem[];
extern uint8_t gVram[];
extern uint16_t gBgPltt[];
extern uint16_t gObjPltt[];
extern uint16_t gOamMem[];
}

namespace Mode1 {

// ---- GBA constants --------------------------------------------------------
constexpr int GBA_WIDTH = 240;
constexpr int GBA_HEIGHT = 160;
constexpr int BG_COUNT = 4;
constexpr int OAM_COUNT = 128;

// ---- tiny helpers to read from gIoMem as host-endian LE -------------------
static inline uint16_t IoRead16(uint16_t off) {
    return uint16_t(gIoMem[off]) | (uint16_t(gIoMem[off + 1]) << 8);
}
static inline uint32_t IoRead32(uint16_t off) {
    return uint32_t(IoRead16(off)) | (uint32_t(IoRead16(off + 2)) << 16);
}

// ---- RGB555 → ABGR8888 ---------------------------------------------------
static inline uint32_t Rgb555ToAbgr8888(uint16_t c) {
    uint8_t r = (c & 0x1F) << 3;
    uint8_t g = ((c >> 5) & 0x1F) << 3;
    uint8_t b = ((c >> 10) & 0x1F) << 3;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

// ---- GBA I/O register offsets (duplicated here to stay self-contained) -----
enum : uint16_t {
    IO_DISPCNT = 0x00,
    IO_BG0CNT = 0x08,
    IO_BG1CNT = 0x0A,
    IO_BG2CNT = 0x0C,
    IO_BG3CNT = 0x0E,
    IO_BG0HOFS = 0x10,
    IO_BG0VOFS = 0x12,
    IO_BG1HOFS = 0x14,
    IO_BG1VOFS = 0x16,
    IO_BG2HOFS = 0x18,
    IO_BG2VOFS = 0x1A,
    IO_BG3HOFS = 0x1C,
    IO_BG3VOFS = 0x1E,
    IO_WIN0H = 0x40,
    IO_WIN1H = 0x42,
    IO_WIN0V = 0x44,
    IO_WIN1V = 0x46,
    IO_WININ = 0x48,
    IO_WINOUT = 0x4A,
    IO_MOSAIC = 0x4C,
    IO_BLDCNT = 0x50,
    IO_BLDALPHA = 0x52,
    IO_BLDY = 0x54,
};

// ---- DISPCNT bit masks ----------------------------------------------------
enum : uint16_t {
    DISP_OBJ_1D = 0x0040,
    DISP_FORCED_BLANK = 0x0080,
    DISP_BG0_ON = 0x0100,
    DISP_BG1_ON = 0x0200,
    DISP_BG2_ON = 0x0400,
    DISP_BG3_ON = 0x0800,
    DISP_OBJ_ON = 0x1000,
    DISP_WIN0_ON = 0x2000,
    DISP_WIN1_ON = 0x4000,
    DISP_OBJWIN_ON = 0x8000,
};

// ---- BG tilemap entry (16-bit) -------------------------------------------
struct TilemapEntry {
    uint16_t raw;
    uint16_t tileIndex() const {
        return raw & 0x03FF;
    }
    bool hflip() const {
        return (raw >> 10) & 1;
    }
    bool vflip() const {
        return (raw >> 11) & 1;
    }
    uint8_t palette() const {
        return (raw >> 12) & 0xF;
    }
};

// ---- OAM attr helpers -----------------------------------------------------
struct OAMAttr {
    uint16_t attr0, attr1, attr2;

    int yPos() const {
        return attr0 & 0xFF;
    }
    uint8_t objMode() const {
        return (attr0 >> 8) & 3;
    } // 0=normal,1=semi,2=objwin,3=hide
    bool mosaic() const {
        return (attr0 >> 12) & 1;
    }
    bool bpp8() const {
        return (attr0 >> 13) & 1;
    }
    uint8_t shape() const {
        return (attr0 >> 14) & 3;
    }
    bool affine() const {
        return (attr0 >> 8) & 1;
    }
    bool doubleSize() const {
        return affine() && ((attr0 >> 9) & 1);
    }
    bool hidden() const {
        return !affine() && ((attr0 >> 9) & 1);
    }

    int xPos() const {
        return attr1 & 0x1FF;
    }
    bool hflip() const {
        return !affine() && ((attr1 >> 12) & 1);
    }
    bool vflip() const {
        return !affine() && ((attr1 >> 13) & 1);
    }
    uint8_t affineIdx() const {
        return (attr1 >> 9) & 0x1F;
    }
    uint8_t size() const {
        return (attr1 >> 14) & 3;
    }

    uint16_t tileIndex() const {
        return attr2 & 0x03FF;
    }
    uint8_t priority() const {
        return (attr2 >> 10) & 3;
    }
    uint8_t palette() const {
        return (attr2 >> 12) & 0xF;
    }
};

// OBJ size table [shape][size] → (w, h) in pixels
static const uint8_t objWidths[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const uint8_t objHeights[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

// ---- Render a single text BG scanline ------------------------------------
static inline void RenderTextBgLine(int bgIdx, int line,
                                    uint32_t* lineBuf, // GBA_WIDTH output pixels (ABGR8888), 0 = transparent
                                    uint8_t* priBuf)   // per-pixel priority written
{
    uint16_t bgcnt = IoRead16(IO_BG0CNT + bgIdx * 2);
    uint8_t priority = bgcnt & 3;
    uint32_t charBase = ((bgcnt >> 2) & 3) * 0x4000; // tile pixel data base
    bool bpp8 = (bgcnt >> 7) & 1;
    uint32_t screenBase = ((bgcnt >> 8) & 0x1F) * 0x800; // tilemap base
    uint16_t sizeFlag = (bgcnt >> 14) & 3;

    // text BG tilemap dimensions in tiles
    int mapW = (sizeFlag & 1) ? 64 : 32;
    int mapH = (sizeFlag & 2) ? 64 : 32;

    int scrollX = IoRead16(IO_BG0HOFS + bgIdx * 4) & 0x1FF;
    int scrollY = IoRead16(IO_BG0VOFS + bgIdx * 4) & 0x1FF;

    int srcY = (line + scrollY) % (mapH * 8);
    int tileRow = srcY / 8;
    int pixY = srcY % 8;

    for (int x = 0; x < GBA_WIDTH; ++x) {
        int srcX = (x + scrollX) % (mapW * 8);
        int tileCol = srcX / 8;
        int pixX = srcX % 8;

        // tilemap is laid out as 32×32-tile screen blocks
        // For 64-wide maps: SB0 at (0,0), SB1 at (32,0), SB2 at (0,32), SB3 at (32,32)
        int sbX = tileCol / 32;
        int sbY = tileRow / 32;
        int sbIdx = sbX + sbY * (mapW / 32);
        int localCol = tileCol % 32;
        int localRow = tileRow % 32;

        uint32_t mapAddr = screenBase + sbIdx * 0x800 + (localRow * 32 + localCol) * 2;
        TilemapEntry te;
        te.raw = gVram[mapAddr] | (gVram[mapAddr + 1] << 8);

        int tpx = te.hflip() ? (7 - pixX) : pixX;
        int tpy = te.vflip() ? (7 - pixY) : pixY;

        uint8_t colorIdx;
        if (bpp8) {
            uint32_t addr = charBase + te.tileIndex() * 64 + tpy * 8 + tpx;
            colorIdx = (addr < 0x18000) ? gVram[addr] : 0;
        } else {
            uint32_t addr = charBase + te.tileIndex() * 32 + tpy * 4 + tpx / 2;
            uint8_t byte = (addr < 0x18000) ? gVram[addr] : 0;
            colorIdx = (tpx & 1) ? (byte >> 4) : (byte & 0xF);
        }

        if (colorIdx == 0)
            continue; // transparent

        uint16_t rgb555;
        if (bpp8) {
            rgb555 = gBgPltt[colorIdx];
        } else {
            rgb555 = gBgPltt[te.palette() * 16 + colorIdx];
        }

        lineBuf[x] = Rgb555ToAbgr8888(rgb555);
        priBuf[x] = priority;
    }
}

// ---- Render OBJ (sprites) for one scanline --------------------------------
static inline void RenderObjLine(int line, bool obj1d, uint32_t* lineBuf, uint8_t* priBuf) {
    // OBJ tile data starts at 0x10000 in VRAM
    constexpr uint32_t OBJ_TILE_BASE = 0x10000;

    for (int i = OAM_COUNT - 1; i >= 0; --i) {
        OAMAttr oa;
        oa.attr0 = gOamMem[i * 4 + 0];
        oa.attr1 = gOamMem[i * 4 + 1];
        oa.attr2 = gOamMem[i * 4 + 2];

        if (oa.hidden())
            continue;

        uint8_t shape = oa.shape();
        uint8_t sz = oa.size();
        int objW = objWidths[shape][sz];
        int objH = objHeights[shape][sz];

        bool isAffine = oa.affine();

        // Bounding box dimensions (doubled for double-size affine)
        int boundsW = objW;
        int boundsH = objH;
        if (isAffine && oa.doubleSize()) {
            boundsW = objW * 2;
            boundsH = objH * 2;
        }

        int objY = oa.yPos();
        if (objY >= 160)
            objY -= 256;

        if (line < objY || line >= objY + boundsH)
            continue;

        int objX = oa.xPos();
        if (objX >= 240)
            objX -= 512;

        bool bpp8 = oa.bpp8();
        uint8_t priority = oa.priority();
        uint16_t baseTile = oa.tileIndex();
        int tilesW = objW / 8;

        // Affine parameters (8.8 fixed point)
        int16_t pa = 0x100, pb = 0, pc = 0, pd = 0x100; // identity by default
        if (isAffine) {
            int affGrp = oa.affineIdx();
            pa = (int16_t)gOamMem[affGrp * 16 + 3];
            pb = (int16_t)gOamMem[affGrp * 16 + 7];
            pc = (int16_t)gOamMem[affGrp * 16 + 11];
            pd = (int16_t)gOamMem[affGrp * 16 + 15];
        }

        int halfW = boundsW / 2;
        int halfH = boundsH / 2;
        int sprHalfW = objW / 2;
        int sprHalfH = objH / 2;

        int iry = line - objY - halfH; // relative Y from center of bounding box

        for (int sx = 0; sx < boundsW; ++sx) {
            int screenX = objX + sx;
            if (screenX < 0 || screenX >= GBA_WIDTH)
                continue;

            int texX, texY;

            if (isAffine) {
                int irx = sx - halfW; // relative X from center of bounding box
                // Transform to texture coords (8.8 fixed-point)
                texX = ((pa * irx + pb * iry) >> 8) + sprHalfW;
                texY = ((pc * irx + pd * iry) >> 8) + sprHalfH;
                if (texX < 0 || texX >= objW || texY < 0 || texY >= objH)
                    continue; // outside sprite texture
            } else {
                int drawX = oa.hflip() ? (objW - 1 - sx) : sx;
                int drawY = line - objY;
                if (oa.vflip())
                    drawY = objH - 1 - drawY;
                texX = drawX;
                texY = drawY;
            }

            int tileRow = texY / 8;
            int pixY = texY % 8;
            int tileCol = texX / 8;
            int pixX = texX % 8;

            uint16_t tileIdx;
            if (obj1d) {
                tileIdx = baseTile + tileRow * tilesW + tileCol;
                if (bpp8)
                    tileIdx = baseTile + (tileRow * tilesW + tileCol) * 2;
            } else {
                // 2D mapping: 32 tiles per row
                tileIdx = baseTile + tileRow * 32 + tileCol;
                if (bpp8)
                    tileIdx = baseTile + tileRow * 32 + tileCol * 2;
            }

            uint8_t colorIdx;
            if (bpp8) {
                uint32_t addr = OBJ_TILE_BASE + tileIdx * 32 + pixY * 8 + pixX;
                colorIdx = (addr < 0x18000) ? gVram[addr] : 0;
            } else {
                uint32_t addr = OBJ_TILE_BASE + tileIdx * 32 + pixY * 4 + pixX / 2;
                uint8_t byte = (addr < 0x18000) ? gVram[addr] : 0;
                colorIdx = (pixX & 1) ? (byte >> 4) : (byte & 0xF);
            }

            if (colorIdx == 0)
                continue;

            // Only draw if higher priority (lower number) or empty.
            // Use strict < so that lower OAM index wins at same priority
            // (we iterate from OAM_COUNT-1 down to 0).
            if (lineBuf[screenX] != 0 && priBuf[screenX] < priority)
                continue;

            uint16_t rgb555;
            if (bpp8) {
                rgb555 = gObjPltt[colorIdx];
            } else {
                rgb555 = gObjPltt[oa.palette() * 16 + colorIdx];
            }

            lineBuf[screenX] = Rgb555ToAbgr8888(rgb555);
            priBuf[screenX] = priority;
        }
    }
}

// ---- Blend helpers --------------------------------------------------------
enum BlendEffect : uint8_t {
    BLEND_NONE = 0,
    BLEND_ALPHA = 1,
    BLEND_BRIGHTEN = 2,
    BLEND_DARKEN = 3,
};

// Clamp to 0-31
static inline uint8_t Clamp5(int v) {
    return v < 0 ? 0 : (v > 31 ? 31 : (uint8_t)v);
}

// Alpha blend two RGB555 colors: result = (c1*eva + c2*evb) / 16
static inline uint32_t AlphaBlend(uint32_t top_abgr, uint32_t bot_abgr, int eva, int evb) {
    // Extract R,G,B from ABGR8888 (low bits are R)
    int tR = (top_abgr >> 0) & 0xFF;
    int tG = (top_abgr >> 8) & 0xFF;
    int tB = (top_abgr >> 16) & 0xFF;
    int bR = (bot_abgr >> 0) & 0xFF;
    int bG = (bot_abgr >> 8) & 0xFF;
    int bB = (bot_abgr >> 16) & 0xFF;
    int rR = (tR * eva + bR * evb) / 16;
    if (rR > 255)
        rR = 255;
    int rG = (tG * eva + bG * evb) / 16;
    if (rG > 255)
        rG = 255;
    int rB = (tB * eva + bB * evb) / 16;
    if (rB > 255)
        rB = 255;
    return 0xFF000000u | ((uint32_t)rB << 16) | ((uint32_t)rG << 8) | (uint32_t)rR;
}

// Brightness increase: each channel = channel + (31-channel)*evy/16 (in RGB555 scale)
static inline uint32_t BrightenPixel(uint32_t abgr, int evy) {
    int r = (abgr >> 0) & 0xFF;
    int g = (abgr >> 8) & 0xFF;
    int b = (abgr >> 16) & 0xFF;
    r = r + (255 - r) * evy / 16;
    if (r > 255)
        r = 255;
    g = g + (255 - g) * evy / 16;
    if (g > 255)
        g = 255;
    b = b + (255 - b) * evy / 16;
    if (b > 255)
        b = 255;
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

// Brightness decrease: each channel = channel - channel*evy/16
static inline uint32_t DarkenPixel(uint32_t abgr, int evy) {
    int r = (abgr >> 0) & 0xFF;
    int g = (abgr >> 8) & 0xFF;
    int b = (abgr >> 16) & 0xFF;
    r = r - r * evy / 16;
    if (r < 0)
        r = 0;
    g = g - g * evy / 16;
    if (g < 0)
        g = 0;
    b = b - b * evy / 16;
    if (b < 0)
        b = 0;
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

// ---- Layer ID enum for blend target bits ----------------------------------
// BLDCNT bits: 0=BG0, 1=BG1, 2=BG2, 3=BG3, 4=OBJ, 5=BD (backdrop)
static inline bool IsFirstTarget(uint16_t bldcnt, int layerId) {
    return (bldcnt >> layerId) & 1;
}
static inline bool IsSecondTarget(uint16_t bldcnt, int layerId) {
    return (bldcnt >> (layerId + 8)) & 1;
}

// ---- Composite BGs + OBJ with blend effects --------------------------------
static inline void CompositeLineMode0(int line, uint32_t bgLayers[BG_COUNT][GBA_WIDTH],
                                      uint8_t bgPri[BG_COUNT][GBA_WIDTH], uint32_t objLayer[GBA_WIDTH],
                                      uint8_t objPri[GBA_WIDTH], uint16_t dispcnt) {
    uint16_t backdrop = gBgPltt[0];
    uint32_t bdColor = Rgb555ToAbgr8888(backdrop);

    bool bgEnabled[4] = {
        (dispcnt & DISP_BG0_ON) != 0,
        (dispcnt & DISP_BG1_ON) != 0,
        (dispcnt & DISP_BG2_ON) != 0,
        (dispcnt & DISP_BG3_ON) != 0,
    };
    bool objEnabled = (dispcnt & DISP_OBJ_ON) != 0;

    // Read blend registers
    uint16_t bldcnt = IoRead16(IO_BLDCNT);
    uint16_t bldalpha = IoRead16(IO_BLDALPHA);
    uint16_t bldy = IoRead16(IO_BLDY);
    BlendEffect effect = (BlendEffect)((bldcnt >> 6) & 3);
    int eva = bldalpha & 0x1F;
    if (eva > 16)
        eva = 16;
    int evb = (bldalpha >> 8) & 0x1F;
    if (evb > 16)
        evb = 16;
    int evy = bldy & 0x1F;
    if (evy > 16)
        evy = 16;

    // Sort BG indices by their priority (from BGxCNT bits 0-1), lower = higher prio
    uint8_t bgOrder[4] = { 0, 1, 2, 3 };
    uint8_t bgPriority[4];
    for (int i = 0; i < 4; ++i)
        bgPriority[i] = IoRead16(IO_BG0CNT + i * 2) & 3;

    // Stable sort: lower priority number first, ties broken by lower BG index
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 4; ++j)
            if (bgPriority[bgOrder[j]] < bgPriority[bgOrder[i]]) {
                uint8_t t = bgOrder[i];
                bgOrder[i] = bgOrder[j];
                bgOrder[j] = t;
            }

    for (int x = 0; x < GBA_WIDTH; ++x) {
        // Build a sorted list of visible pixels at this column.
        // Each entry: {color, priority, layerId (0-3=BG, 4=OBJ, 5=BD)}
        // We want the top pixel and the second-from-top for alpha blending.
        uint32_t topColor = bdColor;
        int topLayerId = 5; // backdrop
        uint32_t botColor = bdColor;
        int botLayerId = 5;

        // Find topmost and second-topmost pixel.
        // Iterate priority levels 0..3. Within each, check BGs by order, then OBJ.
        bool foundTop = false;
        bool foundBot = false;

        for (int pri = 0; pri <= 3 && !foundBot; ++pri) {
            // On GBA, OBJ with priority P is drawn IN FRONT of BGs with priority P.
            // Check OBJ at this priority FIRST.
            if (objEnabled && objLayer[x] != 0 && objPri[x] == pri) {
                if (!foundTop) {
                    topColor = objLayer[x];
                    topLayerId = 4; // OBJ
                    foundTop = true;
                } else if (!foundBot) {
                    botColor = objLayer[x];
                    botLayerId = 4;
                    foundBot = true;
                }
            }

            // Then check BGs at this priority
            for (int k = 0; k < 4; ++k) {
                int bg = bgOrder[k];
                if (!bgEnabled[bg])
                    continue;
                if (bgPriority[bg] != pri)
                    continue;
                if (bgLayers[bg][x] == 0)
                    continue; // transparent

                if (!foundTop) {
                    topColor = bgLayers[bg][x];
                    topLayerId = bg;
                    foundTop = true;
                } else if (!foundBot) {
                    botColor = bgLayers[bg][x];
                    botLayerId = bg;
                    foundBot = true;
                    break;
                }
            }
        }

        // Apply blend effects
        uint32_t pixel = topColor;

        switch (effect) {
            case BLEND_ALPHA:
                // Alpha blend only if top is 1st target and bot is 2nd target
                if (IsFirstTarget(bldcnt, topLayerId) && IsSecondTarget(bldcnt, botLayerId)) {
                    pixel = AlphaBlend(topColor, botColor, eva, evb);
                }
                break;
            case BLEND_BRIGHTEN:
                if (IsFirstTarget(bldcnt, topLayerId)) {
                    pixel = BrightenPixel(topColor, evy);
                }
                break;
            case BLEND_DARKEN:
                if (IsFirstTarget(bldcnt, topLayerId)) {
                    pixel = DarkenPixel(topColor, evy);
                }
                break;
            default:
                break;
        }

        // OBJ with semi-transparent mode (objMode == 1) forces alpha blend
        // regardless of BLDCNT 1st target setting
        if (objEnabled && objLayer[x] != 0 && topLayerId == 4) {
            // Check if this OBJ has semi-transparent mode
            // We'd need the OAM attr0 objMode — for now, handled by BLDCNT
        }

        frame_buffer[line * GBA_WIDTH + x] = pixel;
    }
}

// ---- Frame entry point for Mode 1 (= GBA Mode 0) -------------------------
inline void RenderFrame(const PPUMemory& ppu) {
    uint16_t dispcnt = IoRead16(IO_DISPCNT);

    if (dispcnt & DISP_FORCED_BLANK) {
        memset(frame_buffer, 0xFF, GBA_WIDTH * GBA_HEIGHT * 4); // white
        return;
    }

    bool obj1d = (dispcnt & DISP_OBJ_1D) != 0;

    for (int line = 0; line < GBA_HEIGHT; ++line) {
        uint32_t bgLayers[BG_COUNT][GBA_WIDTH];
        uint8_t bgPri[BG_COUNT][GBA_WIDTH];
        uint32_t objLayer[GBA_WIDTH];
        uint8_t objPriLine[GBA_WIDTH];

        memset(bgLayers, 0, sizeof(bgLayers));
        memset(bgPri, 0, sizeof(bgPri));
        memset(objLayer, 0, sizeof(objLayer));
        memset(objPriLine, 0xFF, sizeof(objPriLine));

        // Render enabled text BGs
        if (dispcnt & DISP_BG0_ON)
            RenderTextBgLine(0, line, bgLayers[0], bgPri[0]);
        if (dispcnt & DISP_BG1_ON)
            RenderTextBgLine(1, line, bgLayers[1], bgPri[1]);
        if (dispcnt & DISP_BG2_ON)
            RenderTextBgLine(2, line, bgLayers[2], bgPri[2]);
        if (dispcnt & DISP_BG3_ON)
            RenderTextBgLine(3, line, bgLayers[3], bgPri[3]);

        // Render OBJ
        if (dispcnt & DISP_OBJ_ON)
            RenderObjLine(line, obj1d, objLayer, objPriLine);

        // Composite
        CompositeLineMode0(line, bgLayers, bgPri, objLayer, objPriLine, dispcnt);
    }
}

} // namespace Mode1
