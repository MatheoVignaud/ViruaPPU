#pragma once
#include <ModesImpl.hpp>
#include <PPUMemory.hpp>
#include <cstdint>

uint32_t frame_buffer[360 * 1280]; // rgba frame buffer
alignas(64) uint8_t VRAM[4194304]; // 4MB VRAM , layout change for each mode

PPUMemory global_Registers;

void RenderFrame() {
    switch (global_Registers.mode) {
        case 0:
            Mode0::RenderFrame(global_Registers);
            break;
        case 1:
            Mode1::RenderFrame(global_Registers);
            break;
        case 2:
            Mode2::RenderFrame(global_Registers);
            break;
        default:
            break;
    }
}

uint32_t* GetFrameBuffer() {
    return frame_buffer;
}