#ifndef GUARD_NATIVE_MEMORY_H
#define GUARD_NATIVE_MEMORY_H

#include <stdint.h>

// Storage only: writes do not yet trigger DMA, audio, IRQs, or rendering.
// Keep this outside src/ so the original GBA Makefile cannot collect it.
struct NativeGraphicsMemory
{
    _Alignas(4) uint8_t registers[0x400];
    _Alignas(4) uint8_t palette[0x400];
    _Alignas(4) uint8_t vram[0x18000];
    _Alignas(4) uint8_t oam[0x400];
};

extern struct NativeGraphicsMemory gNativeGraphicsMemory;

void NativeGraphicsMemoryReset(void);

#endif // GUARD_NATIVE_MEMORY_H