#pragma once

#include <PPUMemory.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Mode7
{
constexpr std::size_t GB_SCREEN_WIDTH = 160;
constexpr std::size_t GB_SCREEN_HEIGHT = 144;
constexpr std::size_t VRAM_SIZE_BYTES = 0x2000;
constexpr std::size_t OAM_SIZE_BYTES = 0x00A0;

constexpr uint8_t LCDC_ENABLE = (1u << 7);
constexpr uint8_t LCDC_WINDOW_TILE_MAP = (1u << 6);
constexpr uint8_t LCDC_WINDOW_ENABLE = (1u << 5);
constexpr uint8_t LCDC_BG_WINDOW_TILE_DATA = (1u << 4);
constexpr uint8_t LCDC_BG_TILE_MAP = (1u << 3);
constexpr uint8_t LCDC_OBJ_SIZE = (1u << 2);
constexpr uint8_t LCDC_OBJ_ENABLE = (1u << 1);
constexpr uint8_t LCDC_BG_ENABLE = (1u << 0);

struct GBRegs
{
    uint8_t lcdc;
    uint8_t scy;
    uint8_t scx;
    uint8_t bgp;
    uint8_t obp0;
    uint8_t obp1;
    uint8_t wy;
    uint8_t wx;
};

struct Mode7Layout
{
    uint8_t vram[VRAM_SIZE_BYTES];
    uint8_t oam[OAM_SIZE_BYTES];
    GBRegs regs;
};

static_assert(sizeof(Mode7Layout) <= (4u * 1024u * 1024u), "Mode7Layout exceeds VRAM storage");

struct SpriteCandidate
{
    uint8_t x;
    uint8_t tile;
    uint8_t attributes;
    uint8_t line;
    uint8_t index;
};

inline Mode7Layout *GetLayout()
{
    return reinterpret_cast<Mode7Layout *>(VRAM);
}

inline const Mode7Layout *GetLayoutConst()
{
    return reinterpret_cast<const Mode7Layout *>(VRAM);
}

static inline uint8_t VramRead(const Mode7Layout *layout, uint16_t addr)
{
    if (addr < 0x8000u || addr >= 0xA000u)
        return 0;
    return layout->vram[addr - 0x8000u];
}

static inline uint32_t PaletteColor(uint8_t palette, uint8_t color_id)
{
    static constexpr uint32_t dmg_palette[4] = {
        0xFF9BBC0F, // lightest
        0xFF8BAC0F,
        0xFF306230,
        0xFF0F380F // darkest
    };

    const uint8_t shade = (palette >> (color_id * 2u)) & 0x03u;
    return dmg_palette[shade];
}

static inline uint8_t FetchTileColor(const Mode7Layout *layout,
                                     uint16_t tile_map_base,
                                     uint16_t tile_data_base,
                                     bool signed_indexing,
                                     uint8_t x,
                                     uint8_t y)
{
    const uint8_t tile_x = static_cast<uint8_t>(x / 8u);
    const uint8_t tile_y = static_cast<uint8_t>(y / 8u);
    const uint16_t map_index = static_cast<uint16_t>(tile_y * 32u + tile_x);
    const uint16_t map_addr = static_cast<uint16_t>(tile_map_base + map_index);
    const uint8_t tile_index = VramRead(layout, map_addr);

    const int32_t tile_id = signed_indexing ? static_cast<int8_t>(tile_index) : tile_index;
    const uint16_t tile_addr = static_cast<uint16_t>(tile_data_base + tile_id * 16);
    const uint8_t row = static_cast<uint8_t>(y % 8u);
    const uint16_t tile_row_addr = static_cast<uint16_t>(tile_addr + row * 2u);
    const uint8_t low = VramRead(layout, tile_row_addr);
    const uint8_t high = VramRead(layout, static_cast<uint16_t>(tile_row_addr + 1u));
    const uint8_t col = static_cast<uint8_t>(x % 8u);
    const uint8_t bit = static_cast<uint8_t>(7u - col);

    return static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
}

static inline uint8_t EvalSprites(const Mode7Layout *layout,
                                  uint8_t ly,
                                  uint8_t sprite_height,
                                  SpriteCandidate *out_sprites)
{
    uint8_t candidate_count = 0;
    SpriteCandidate candidates[40];

    for (uint8_t i = 0; i < 40; ++i)
    {
        const uint8_t y = layout->oam[i * 4u];
        const uint8_t x = layout->oam[i * 4u + 1u];
        const uint8_t tile = layout->oam[i * 4u + 2u];
        const uint8_t attr = layout->oam[i * 4u + 3u];

        const int sprite_y = static_cast<int>(y) - 16;
        if (static_cast<int>(ly) < sprite_y || static_cast<int>(ly) >= sprite_y + sprite_height)
            continue;
        if (x == 0 || x >= 168)
            continue;

        SpriteCandidate sc{};
        sc.x = x;
        sc.tile = tile;
        sc.attributes = attr;
        uint8_t line = static_cast<uint8_t>(ly - sprite_y);
        if (attr & 0x40u)
        {
            line = static_cast<uint8_t>((sprite_height - 1u) - line);
        }
        sc.line = line;
        sc.index = i;
        candidates[candidate_count++] = sc;
    }

    for (uint8_t i = 1; i < candidate_count; ++i)
    {
        const SpriteCandidate key = candidates[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 &&
               (candidates[j].x > key.x ||
                (candidates[j].x == key.x && candidates[j].index > key.index)))
        {
            candidates[j + 1] = candidates[j];
            --j;
        }
        candidates[j + 1] = key;
    }

    const uint8_t limit = (candidate_count > 10u) ? 10u : candidate_count;
    for (uint8_t i = 0; i < limit; ++i)
    {
        out_sprites[i] = candidates[i];
    }

    return limit;
}

inline void RenderFrame(const PPUMemory &ppu)
{
    (void)ppu;
    const Mode7Layout *layout = GetLayoutConst();
    const GBRegs &regs = layout->regs;

    if ((regs.lcdc & LCDC_ENABLE) == 0)
    {
        const uint32_t clear_color = PaletteColor(regs.bgp, 0);
        for (std::size_t i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; ++i)
        {
            frame_buffer[i] = clear_color;
        }
        return;
    }

    for (uint8_t y = 0; y < static_cast<uint8_t>(GB_SCREEN_HEIGHT); ++y)
    {
        SpriteCandidate sprites[10];
        uint8_t sprite_count = 0;
        if (regs.lcdc & LCDC_OBJ_ENABLE)
        {
            const uint8_t sprite_height = (regs.lcdc & LCDC_OBJ_SIZE) ? 16u : 8u;
            sprite_count = EvalSprites(layout, y, sprite_height, sprites);
        }

        for (uint8_t x = 0; x < static_cast<uint8_t>(GB_SCREEN_WIDTH); ++x)
        {
            uint8_t bg_color_id = 0;
            uint32_t bg_color = PaletteColor(regs.bgp, 0);

            if (regs.lcdc & LCDC_BG_ENABLE)
            {
                const uint16_t tile_map_base = (regs.lcdc & LCDC_BG_TILE_MAP) ? 0x9C00u : 0x9800u;
                const uint16_t tile_data_base = (regs.lcdc & LCDC_BG_WINDOW_TILE_DATA) ? 0x8000u : 0x9000u;
                const bool signed_indexing = (regs.lcdc & LCDC_BG_WINDOW_TILE_DATA) == 0;

                const uint8_t bg_x = static_cast<uint8_t>(x + regs.scx);
                const uint8_t bg_y = static_cast<uint8_t>(y + regs.scy);
                bg_color_id = FetchTileColor(layout, tile_map_base, tile_data_base, signed_indexing, bg_x, bg_y);

                const bool window_enabled = (regs.lcdc & LCDC_WINDOW_ENABLE) != 0;
                if (window_enabled && regs.wy <= y)
                {
                    const uint8_t wx = (regs.wx > 7u) ? static_cast<uint8_t>(regs.wx - 7u) : 0u;
                    if (x >= wx && regs.wx <= 166u)
                    {
                        const uint8_t window_x = static_cast<uint8_t>(x - wx);
                        const uint8_t window_y = static_cast<uint8_t>(y - regs.wy);
                        const uint16_t window_map_base = (regs.lcdc & LCDC_WINDOW_TILE_MAP) ? 0x9C00u : 0x9800u;
                        bg_color_id = FetchTileColor(layout,
                                                     window_map_base,
                                                     tile_data_base,
                                                     signed_indexing,
                                                     window_x,
                                                     window_y);
                    }
                }

                bg_color = PaletteColor(regs.bgp, bg_color_id);
            }

            uint32_t final_color = bg_color;

            if ((regs.lcdc & LCDC_OBJ_ENABLE) && sprite_count > 0)
            {
                const uint8_t sprite_height = (regs.lcdc & LCDC_OBJ_SIZE) ? 16u : 8u;

                for (uint8_t i = 0; i < sprite_count; ++i)
                {
                    const int screen_x = static_cast<int>(sprites[i].x) - 8;
                    if (x < screen_x || x >= screen_x + 8)
                        continue;

                    uint8_t pixel_x = static_cast<uint8_t>(x - screen_x);
                    const uint8_t attributes = sprites[i].attributes;
                    if (attributes & 0x20u)
                    {
                        pixel_x = static_cast<uint8_t>(7u - pixel_x);
                    }

                    uint8_t tile_index = sprites[i].tile;
                    uint8_t line = sprites[i].line;
                    if (sprite_height == 16u)
                    {
                        tile_index = static_cast<uint8_t>((tile_index & 0xFEu) | (line >= 8u));
                        line &= 0x07u;
                    }

                    const uint16_t tile_addr = static_cast<uint16_t>(0x8000u + tile_index * 16u);
                    const uint16_t row_addr = static_cast<uint16_t>(tile_addr + line * 2u);
                    const uint8_t low = VramRead(layout, row_addr);
                    const uint8_t high = VramRead(layout, static_cast<uint16_t>(row_addr + 1u));
                    const uint8_t bit = static_cast<uint8_t>(7u - pixel_x);
                    const uint8_t color_id =
                        static_cast<uint8_t>((((high >> bit) & 0x01u) << 1u) | ((low >> bit) & 0x01u));
                    if (color_id == 0)
                        continue;

                    const uint8_t palette = (attributes & 0x10u) ? regs.obp1 : regs.obp0;
                    const uint32_t sprite_color = PaletteColor(palette, color_id);
                    if ((attributes & 0x80u) && bg_color_id != 0)
                    {
                        final_color = bg_color;
                    }
                    else
                    {
                        final_color = sprite_color;
                    }
                    break;
                }
            }

            frame_buffer[static_cast<std::size_t>(y) * GB_SCREEN_WIDTH + x] = final_color;
        }
    }
}
} // namespace Mode7
