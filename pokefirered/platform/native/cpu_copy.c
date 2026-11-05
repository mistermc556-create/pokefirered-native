#include "gba/types.h"
#include "gba/multiboot.h"
#include "gba/syscall.h"

#include <stddef.h>
#include <string.h>

static void CopyUnits(const void *src, void *dest, size_t count, size_t width, int fixed)
{
    const unsigned char *input = src;
    unsigned char *output = dest;
    unsigned char value[4];
    size_t i;

    if (count == 0)
        return;

    if (fixed)
        memcpy(value, input, width);

    for (i = 0; i < count; i++)
    {
        // Byte copies avoid alignment and strict-aliasing assumptions on hosts.
        if (!fixed)
            memcpy(value, input + i * width, width);
        memcpy(output + i * width, value, width);
    }
}

void CpuSet(const void *src, void *dest, u32 control)
{
    CopyUnits(src, dest, control & 0x1FFFFF,
              (control & CPU_SET_32BIT) ? 4 : 2,
              (control & CPU_SET_SRC_FIXED) != 0);
}

void CpuFastSet(const void *src, void *dest, u32 control)
{
    size_t count = control & 0x1FFFFF;

    // The fast operation transfers complete groups of eight 32-bit words.
    count = (count + 7) & ~(size_t)7;
    CopyUnits(src, dest, count, 4, (control & CPU_FAST_SET_SRC_FIXED) != 0);
}