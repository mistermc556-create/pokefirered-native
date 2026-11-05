#!/usr/bin/env python3
"""Check prerequisites for the proposed 32-bit Linux game target, without installing anything."""

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    if not compiler or shutil.which(compiler[0]) is None:
        print("FAIL: C compiler not found")
        return 1

    missing = False
    with tempfile.TemporaryDirectory(prefix="firered-toolchain-") as directory:
        output = Path(directory) / "probe"
        command = compiler + ["-m32", "-x", "c", "-", "-o", str(output)]
        result = subprocess.run(command, input="int main(void) { return 0; }\n",
                                text=True, capture_output=True, check=False)
        if result.returncode:
            print("FAIL: 32-bit C executable cannot be linked (multilib compiler/runtime required)")
            print(result.stderr.strip())
            missing = True
        else:
            print("PASS: 32-bit C compiler and runtime link")

        if shutil.which("pkg-config") is None:
            print("FAIL: pkg-config not found; SDL2 development files cannot be checked")
            missing = True
        else:
            flags = subprocess.run(["pkg-config", "--cflags", "--libs", "sdl2"],
                                   text=True, capture_output=True, check=False)
            if flags.returncode:
                flags = subprocess.run(["pkg-config", "--cflags", "--libs", "sdl2-compat"],
                                       text=True, capture_output=True, check=False)
            if flags.returncode:
                print("INFO: Checking Fedora's default SDL2 include/library locations directly")
                sdl_flags = ["-I/usr/include/SDL2", "-lSDL2"]
            else:
                sdl_flags = shlex.split(flags.stdout)
            if result.returncode == 0:
                command = compiler + ["-m32", "-x", "c", "-", "-o", str(output)]
                command += sdl_flags
                source = "#include <SDL.h>\nint main(void) { return SDL_Init(0); }\n"
                linked = subprocess.run(command, input=source, text=True,
                                        capture_output=True, check=False)
                if linked.returncode:
                    print("FAIL: SDL2 is discoverable but cannot link in a 32-bit executable")
                    print(linked.stderr.strip())
                    missing = True
                else:
                    print("PASS: 32-bit SDL2 linkage")

    print("This checks dependencies only. Build the game with Makefile.native game.")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())