#pragma once
// ============================================================================
// Mode 2 — GBA Mode 1 : 2 Text BGs (BG0, BG1) + 1 Affine BG (BG2) + OBJ
//
// Reads directly from the emulated GBA memory arrays.
// BG3 is not available in this mode.
// ============================================================================
#include "mode1.hpp" // reuse text BG, OBJ rendering, helpers
#include <PPUMemory.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Mode2 {

using namespace Mode1; // reuse IoRead16, Rgb555ToAbgr8888, RenderTextBgLine, RenderObjLine, etc.

// ---- Render an affine (rotation/scaling) BG scanline ----------------------
static inline void RenderAffineBgLine(int bgIdx, int line, uint32_t* lineBuf, uint8_t* priBuf) {
    uint16_t bgcnt = IoRead16(IO_BG0CNT + bgIdx * 2);
    uint8_t priority = bgcnt & 3;
    uint32_t charBase = ((bgcnt >> 2) & 3) * 0x4000;
    // affine BGs are always 8bpp single-palette
    uint32_t screenBase = ((bgcnt >> 8) & 0x1F) * 0x800;
    bool wrap = (bgcnt >> 13) & 1;
    uint16_t sizeFlag = (bgcnt >> 14) & 3;

    // Affine BG sizes: 128, 256, 512, 1024
    static const int affSizes[4] = { 128, 256, 512, 1024 };
    int mapSize = affSizes[sizeFlag]; // in pixels
    int mapTiles = mapSize / 8;       // tilemap is mapTiles × mapTiles (8-bit entries)

    // Affine parameters from I/O registers
    // BG2: PA=0x20, PB=0x22, PC=0x24, PD=0x26, X=0x28 (32-bit), Y=0x2C (32-bit)
    // BG3: PA=0x30, PB=0x32, PC=0x34, PD=0x36, X=0x38 (32-bit), Y=0x3C (32-bit)
    uint16_t affBase = (bgIdx == 2) ? 0x20 : 0x30;

    int16_t pa = (int16_t)IoRead16(affBase + 0);
    int16_t pb = (int16_t)IoRead16(affBase + 2);
    int16_t pc = (int16_t)IoRead16(affBase + 4);
    int16_t pd = (int16_t)IoRead16(affBase + 6);

    // Reference point is 28-bit signed fixed-point (20.8)
    int32_t refX = (int32_t)IoRead32(affBase + 8);
    int32_t refY = (int32_t)IoRead32(affBase + 12);

    // Sign-extend 28-bit → 32-bit
    if (refX & 0x08000000)
        refX |= 0xF0000000u;
    if (refY & 0x08000000)
        refY |= 0xF0000000u;

    // Starting texture coordinates for this scanline (fixed 8.8)
    int32_t texX = refX + pb * line;
    int32_t texY = refY + pd * line;

    for (int x = 0; x < GBA_WIDTH; ++x) {
        // Convert from 8.8 fixed-point to integer pixel coords
        int32_t srcX = texX >> 8;
        int32_t srcY = texY >> 8;

        texX += pa;
        texY += pc;

        // Wrapping or clipping
        if (wrap) {
            srcX = ((srcX % mapSize) + mapSize) % mapSize;
            srcY = ((srcY % mapSize) + mapSize) % mapSize;
        } else {
            if (srcX < 0 || srcX >= mapSize || srcY < 0 || srcY >= mapSize)
                continue; // transparent (outside)
        }

        int tileCol = srcX / 8;
        int tileRow = srcY / 8;
        int pixX = srcX % 8;
        int pixY = srcY % 8;

        // Affine tilemap: 8-bit entries (just tile index, no flip/palette)
        uint32_t mapAddr = screenBase + tileRow * mapTiles + tileCol;
        uint8_t tileIdx = (mapAddr < 0x18000) ? gVram[mapAddr] : 0;

        // Always 8bpp
        uint32_t tileAddr = charBase + tileIdx * 64 + pixY * 8 + pixX;
        uint8_t colorIdx = (tileAddr < 0x18000) ? gVram[tileAddr] : 0;

        if (colorIdx == 0)
            continue; // transparent

        uint16_t rgb555 = gBgPltt[colorIdx];
        lineBuf[x] = Rgb555ToAbgr8888(rgb555);
        priBuf[x] = priority;
    }
}

// ---- Frame entry point for Mode 2 (= GBA Mode 1) -------------------------
inline void RenderFrame(const PPUMemory& ppu) {
    uint16_t dispcnt = IoRead16(IO_DISPCNT);

    if (dispcnt & DISP_FORCED_BLANK) {
        memset(frame_buffer, 0xFF, GBA_WIDTH * GBA_HEIGHT * 4);
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

        // BG0, BG1: text
        if (dispcnt & DISP_BG0_ON)
            RenderTextBgLine(0, line, bgLayers[0], bgPri[0]);
        if (dispcnt & DISP_BG1_ON)
            RenderTextBgLine(1, line, bgLayers[1], bgPri[1]);

        // BG2: affine
        if (dispcnt & DISP_BG2_ON)
            RenderAffineBgLine(2, line, bgLayers[2], bgPri[2]);

        // BG3: not available in GBA Mode 1

        // OBJ
        if (dispcnt & DISP_OBJ_ON)
            RenderObjLine(line, obj1d, objLayer, objPriLine);

        // Composite (reuse Mode1's compositor)
        CompositeLineMode0(line, bgLayers, bgPri, objLayer, objPriLine, dispcnt);
    }
}

} // namespace Mode2
