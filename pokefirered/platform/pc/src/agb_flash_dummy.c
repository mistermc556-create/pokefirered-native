#include "global.h"
#include "gba/gba.h"
#include "gba/flash_internal.h"
#include <stdio.h>
void NativePcSaveChanged(void);
#ifdef PORT_BRIDGE
#include "port_platform.h"
#endif

const u16 dummyMaxTime[] =
{
      10, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
      10, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
    2000, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
    2000, 65469, TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_256CLK,
};

const struct FlashSetupInfo DUMMY_SAVE =
{
    ProgramFlashByte_DUMMY,
    ProgramFlashSector_DUMMY,
    EraseFlashChip_DUMMY,
    EraseFlashSector_DUMMY,
    WaitForFlashWrite_DUMMY,
    dummyMaxTime,
    {
        131072, // ROM size
        {
            4096, // sector size
              12, // bit shift to multiply by sector size (4096 == 1 << 12)
              32, // number of sectors
               0  // appears to be unused
        },
        { 3, 1 }, // wait state setup data
        { { 0xCC, 0xCC } } // ID
    }
};

u16 WaitForFlashWrite_DUMMY(u8 phase, u8 *addr, u8 lastData)
{
    // stub
    return 0;
}

u16 EraseFlashChip_DUMMY(void)
{
#ifdef PORT_BRIDGE
    u16 sector;
    u16 result = 0;

    if (gFlash == NULL)
        return 1;
    for (sector = 0; sector < gFlash->sector.count; sector++)
        result |= EraseFlashSector_DUMMY(sector);
    return result;
#else
    memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
    NativePcSaveChanged();
    return 0;
#endif
}

u16 EraseFlashSector_DUMMY(u16 sectorNum)
{
    u8 clearBuffer[0x1000];
    memset(clearBuffer, 0xFF, sizeof(clearBuffer));
    return ProgramFlashSector_DUMMY(sectorNum, &clearBuffer[0]);
}

u16 ProgramFlashByte_DUMMY(u16 sectorNum, u32 offset, u8 data)
{
#ifdef PORT_BRIDGE
    if (gFlash == NULL || sectorNum >= gFlash->sector.count || offset >= gFlash->sector.size)
        return 1;
    return Port_WriteFlash((sectorNum << gFlash->sector.shift) + offset, &data, sizeof(data));
#else
    FLASH_BASE[(sectorNum << gFlash->sector.shift) + offset] = data;
    NativePcSaveChanged();
    return 0;
#endif
}


u16 ProgramFlashSector_DUMMY(u16 sectorNum, void *src)
{
#ifdef PORT_BRIDGE
    if (gFlash == NULL || sectorNum >= gFlash->sector.count)
        return 1;
    return Port_WriteFlash(sectorNum << gFlash->sector.shift, src, gFlash->sector.size);
#else
    memcpy(&FLASH_BASE[sectorNum << gFlash->sector.shift], src, 0x1000);
    NativePcSaveChanged();
    return 0;
#endif
}
