#pragma once
#include <cstdint>

// Shared PPU globals (declared once in VirtuaPPU.hpp)
extern uint32_t frame_buffer[]; // RGBA8888 framebuffer
extern uint8_t VRAM[];          // VRAM backing store

struct PPUMemory
{
    uint16_t frame_width;
    uint8_t mode;
};

extern PPUMemory global_Registers;
