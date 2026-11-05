#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include "global.h"

// FireRed has no real-time clock (no siirtc.h); the RTC entry points of the
// platform layer keep their signatures over an opaque struct.
struct SiiRtcInfo;

void Platform_StoreSaveFile(void);
void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size);
/* Interleaved stereo in fixed point, 1 << 23 to full scale; the count is in bytes. */
void Platform_QueueAudio(s32 *audioBuffer, s32 samplesPerFrame);
u16 Platform_GetKeyInput(void);
void Platform_GetStatus(struct SiiRtcInfo *rtc);
void Platform_SetStatus(struct SiiRtcInfo *rtc);
void Platform_GetDateTime(struct SiiRtcInfo *rtc);
void Platform_SetDateTime(struct SiiRtcInfo *rtc);
void Platform_GetTime(struct SiiRtcInfo *rtc);
void Platform_SetTime(struct SiiRtcInfo *rtc);
void Platform_SetAlarm(u8 *alarmData);

#endif