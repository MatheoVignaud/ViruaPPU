#pragma once
#include <PPUMemory.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Mode0
{
    // --------------------------------------------
    // Config (à adapter à ton moteur)
    // --------------------------------------------
    constexpr std::size_t VRAM_MAX_BYTES = 4u * 1024u * 1024u;
    constexpr std::size_t BG_COUNT = 4;
    constexpr std::size_t OAM_COUNT = 512;

    // Tu avais 12000 entries par BG (probablement max 1280*360/??).
    // Garde ta valeur pour compat, mais maintenant c'est hors BgEntry.
    constexpr std::size_t TILEMAP_ENTRIES_PER_BG = 12000;

    // Max lignes pour line-regs (HDMA-like). Tu avais évoqué 360.
    constexpr std::size_t MAX_LINES = 360;

    // --------------------------------------------
    // Tiles (inchangés)
    // --------------------------------------------
    struct GfxBloc8pbb
    {
        uint8_t data[64]; // 8x8 pixels, 8bpp
    };

    struct GfxBloc4pbb
    {
        uint8_t data[32]; // 8x8 pixels, 4bpp
    };

    // --------------------------------------------
    // Palettes : RGB555 recommandé (économise et suffit "rétro")
    // --------------------------------------------
    struct RGB888
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    struct Palette16RGB888
    {
        RGB888 colors[16]; // 16 couleurs, RGB555
    };

    // 256 couleurs = 16 palettes de 16
    struct Palette256RGB888
    {
        Palette16RGB888 palettes[16];
    };

    // Exemple : 6 palettes de 256 couleurs (BG/OBJ banks, effets, etc.)
    constexpr std::size_t PALETTE_256_BANKS = 6;

    // --------------------------------------------
    // Tilemap entry 32-bit (superset GBA/SNES)
    //
    // bits  0..15 : tileIndex (0..65535)
    // bits 16..23 : reserved
    // bits 24..26 : priority (0..7)
    // bit      27 : hflip
    // bit      28 : vflip
    // bit      29 : mosaic enable (option)
    // bits 30..31 : reserved
    // --------------------------------------------
    using TileEntry = uint32_t;

    enum TileEntryBits : uint32_t
    {
        TILE_HFLIP = (1u << 27),
        TILE_VFLIP = (1u << 28),
        TILE_MOSAIC = (1u << 29),
    };

    static inline constexpr TileEntry MakeTileEntry(
        uint16_t tileIndex,
        uint8_t paletteIndex,
        uint8_t priority, // 0..7
        bool hflip,
        bool vflip,
        bool mosaicEnable = false)
    {
        return (uint32_t(tileIndex) & 0xFFFFu) | (uint32_t(paletteIndex) << 16) | ((uint32_t(priority) & 0x7u) << 24) | (hflip ? TILE_HFLIP : 0u) | (vflip ? TILE_VFLIP : 0u) | (mosaicEnable ? TILE_MOSAIC : 0u);
    }

    // --------------------------------------------
    // BG + Affine
    // --------------------------------------------
    enum BgFlags : uint16_t
    {
        BG_FLAG_ENABLED = (1u << 0),
        BG_FLAG_BPP8 = (1u << 1), // sinon 4bpp
        BG_FLAG_WRAP_X = (1u << 2),
        BG_FLAG_WRAP_Y = (1u << 3),
        BG_FLAG_AFFINE = (1u << 4), // utilise affine matrix (a,b,c,d,tx,ty)
        BG_FLAG_MOSAIC = (1u << 5),
        // réserve bits futurs : per-layer color math enable, etc.
    };

    // Matrice 2x2 en fixed-point 8.8
    // x = (a*u + b*v) + tx
    // y = (c*u + d*v) + ty
    struct Affine2x2_8_8
    {
        int16_t a; // 8.8
        int16_t b; // 8.8
        int16_t c; // 8.8
        int16_t d; // 8.8
    };

    struct BgEntry
    {
        // "Base tile index" dans GfxData (unité = tile 8x8)
        // => pas un offset byte.
        uint16_t tileBase;

        // Pour 4bpp, paletteIndex est souvent 0..15 (bank).
        // Pour 8bpp, paletteIndex peut servir de bank/offset si tu veux.
        uint16_t paletteIndex;

        // Scroll signé (utile)
        int16_t scrollX;
        int16_t scrollY;

        // Flags
        uint16_t flags;

        // Priorité de couche (tu peux la mixer avec tile priority)
        uint8_t layerPriority; // 0..7 (plus grand = plus "devant" ou l’inverse selon toi)
        uint8_t mosaicSizeX;   // 0=disabled, sinon 1..127
        uint8_t mosaicSizeY;   // 0=disabled, sinon 1..127
        uint8_t _pad0;

        // Affine (si BG_FLAG_AFFINE)
        Affine2x2_8_8 m;

        // Offsets affine en 24.8 (ou 16.8) : on met 24.8 via int32 (simple)
        int32_t tx; // 24.8
        int32_t ty; // 24.8
    };

    // --------------------------------------------
    // OAM (sprites)
    // --------------------------------------------
    enum OamFlags : uint16_t
    {
        OAM_FLAG_ENABLED = (1u << 0),
        OAM_FLAG_BPP8 = (1u << 1), // sinon 4bpp
        OAM_FLAG_HFLIP = (1u << 2),
        OAM_FLAG_VFLIP = (1u << 3),
        OAM_FLAG_MOSAIC = (1u << 4),
        OAM_FLAG_AFFINE = (1u << 5),
        OAM_FLAG_DOUBLE_SIZE = (1u << 6), // bounding box doublée (affine)
        OAM_FLAG_SEMI_TRANSP = (1u << 7), // participe au color math / alpha rétro
        OAM_FLAG_OBJ_WINDOW = (1u << 8),  // n’écrit pas la couleur, écrit dans mask fenêtre
        // bits libres : blendmode custom, priority rules, etc.
    };

    // Matrices affine OBJ partagées (style GBA)
    struct ObjAffine
    {
        Affine2x2_8_8 m; // 8.8
    };

    struct OAMEntry
    {
        int16_t y;
        int16_t x;

        // Taille en blocs de 8 pixels (comme ton code)
        uint8_t heightBlocks; // h = heightBlocks * 8
        uint8_t widthBlocks;  // w = widthBlocks  * 8

        uint16_t paletteIndex; // 0..15 (4bpp) ou bank/offset (8bpp)
        uint16_t tileIndex;    // base tile index dans GfxData

        uint8_t priority;    // 0..7
        uint8_t affineIndex; // index dans objAffine[] si OAM_FLAG_AFFINE

        uint16_t flags; // OamFlags
    };

    constexpr std::size_t OBJ_AFFINE_COUNT = 64;

    // --------------------------------------------
    // Windows / Masking (Win0, Win1, Outside, OBJ window)
    // --------------------------------------------
    enum LayerMaskBits : uint16_t
    {
        LAYER_BG0 = (1u << 0),
        LAYER_BG1 = (1u << 1),
        LAYER_BG2 = (1u << 2),
        LAYER_BG3 = (1u << 3),
        LAYER_OBJ = (1u << 4),
        LAYER_COLORMATH = (1u << 5), // pour color math target
    };

    struct WindowRect
    {
        uint16_t x1, x2;
        uint16_t y1, y2;
    };

    struct WindowCtrl
    {
        WindowRect rect;
        uint16_t enableMask; // LayerMaskBits
        uint16_t flags;      // réservé : invert, etc.
    };

    // --------------------------------------------
    // Color math / blending (SNES/GBA-like superset)
    // --------------------------------------------
    enum ColorMathMode : uint8_t
    {
        COLORMATH_OFF = 0,
        COLORMATH_ADD = 1,
        COLORMATH_SUB = 2,
        COLORMATH_AVG = 3,    // (A+B)/2
        COLORMATH_EVA_EVB = 4 // out = eva*A + evb*B (0..16)
    };

    struct ColorMathCtrl
    {
        uint8_t mode; // ColorMathMode
        uint8_t eva;  // 0..16
        uint8_t evb;  // 0..16
        uint8_t half; // bool (si tu veux l'option "half" à part)

        // Qui peut être A et B (layer masks)
        uint16_t targetA; // LayerMaskBits
        uint16_t targetB; // LayerMaskBits

        // Fade global (optionnel)
        uint8_t fadeToWhite; // 0/1
        uint8_t fadeToBlack; // 0/1
        uint8_t fadeFactor;  // 0..16
        uint8_t _pad;
    };

    // --------------------------------------------
    // Regs globaux PPU
    // --------------------------------------------
    struct PPURegs
    {
        RGB888 backdropColor;      // couleur de fond (si rien)
        uint16_t masterEnableMask; // LAYER_BG0..LAYER_OBJ etc. (LayerMaskBits)

        WindowCtrl win0;
        WindowCtrl win1;

        // "outside" = ce qui n’est ni dans win0 ni dans win1 ni (option) obj window
        uint16_t outsideEnableMask; // LayerMaskBits
        uint16_t useObjWindow;      // bool

        ColorMathCtrl colorMath;
    };

    struct LineScroll
    {
        int16_t scrollX;
        int16_t scrollY;
    };

    struct LineAffineTxTy
    {
        int32_t tx; // 24.8
        int32_t ty; // 24.8
    };

    struct Mode0Layout
    {
        // Registres globaux
        PPURegs regs;

        // BG regs
        BgEntry bg[BG_COUNT];

        // Tilemaps : 4 BG * 12000 entries
        TileEntry tilemaps[BG_COUNT][TILEMAP_ENTRIES_PER_BG];

        // Data tiles : 2MB max comme toi
        uint8_t GfxData[2u * 1024u * 1024u];

        // Palettes : 6 banks de 256 couleurs (RGB555)
        Palette256RGB888 palettes[PALETTE_256_BANKS];

        // Affines OBJ (GBA-like)
        ObjAffine objAffine[OBJ_AFFINE_COUNT];

        // OAM
        OAMEntry OAM[OAM_COUNT];

        // Line regs (HDMA-like) : scroll par BG par ligne
        LineScroll bgLineScroll[BG_COUNT][MAX_LINES];

        // Line affine TX/TY par BG
        LineAffineTxTy bgLineAffine[BG_COUNT][MAX_LINES];
    };

    // --------------------------------------------
    // Sanity checks
    // --------------------------------------------
    static_assert(sizeof(Mode0Layout) <= VRAM_MAX_BYTES, "Mode0Layout depasse 4MB !");
    static_assert(sizeof(TileEntry) == 4, "TileEntry doit etre en 32-bit.");

    inline void setPalette16(std::size_t paletteBankIndex, std::size_t paletteIndexInBank, const Palette16RGB888 &palette)
    {
        if (paletteBankIndex >= PALETTE_256_BANKS || paletteIndexInBank >= 16)
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->palettes[paletteBankIndex].palettes[paletteIndexInBank] = palette;
    }

    inline void setPalette256(std::size_t paletteBankIndex, const Palette256RGB888 &palette)
    {
        if (paletteBankIndex >= PALETTE_256_BANKS)
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->palettes[paletteBankIndex] = palette;
    }

    inline void setGfxData(const uint8_t *data, std::size_t size, std::size_t offset = 0)
    {
        if (offset + size > sizeof(Mode0Layout::GfxData))
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        std::memcpy(&layout->GfxData[offset], data, size);
    }

    inline void setTilemapEntry(std::size_t bgIndex, std::size_t entryIndex, TileEntry entry)
    {
        if (bgIndex >= BG_COUNT || entryIndex >= TILEMAP_ENTRIES_PER_BG)
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->tilemaps[bgIndex][entryIndex] = entry;
    }

    inline void setBgEntry(std::size_t bgIndex, const BgEntry &bgEntry)
    {
        if (bgIndex >= BG_COUNT)
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->bg[bgIndex] = bgEntry;
    }

    inline void setOAMEntry(std::size_t oamIndex, const OAMEntry &oamEntry)
    {
        if (oamIndex >= OAM_COUNT)
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->OAM[oamIndex] = oamEntry;
    }

    inline void setPPURegs(const PPURegs &regs)
    {
        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->regs = regs;
    }

    inline void setbgLineScroll(std::size_t bgIndex, std::size_t lineIndex, const LineScroll &lineScroll)
    {
        if (bgIndex >= BG_COUNT || lineIndex >= MAX_LINES)
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->bgLineScroll[bgIndex][lineIndex] = lineScroll;
    }

    inline void setbgLineAffineTxTy(std::size_t bgIndex, std::size_t lineIndex, const LineAffineTxTy &lineAffine)
    {
        if (bgIndex >= BG_COUNT || lineIndex >= MAX_LINES)
            return; // out of bounds

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        layout->bgLineAffine[bgIndex][lineIndex] = lineAffine;
    }

    static inline int32_t Wrap(int32_t v, int32_t size)
    {
        // size > 0
        int32_t r = v % size;
        return (r < 0) ? (r + size) : r;
    }

    static inline int32_t MosaicCoord(int32_t c, int32_t m) // m=0 => disabled
    {
        if (m <= 1)
            return c;
        return c - (c % m);
    }

    inline void GetBg32px(uint8_t bgIndex, std::size_t line, std::size_t xPixelOffset, uint32_t *outPixels)
    {
        if (bgIndex >= BG_COUNT)
        {
            return;
        }

        Mode0Layout *layout = reinterpret_cast<Mode0Layout *>(VRAM);

        int32_t sx = layout->bg[bgIndex].scrollX;
        int32_t sy = layout->bg[bgIndex].scrollY;

        sx += layout->bgLineScroll[bgIndex][line].scrollX;
        sy += layout->bgLineScroll[bgIndex][line].scrollY;
    }

    inline void RenderBg(uint8_t index,
                         uint64_t &opaque_mask,
                         uint32_t *scanline_layers, // pointeur vers buffer linéaire
                         const PPUMemory &ppu,
                         std::size_t line)
    {
        const std::size_t width = std::size_t(ppu.frame_width);
        const std::size_t nb32pxBlocks = (width + 31) / 32;

        auto *layout = reinterpret_cast<Mode0Layout *>(VRAM);
        const std::size_t layer_base = std::size_t(index) * width;

        for (std::size_t block = 0; block < nb32pxBlocks; ++block)
        {
            const uint64_t bit = (block < 64) ? (uint64_t(1) << block) : 0;
            if (block < 64 && (opaque_mask & bit))
                continue;

            const std::size_t x0 = block * 32;
            uint32_t *dst = &scanline_layers[layer_base + x0];

            GetBg32px(index, line, x0, dst);

            uint32_t anyNotOpaque = 0;
            const std::size_t count = (x0 + 32 <= width) ? 32 : (width - x0);
            for (std::size_t i = 0; i < count; ++i)
                anyNotOpaque |= (dst[i] & 0xFF000000u) ^ 0xFF000000u;

            const uint64_t isOpaque = (anyNotOpaque == 0) ? 1ull : 0ull;
            if (block < 64)
                opaque_mask |= (isOpaque << block);
        }
    }

    inline void CompositeAndOam(const uint32_t *scanline_layers,
                                const PPUMemory &ppu,
                                std::size_t line)
    {
        const std::size_t width = std::size_t(ppu.frame_width);

        // TODO: ici tu fais le resolve des 4 layers + sprites
        // Pour l’instant, copie BG0 (layer 0) pour test.
        const uint32_t *bg0 = &scanline_layers[0 * width];

        for (std::size_t x = 0; x < width; ++x)
            frame_buffer[line * width + x] = bg0[x];
    }

    inline void RenderFrame(const PPUMemory &ppu)
    {
        const std::size_t width = std::size_t(ppu.frame_width);
        const uint8_t padding = width % 32;

#ifdef USE_OPENMP
#pragma omp parallel
        {
#endif

            std::vector<uint32_t> scanline_layers(BG_COUNT * (width + padding));

#ifdef USE_OPENMP
#pragma omp for
#endif
            for (std::size_t line = 0; line < 360; ++line)
            {
                std::fill(scanline_layers.begin(), scanline_layers.end(), 0);

                uint64_t opaque_mask = 0;

                // IMPORTANT: ordre top->bottom si tu veux que opaque_mask soit valide
                for (uint8_t bgIndex = 0; bgIndex < BG_COUNT; ++bgIndex)
                    RenderBg(bgIndex, opaque_mask, scanline_layers.data(), ppu, line);

                CompositeAndOam(scanline_layers.data(), ppu, line);
            }
        }
#ifdef USE_OPENMP
    }
#endif
} // namespace Mode0
