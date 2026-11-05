# Native FireRed for Fedora — experimental

This executable compiles the existing FireRed C sources directly for x86. No
GBA CPU emulator or ROM execution is involved. Original visuals are retained
for baseline validation; 3D battles are not integrated yet.

## Build and run

```sh
sudo dnf install gcc make python3 glibc-devel.i686 libgcc.i686 sdl2-compat-devel.i686
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native game
/home/mistermc/pokemon_rom_hack/pokefirered/build/pc/pokefirered
```

The game uses 32-bit pointers for its embedded scripts. The existing decomp
preprocessor and generated game assets are prerequisites. This checkout already
has them from its GBA build; for a clean clone, follow INSTALL.md first. The
native build does not download assets or dependencies. Fedora's default i686
SDL2 include/library locations are checked if pkg-config cannot find them.

## Controls

Arrow keys: D-pad. Z/X: A/B. Enter/Backspace: Start/Select. A/S: L/R.
Letter bindings follow the active keyboard layout, not US physical key positions;
on Austrian/German QWERTZ, press the key labeled Z for A, not Y.
Escape: quit. The window is resizable, preserving the 240x160 aspect ratio.
Space also maps to A, Left Shift to B, keypad Enter to Start and Tab to Select.
Click the game window to give it keyboard focus. Key states are cleared when
focus is lost so movement cannot remain stuck after switching windows.

## Saves

The default is SDL's per-user application directory, normally
`~/.local/share/firered-native/firered/firered.sav`. Startup prints the exact
path. The existing project save is not automatically imported or overwritten.
Writes use a temporary file and rename. Invalid-length files are refused.
Override the path with `FIRERED_SAVE_PATH`. Use a **copy** of an existing save
until full save/load compatibility is playtested. Compile-time checks enforce
cartridge save-block sizes and selected offsets, not every save feature.

## Tests

```sh
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native check-game-toolchain
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native test
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native sanitize
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native smoke
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native test-rival
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native test-build
make -f /home/mistermc/pokemon_rom_hack/pokefirered/Makefile.native test-encounters
```

`test` and `sanitize` are the separate foundation suite. `smoke` runs the actual
game using temporary saves and dummy SDL devices; it checks framebuffer output,
keyboard down/hold/up and focus events, non-silent captured PCM, exactly one
audio submission per frame, and malformed-save protection. Sanitizers need
libasan/libubsan runtimes.

`test-rival` requires GDB. It starts the normal game, constructs isolated level-5
starter fixtures, and exercises the real trainer transition and battle engine
for all three starter matchups through a win/loss result. It does not navigate
the complete Oak's Lab story or validate all post-battle field scripts.
The native transition detaches its display callbacks before freeing effect data;
otherwise the next blanking callback dereferences the freed/null transition.
Graphics regressions cover transparent 8bpp layering and affine identity scale;
the latter is initialized at startup as it would be by the cartridge BIOS.

`test-build` checks that edits to generated game-data headers invalidate cached
objects. The native builder regenerates wild encounters from the same JSON and
template as the GBA build, and tracks data under src/data as dependencies.
`test-encounters` uses an isolated game process to check all twelve original
Route 1 slots and 1,024 actual generated Pokemon (Pidgey/Rattata, slot-specific
levels 2–5). It does not edit saves or clamp encounter levels at runtime.

For startup captures, `--frames N` exits after N frames, and `FIRERED_CAPTURE`
names an absolute BMP output path. `FIRERED_UNTHROTTLED=1` disables frame limiting;
`FIRERED_SMOKE_INPUT=1` injects periodic A/Start presses. Leave those unset for
normal play. They do not skip scripts or edit game state directly.
`--test-keyboard` exercises the SDL keyboard event path without starting the
game. `FIRERED_AUDIO_CAPTURE` names a signed 16-bit native-endian stereo PCM
capture at 42060 Hz. `FIRERED_BACKEND_STATS` names a JSON diagnostics output.

## Structure and limitations

- Builder: tools/native/build_pc.py. Output: build/pc/.
- SDL backend: platform/pc/src/desktop.c (display, input, audio, saves, timing).
- Native technical variants live in platform/pc/, outside GBA source collection.
  FIRERED_PC include wrappers select native headers. Most gameplay compiles from
  its original source. Keep variants synchronized with future original fixes.
- External technical code was adapted; see platform/pc/UPSTREAM-NOTICE.md.
- No link cable, wireless networking, or GameCube multiboot support. Soft reset
  currently exits with a restart message, rather than rebooting the game.
- Rendering and timing are approximations; graphical issues may remain.
- Audio is submitted independently of display interrupts. Non-silent mixer
  output and opening Fedora's PipeWire device are tested, but exact audible
  fidelity and the user's speaker routing still require listening tests.
- Boot, intro, new-game dialogue, naming-screen exit and loading the player's
  bedroom are tested (7,000 automated game frames and inspected capture).
  Whole-game progression, movement, every battle type, and full save round trips
  still require playtesting.
- Controlled first-rival battles and normal battle transitions are tested with
  GDB. The full laboratory story sequence still requires manual testing.
- The original GBA build entry point remains; no post-change full ROM checksum
  rebuild has been performed.

This is a runnable experimental port, **not a validated complete port**.
3D battle replacement and a later native 3DS backend remain future work.