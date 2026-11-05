#include "native/memory.h"

#include <string.h>

struct NativeGraphicsMemory gNativeGraphicsMemory;

void NativeGraphicsMemoryReset(void)
{
    memset(&gNativeGraphicsMemory, 0, sizeof(gNativeGraphicsMemory));
}