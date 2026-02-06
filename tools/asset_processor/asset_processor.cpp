#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct BMPFileHeader
{
    uint16_t signature; // 'BM'
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;
};

struct BMPInfoHeader
{
    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
};
#pragma pack(pop)

struct rgb888
{
    uint8_t r;
    uint8_t g;
    uint8_t b;

    bool operator==(const rgb888 &other) const
    {
        return r == other.r && g == other.g && b == other.b;
    }
};

struct pal16
{
    rgb888 colors[16];
    size_t color_count = 0;

    bool operator==(const pal16 &other) const
    {
        if (color_count != other.color_count)
            return false;
        for (size_t i = 0; i < color_count; ++i)
        {
            if (!(colors[i] == other.colors[i]))
                return false;
        }
        return true;
    }
};

struct pal256
{
    rgb888 colors[256];
};

struct sprite_block
{
    uint8_t tile_data[32]; // 8x8, 4bpp => 32 bytes
};

struct sprite_data
{
    std::string name;
    uint16_t width;
    uint16_t height;
    uint16_t blocks_w;
    uint16_t blocks_h;
    size_t palette_index;
    std::vector<sprite_block> blocks;
};

// Fonction pour lire un BMP indexé et extraire les données
bool load_indexed_bmp(const std::string &filepath, std::vector<uint8_t> &pixel_indices,
                      pal16 &palette, int &width, int &height)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Cannot open file: " << filepath << "\n";
        return false;
    }

    BMPFileHeader file_header;
    file.read(reinterpret_cast<char *>(&file_header), sizeof(file_header));

    if (file_header.signature != 0x4D42) // 'BM' en little-endian
    {
        std::cerr << "Not a valid BMP file: " << filepath << "\n";
        return false;
    }

    BMPInfoHeader info_header;
    file.read(reinterpret_cast<char *>(&info_header), sizeof(info_header));

    // Vérifier que c'est un BMP indexé (4 bits = 16 couleurs max, ou 8 bits avec <= 16 couleurs)
    if (info_header.bits_per_pixel != 4 && info_header.bits_per_pixel != 8)
    {
        std::cerr << "BMP must be indexed (4bpp or 8bpp): " << filepath
                  << " (found " << info_header.bits_per_pixel << "bpp)\n";
        return false;
    }

    width = info_header.width;
    height = std::abs(info_header.height);
    bool top_down = info_header.height < 0;

    // Vérifier les dimensions
    if (width % 8 != 0 || height % 8 != 0)
    {
        std::cerr << "Sprite dimensions must be divisible by 8: " << filepath
                  << " (" << width << "x" << height << ")\n";
        return false;
    }

    // Lire la palette
    uint32_t num_colors = info_header.colors_used;
    if (num_colors == 0)
        num_colors = (1u << info_header.bits_per_pixel);

    if (num_colors > 16)
    {
        std::cerr << "BMP has more than 16 colors in palette: " << filepath
                  << " (" << num_colors << " colors)\n";
        return false;
    }

    palette.color_count = num_colors;
    for (uint32_t i = 0; i < num_colors; ++i)
    {
        uint8_t bgra[4];
        file.read(reinterpret_cast<char *>(bgra), 4);    // BGRA format in BMP
        palette.colors[i] = {bgra[2], bgra[1], bgra[0]}; // Convert to RGB
    }

    // Remplir le reste de la palette avec du noir
    for (size_t i = num_colors; i < 16; ++i)
    {
        palette.colors[i] = {0, 0, 0};
    }

    // Aller au début des données pixel
    file.seekg(file_header.data_offset, std::ios::beg);

    // Calculer le row stride (aligné sur 4 bytes)
    int row_stride;
    if (info_header.bits_per_pixel == 4)
        row_stride = ((width + 1) / 2 + 3) & ~3;
    else // 8bpp
        row_stride = (width + 3) & ~3;

    // Lire les pixels
    pixel_indices.resize(width * height);
    std::vector<uint8_t> row_data(row_stride);

    for (int y = 0; y < height; ++y)
    {
        int dest_y = top_down ? y : (height - 1 - y);
        file.read(reinterpret_cast<char *>(row_data.data()), row_stride);

        for (int x = 0; x < width; ++x)
        {
            uint8_t index;
            if (info_header.bits_per_pixel == 4)
            {
                uint8_t byte = row_data[x / 2];
                index = (x % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
            }
            else // 8bpp
            {
                index = row_data[x];
                if (index >= 16)
                {
                    std::cerr << "Pixel uses color index >= 16: " << filepath << "\n";
                    return false;
                }
            }
            pixel_indices[dest_y * width + x] = index;
        }
    }

    return true;
}

size_t find_or_add_palette(std::vector<pal16> &palettes, const pal16 &palette)
{
    for (size_t i = 0; i < palettes.size(); ++i)
    {
        if (palettes[i] == palette)
        {
            return i;
        }
    }
    palettes.push_back(palette);
    return palettes.size() - 1;
}

std::vector<sprite_block> extract_8x8_blocks(const std::vector<uint8_t> &pixel_indices,
                                             int width, int height)
{
    int blocks_w = width / 8;
    int blocks_h = height / 8;
    std::vector<sprite_block> blocks(blocks_w * blocks_h);

    for (int by = 0; by < blocks_h; ++by)
    {
        for (int bx = 0; bx < blocks_w; ++bx)
        {
            sprite_block &block = blocks[by * blocks_w + bx];

            for (int py = 0; py < 8; ++py)
            {
                for (int px = 0; px < 8; px += 2)
                {
                    int src_x = bx * 8 + px;
                    int src_y = by * 8 + py;

                    uint8_t idx1 = pixel_indices[src_y * width + src_x];     // even pixel (x)
                    uint8_t idx2 = pixel_indices[src_y * width + src_x + 1]; // odd pixel (x+1)

                    // Pack: even pixel in low nibble, odd pixel in high nibble
                    int byte_index = py * 4 + px / 2;
                    block.tile_data[byte_index] = (idx2 << 4) | (idx1 & 0x0F);
                }
            }
        }
    }

    return blocks;
}

int main(int argc, char **argv)
{
    std::filesystem::path working_dir = std::filesystem::current_path();
    if (argc > 1)
        working_dir = argv[1];
    std::filesystem::current_path(working_dir);

    const std::filesystem::path base = std::filesystem::current_path();
    const std::filesystem::path bg_dir = base / "bg";
    const std::filesystem::path sprites_dir = base / "sprites";

    std::vector<std::string> bg_files;
    std::vector<std::string> sprite_files;
    if (std::filesystem::is_directory(bg_dir))
    {
        for (const auto &entry : std::filesystem::directory_iterator(bg_dir))
        {
            if (entry.is_regular_file())
                bg_files.push_back(entry.path().string());
        }
    }
    else
    {
        std::cerr << "Missing bg directory: " << bg_dir.string() << "\n";
    }

    if (std::filesystem::is_directory(sprites_dir))
    {
        for (const auto &entry : std::filesystem::directory_iterator(sprites_dir))
        {
            if (entry.is_regular_file())
                sprite_files.push_back(entry.path().string());
        }
    }
    else
    {
        std::cerr << "Missing sprites directory: " << sprites_dir.string() << "\n";
    }

    std::vector<pal16> pal16_vector;
    std::vector<pal256> pal256_vector;
    std::vector<sprite_data> all_sprites;

    nlohmann::json assets_json;

    for (const auto &file : sprite_files)
    {
        if (file.find(".bmp") == std::string::npos)
        {
            std::cerr << "Unsupported sprite file format (only .bmp supported): " << file << "\n";
            continue;
        }

        std::vector<uint8_t> pixel_indices;
        pal16 palette{};
        int width, height;

        if (!load_indexed_bmp(file, pixel_indices, palette, width, height))
        {
            continue;
        }

        std::cout << "Loaded sprite: " << file << " (" << width << "x" << height << ")\n";

        size_t palette_index = find_or_add_palette(pal16_vector, palette);
        std::cout << "  -> Using palette index: " << palette_index;
        if (palette_index == pal16_vector.size() - 1 && pal16_vector.size() > 0)
            std::cout << " (new palette created)";
        else
            std::cout << " (existing palette)";
        std::cout << "\n";

        std::vector<sprite_block> blocks = extract_8x8_blocks(pixel_indices, width, height);

        uint16_t blocks_w = width / 8;
        uint16_t blocks_h = height / 8;
        std::cout << "  -> Extracted " << blocks.size() << " tiles (" << blocks_w << "x" << blocks_h << " tiles)\n";

        sprite_data spr;
        spr.name = std::filesystem::path(file).stem().string();
        spr.width = static_cast<uint16_t>(width);
        spr.height = static_cast<uint16_t>(height);
        spr.blocks_w = blocks_w;
        spr.blocks_h = blocks_h;
        spr.palette_index = palette_index;
        spr.blocks = std::move(blocks);

        nlohmann::json spr_json;
        spr_json["name"] = spr.name;
        spr_json["width"] = spr.width;
        spr_json["height"] = spr.height;
        spr_json["blocks_w"] = spr.blocks_w;
        spr_json["blocks_h"] = spr.blocks_h;
        spr_json["palette_index"] = spr.palette_index;
        spr_json["num_tiles"] = spr.blocks.size();
        for (size_t b = 0; b < spr.blocks.size(); ++b)
        {
            nlohmann::json block_json;
            for (size_t i = 0; i < 32; ++i)
            {
                block_json["data"].push_back(spr.blocks[b].tile_data[i]);
            }
            spr_json["tiles"].push_back(block_json);
        }
        assets_json["sprites"].push_back(spr_json);

        all_sprites.push_back(std::move(spr));
    }

    nlohmann::json pal_json;
    for (size_t i = 0; i < pal16_vector.size(); ++i)
    {
        nlohmann::json pjson;
        for (size_t c = 0; c < pal16_vector[i].color_count; ++c)
        {
            nlohmann::json cjson;
            cjson["r"] = pal16_vector[i].colors[c].r;
            cjson["g"] = pal16_vector[i].colors[c].g;
            cjson["b"] = pal16_vector[i].colors[c].b;
            pjson["colors"].push_back(cjson);
        }
        assets_json["palettes_16"].push_back(pjson);
    }
    assets_json["pal16"] = pal_json;
    std::ofstream json_file("assets.json");
    if (!json_file)
    {
        std::cerr << "Cannot create assets.json file\n";
        return 1;
    }
    json_file << assets_json.dump(4);
    json_file.close();
    std::cout << "Assets JSON written to assets.json\n";

    std::cout << "\n=== Summary ===\n";
    std::cout << "Total palettes: " << pal16_vector.size() << "\n";
    std::cout << "Total sprites: " << all_sprites.size() << "\n";

    for (size_t i = 0; i < pal16_vector.size(); ++i)
    {
        std::cout << "Palette " << i << ": " << pal16_vector[i].color_count << " colors\n";
        for (size_t c = 0; c < pal16_vector[i].color_count; ++c)
        {
            const auto &col = pal16_vector[i].colors[c];
            std::cout << "  [" << c << "] RGB(" << (int)col.r << ", " << (int)col.g << ", " << (int)col.b << ")\n";
        }
    }

    std::cout << "\nBackground files found:\n";
    for (const auto &file : bg_files)
        std::cout << " - " << file << "\n";

    std::cout << "Sprite files processed:\n";
    for (const auto &spr : all_sprites)
    {
        std::cout << " - " << spr.name << " (" << spr.width << "x" << spr.height
                  << ", " << spr.blocks.size() << " tiles, palette " << spr.palette_index << ")\n";
    }

    return 0;
}
