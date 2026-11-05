#include "global.h"
#include "pokemon_storage_system.h"

_Static_assert(sizeof(void *) == 4, "Full game build requires 32-bit pointers");
_Static_assert(sizeof(struct SaveBlock2) == 0xF24, "SaveBlock2 must retain cartridge layout");
_Static_assert(sizeof(struct SaveBlock1) == 0x3D68, "SaveBlock1 must retain cartridge layout");
_Static_assert(offsetof(struct SaveBlock1, easyChatProfile) == 0x2CA0, "Quest log layout");
_Static_assert(offsetof(struct SaveBlock1, unused_3A94) == 0x3A94, "Fame checker layout");
_Static_assert(sizeof(struct PokemonStorage) == 0x83D0, "Pokemon storage layout");
_Static_assert(sizeof(struct RamScriptData) == 1000, "RAM script layout");