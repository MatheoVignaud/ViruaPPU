#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

// Convertir un nom en identifiant C valide (majuscules, underscores)
std::string to_c_identifier(const std::string &name)
{
    std::string result;
    for (char c : name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        else
            result += '_';
    }
    // S'assurer que ça ne commence pas par un chiffre
    if (!result.empty() && std::isdigit(static_cast<unsigned char>(result[0])))
        result = "_" + result;
    return result;
}

int main(int argc, char **argv)
{
    std::filesystem::path json_path = "assets.json";
    std::filesystem::path output_path = "assets.h";

    if (argc > 1)
        json_path = argv[1];
    if (argc > 2)
        output_path = argv[2];

    // Lire le fichier JSON
    std::ifstream json_file(json_path);
    if (!json_file)
    {
        std::cerr << "Cannot open JSON file: " << json_path << "\n";
        return 1;
    }

    nlohmann::json assets;
    try
    {
        json_file >> assets;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::cerr << "JSON parse error: " << e.what() << "\n";
        return 1;
    }
    json_file.close();

    // Créer le fichier header
    std::ofstream header(output_path);
    if (!header)
    {
        std::cerr << "Cannot create header file: " << output_path << "\n";
        return 1;
    }

    // Guard macro
    std::string guard_name = to_c_identifier(output_path.stem().string()) + "_H";

    header << "/**\n";
    header << " * Auto-generated asset header file\n";
    header << " * Generated from: " << json_path.filename().string() << "\n";
    header << " */\n\n";
    header << "#ifndef " << guard_name << "\n";
    header << "#define " << guard_name << "\n\n";
    header << "#include <stdint.h>\n\n";

    // ========== Structures ==========
    header << "/* ========== Structures ========== */\n\n";

    header << "typedef struct {\n";
    header << "    uint8_t r;\n";
    header << "    uint8_t g;\n";
    header << "    uint8_t b;\n";
    header << "} rgb888_t;\n\n";

    header << "typedef struct {\n";
    header << "    rgb888_t colors[16];\n";
    header << "    uint8_t color_count;\n";
    header << "} palette16_t;\n\n";

    header << "typedef struct {\n";
    header << "    uint8_t data[32]; /* 8x8 pixels, 4bpp */\n";
    header << "} tile_t;\n\n";

    header << "typedef struct {\n";
    header << "    const char* name;\n";
    header << "    uint16_t width;\n";
    header << "    uint16_t height;\n";
    header << "    uint16_t tiles_w;\n";
    header << "    uint16_t tiles_h;\n";
    header << "    uint8_t palette_index;\n";
    header << "    uint16_t num_tiles;\n";
    header << "    const tile_t* tiles;\n";
    header << "} sprite_t;\n\n";

    // ========== Palettes ==========
    if (assets.contains("palettes_16") && assets["palettes_16"].is_array())
    {
        const auto &palettes = assets["palettes_16"];
        size_t num_palettes = palettes.size();

        header << "/* ========== Palettes (16 colors) ========== */\n\n";
        header << "#define NUM_PALETTES_16 " << num_palettes << "\n\n";

        for (size_t p = 0; p < num_palettes; ++p)
        {
            const auto &pal = palettes[p];
            const auto &colors = pal["colors"];
            size_t color_count = colors.size();

            header << "static const palette16_t PALETTE_" << p << " = {\n";
            header << "    .colors = {\n";

            for (size_t c = 0; c < 16; ++c)
            {
                if (c < color_count)
                {
                    uint8_t r = colors[c]["r"].get<uint8_t>();
                    uint8_t g = colors[c]["g"].get<uint8_t>();
                    uint8_t b = colors[c]["b"].get<uint8_t>();
                    header << "        {" << (int)r << ", " << (int)g << ", " << (int)b << "}";
                }
                else
                {
                    header << "        {0, 0, 0}";
                }
                if (c < 15)
                    header << ",";
                header << "\n";
            }

            header << "    },\n";
            header << "    .color_count = " << color_count << "\n";
            header << "};\n\n";
        }

        // Tableau de toutes les palettes
        header << "static const palette16_t* const PALETTES_16[NUM_PALETTES_16] = {\n";
        for (size_t p = 0; p < num_palettes; ++p)
        {
            header << "    &PALETTE_" << p;
            if (p < num_palettes - 1)
                header << ",";
            header << "\n";
        }
        header << "};\n\n";
    }

    // ========== Sprites ==========
    if (assets.contains("sprites") && assets["sprites"].is_array())
    {
        const auto &sprites = assets["sprites"];
        size_t num_sprites = sprites.size();

        header << "/* ========== Sprites ========== */\n\n";
        header << "#define NUM_SPRITES " << num_sprites << "\n\n";

        // D'abord, générer les données de tiles pour chaque sprite
        for (size_t s = 0; s < num_sprites; ++s)
        {
            const auto &spr = sprites[s];
            std::string name = spr["name"].get<std::string>();
            std::string c_name = to_c_identifier(name);

            if (spr.contains("tiles") && spr["tiles"].is_array())
            {
                const auto &tiles = spr["tiles"];
                size_t num_tiles = tiles.size();

                header << "static const tile_t " << c_name << "_TILES[" << num_tiles << "] = {\n";

                for (size_t t = 0; t < num_tiles; ++t)
                {
                    const auto &tile = tiles[t];
                    const auto &data = tile["data"];

                    header << "    {{ /* Tile " << t << " */\n        ";
                    for (size_t i = 0; i < 32; ++i)
                    {
                        header << "0x" << std::hex << std::setw(2) << std::setfill('0')
                               << data[i].get<int>() << std::dec;
                        if (i < 31)
                            header << ", ";
                        if ((i + 1) % 8 == 0 && i < 31)
                            header << "\n        ";
                    }
                    header << "\n    }}";
                    if (t < num_tiles - 1)
                        header << ",";
                    header << "\n";
                }

                header << "};\n\n";
            }
        }

        // Ensuite, générer les structures sprite
        for (size_t s = 0; s < num_sprites; ++s)
        {
            const auto &spr = sprites[s];
            std::string name = spr["name"].get<std::string>();
            std::string c_name = to_c_identifier(name);

            uint16_t width = spr["width"].get<uint16_t>();
            uint16_t height = spr["height"].get<uint16_t>();
            uint16_t blocks_w = spr["blocks_w"].get<uint16_t>();
            uint16_t blocks_h = spr["blocks_h"].get<uint16_t>();
            uint8_t palette_index = spr["palette_index"].get<uint8_t>();
            size_t num_tiles = spr.contains("tiles") ? spr["tiles"].size() : 0;

            header << "static const sprite_t SPRITE_" << c_name << " = {\n";
            header << "    .name = \"" << name << "\",\n";
            header << "    .width = " << width << ",\n";
            header << "    .height = " << height << ",\n";
            header << "    .tiles_w = " << blocks_w << ",\n";
            header << "    .tiles_h = " << blocks_h << ",\n";
            header << "    .palette_index = " << (int)palette_index << ",\n";
            header << "    .num_tiles = " << num_tiles << ",\n";
            header << "    .tiles = " << c_name << "_TILES\n";
            header << "};\n\n";
        }

        // Tableau de tous les sprites
        header << "static const sprite_t* const SPRITES[NUM_SPRITES] = {\n";
        for (size_t s = 0; s < num_sprites; ++s)
        {
            const auto &spr = sprites[s];
            std::string name = spr["name"].get<std::string>();
            std::string c_name = to_c_identifier(name);

            header << "    &SPRITE_" << c_name;
            if (s < num_sprites - 1)
                header << ",";
            header << "\n";
        }
        header << "};\n\n";

        // Indices pour accéder aux sprites par nom
        header << "/* Sprite indices */\n";
        for (size_t s = 0; s < num_sprites; ++s)
        {
            const auto &spr = sprites[s];
            std::string name = spr["name"].get<std::string>();
            std::string c_name = to_c_identifier(name);
            header << "#define SPRITE_IDX_" << c_name << " " << s << "\n";
        }
        header << "\n";
    }

    header << "#endif /* " << guard_name << " */\n";
    header.close();

    std::cout << "Header generated: " << output_path << "\n";

    return 0;
}
