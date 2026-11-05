# Native FireRed bring-up

**Update:** a native desktop game target now exists. See `fedora-pc.md` in this
directory for launch instructions and current limitations. The sections below
describe the separate initial foundation test target, not the desktop game.

This work ports this repository's FireRed sources directly. It does not execute
a GBA ROM or bundle a CPU emulator. The canceled sibling project is not used.
PC is the first gameplay target; native 3DS is a later target.

## Current implementation

`Makefile.native` builds a headless foundation test executable using the host C
compiler. It links the original, unchanged `src/random.c` alongside new portable
CPU copy operations, bounded LZ77/RL asset decoders, and graphics-memory storage.
`FIRERED_NATIVE` redirects palette, VRAM, OAM and register addresses to aligned
host storage. It has no effect on the ordinary GBA build when undefined.

This is **not a playable PC port**. Register storage does not implement register
side effects, DMA, interrupts, display timing, or rendering. The decoders expose
explicit bounds; original unbounded BIOS decoder entry points are not wired up
yet. Other original hardware addresses remain and must not be executed on PC.

The new implementation files live in `platform/`, outside the original GBA
Makefile's recursive `src/` collection. The original build entry point is intact.
Native includes deliberately use `-iquote`, not `-I`: the game's `strings.h`
must not shadow the host C library's `<strings.h>`. Native builds use the C
library's `abs` rather than redefining it ahead of host library declarations.

## Build and test

From any directory:

```sh
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native test
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native sanitize
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native check-game-toolchain
```

The tests need only Make and a C11-capable compiler. Sanitizer tests require
AddressSanitizer and UndefinedBehaviorSanitizer support. They cover copy/fill
behavior, graphics address isolation, valid and malformed compressed streams,
and fixed RNG vectors. Outputs remain in ignored `build/` subdirectories.

The diagnostic checks the proposed 32-bit Linux game build's C runtime and SDL2
linkage without installing packages. SDL2 is still a proposed game backend, not
a dependency of these foundation tests. A successful diagnostic does not imply
the game target has been implemented. At initial inspection this machine lacked
32-bit C startup/runtime objects and discoverable SDL2 development files.
The initial sanitizer build also lacked the installed ASan/UBSan runtime shared
libraries, so sanitizer execution could not be verified on this host.

## Next acceptance gates

1. Resolve the 32-bit toolchain; use it for the first full-game bring-up.
2. Port native startup/frame scheduling and replace remaining ARM-only routines.
3. Adapt asset preprocessing and assembler data with tested 32-bit relocations.
   Audit pointer casts, script readers, layout, alignment and save serialization.
4. Implement graphics, DMA, input, audio and save-file backends; preserve original
   overworld behavior. Never write tests or diagnostics into the user's game save.
5. Boot, navigate menus, walk, save/load, and finish an original-style battle.
6. Introduce an internal battle-presentation interface while keeping damage,
   RNG, AI, catching and progression in the existing FireRed code.
7. Integrate verified models/animation and measured X/Y camera references.
8. Add a separate 3DS backend sharing presentation state and asset metadata.

The isolated tests may run on 64-bit hosts because they do not execute pointer-
bearing game scripts. They do not establish whole-game 64-bit compatibility.
Do not widen serialized 32-bit fields or game data indiscriminately.

## External research

The Emerald multiplatform port was inspected for architecture only; no external
implementation has been copied into this foundation. Asset extraction and
licensing/provenance validation remain separate, unfinished work. No complete
X/Y model/animation pack has been imported or claimed verified.