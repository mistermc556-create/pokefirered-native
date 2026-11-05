#!/usr/bin/env python3
"""Build existing FireRed sources and data as a native 32-bit Linux executable."""

import argparse
import concurrent.futures
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/pc"
PC = ROOT / "platform/pc"


def run(command, **kwargs):
    return subprocess.run(command, cwd=kwargs.pop("cwd", ROOT), check=True, **kwargs)


def shared_dependencies(root):
    """Include generated game data, not just public API headers."""
    paths = [Path(__file__).resolve(), root / "charmap.txt", root / "tools/preproc/preproc"]
    for directory in [root / "include", root / "platform/pc/include", root / "src/data"]:
        paths.extend(path for path in directory.rglob("*") if path.is_file())
    return [path for path in paths if path.is_file()]


def object_is_current(obj, source, dependencies):
    if not obj.is_file():
        return False
    built = obj.stat().st_mtime_ns
    return all(path.is_file() and path.stat().st_mtime_ns <= built
               for path in [source, *dependencies])


def generate_encounters():
    # Same generator and template as the original GBA build. Generate atomically
    # and keep the timestamp when contents are unchanged.
    target = ROOT / "src/data/wild_encounters.h"
    pending = BUILD / "wild_encounters.h.pending"
    run([str(ROOT / "tools/jsonproc/jsonproc"), "src/data/wild_encounters.json",
         "src/data/wild_encounters.json.txt", str(pending)])
    if target.exists() and target.read_bytes() == pending.read_bytes():
        pending.unlink()
    else:
        pending.replace(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-j", "--jobs", type=int, default=min(os.cpu_count() or 2, 8))
    parser.add_argument("--compile-only", action="store_true")
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    generate_encounters()
    cc = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-m32", "-std=gnu11", "-O1", "-g", "-fno-pie", "-fno-strict-aliasing",
             "-fwrapv", "-fcommon", "-ffunction-sections", "-fdata-sections",
             "-Wno-int-conversion", "-Wno-incompatible-pointer-types",
             "-Wno-implicit-function-declaration", "-Wno-pointer-sign",
             "-Wno-address-of-packed-member", "-Wno-multichar",
             "-DFIRERED_PC", "-DPORTABLE", "-DMODERN=1", "-DUBFIX",
             "-DFIRERED", "-DENGLISH", "-DREVISION=0", "-DRENDERER_EASY_DRAW",
             "-iquote", str(PC / "include"), "-iquote", str(ROOT / "include"),
             "-include", str(PC / "include/compat.h")]
    # SDL2 can be installed as Fedora's sdl2-compat development package.
    sdl = subprocess.run(["pkg-config", "--cflags", "--libs", "sdl2"],
                         capture_output=True, text=True)
    if sdl.returncode:
        sdl = subprocess.run(["pkg-config", "--cflags", "--libs", "sdl2-compat"],
                             capture_output=True, text=True)
    sdl_flags = shlex.split(sdl.stdout) if sdl.returncode == 0 else ["-I/usr/include/SDL2", "-lSDL2"]
    cflags = [x for x in sdl_flags if x.startswith(("-I", "-D"))]
    libs = [x for x in sdl_flags if not x.startswith(("-I", "-D"))]
    excluded = {"m4a.c", "m4a_tables.c", "isagbprn.c", "librfu_intr.c", "rom_header_gf.c"}
    sources = [p for p in sorted((ROOT / "src").rglob("*.c"))
               if not p.name.endswith(".inc.c") and "/data/" not in str(p.relative_to(ROOT))]
    replacements = {p.name: p for p in sorted((PC / "src").glob("*.c"))}
    selected = []
    for source in sources:
        if source.name in replacements:
            selected.append(replacements.pop(source.name))
        elif source.name not in excluded:
            selected.append(source)
    selected += list(replacements.values()) + sorted((PC / "src/platform").glob("*.c"))

    def compile_c(source):
        relative = source.relative_to(ROOT)
        obj = BUILD / relative.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        log = obj.with_suffix(".log")
        # Conservatively rebuild on any shared header/generated-data change.
        if object_is_current(obj, source, dependencies):
            return obj
        try:
            preprocessed = run(cc + flags + cflags + ["-E", str(source)], capture_output=True).stdout
            encoded = run([str(ROOT / "tools/preproc/preproc"), "-i", str(source), "charmap.txt"],
                          input=preprocessed, capture_output=True).stdout
            result = run(cc + flags + cflags + ["-x", "cpp-output", "-c", "-", "-o", str(obj)],
                         input=encoded, capture_output=True)
            log.write_bytes(result.stderr)
        except subprocess.CalledProcessError as error:
            obj.unlink(missing_ok=True)
            log.write_bytes(error.stderr or b"")
            raise RuntimeError(f"{relative}: {log}") from error
        return obj

    dependencies = shared_dependencies(ROOT)
    objects, failures = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        tasks = {pool.submit(compile_c, p): p for p in selected}
        for task in concurrent.futures.as_completed(tasks):
            try:
                objects.append(task.result())
            except Exception as error:
                failures.append(str(error))
                print("FAILED", error, flush=True)
    print(f"C units compiled: {len(objects)}/{len(selected)}", flush=True)
    if failures:
        return 1
    if args.compile_only:
        return 0

    # Data assembly carries scripts, text, map and sound data, not ARM code.
    def convert_assembly(text):
        text = text.replace(".4byte", ".long").replace(".2byte", ".short")
        text = re.sub(r"\.word\b", ".long", text)
        text = re.sub(r"\.hword\b", ".short", text)
        text = re.sub(r"(?m)^(\s*)\.align\s+(\d+)", lambda m: m[1] + ".balign " + str(1 << int(m[2])), text)
        text = re.sub(r"(?m)(?<!\\)@.*$", "", text)
        # x86 GAS treats whitespace as a macro argument separator, unlike ARM
        # GAS. Compact arithmetic outside quoted strings in macro invocations.
        lines = []
        for line in text.splitlines():
            if '"' not in line:
                line = re.sub(r"\s*([+*/-])\s*", r"\1", line)
                line = re.sub(r"\(\s+", "(", line)
                line = re.sub(r"\s+\)", ")", line)
            lines.append(line)
        return "\n".join(lines) + "\n"

    for directory in ["asm", "constants", "data", "sound"]:
        for source in (ROOT / directory).rglob("*.inc"):
            target = BUILD / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            original = source.read_text()
            if source.relative_to(ROOT).as_posix() == "sound/music_player_table.inc":
                begin = original.index("gMPlayTrack_BGM::")
                end = original.index("\t.section .rodata", begin)
                original = original[:begin] + original[end:]
            target.write_text(convert_assembly(original))
    assembly = list((ROOT / "data").glob("*.s")) + list((ROOT / "sound/songs").glob("*.s"))
    assembly += list((ROOT / "sound/songs/midi").glob("*.s"))
    (BUILD / "sound/MPlayDef.s").write_text(convert_assembly((ROOT / "sound/MPlayDef.s").read_text()))
    for source in assembly:
        obj = BUILD / source.relative_to(ROOT).with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        if "sound/songs" in source.as_posix():
            text = convert_assembly(source.read_text())
            converted = BUILD / source.relative_to(ROOT)
            converted.write_text(text)
            run(["as", "--32", "-I", str(BUILD / "sound"), "-I", str(ROOT / "sound"), "-o", str(obj), str(converted)], cwd=BUILD)
            objects.append(obj)
            continue
        raw = run([str(ROOT / "tools/preproc/preproc"), str(source), "charmap.txt"], capture_output=True).stdout
        raw = run(cc + ["-E", "-P", "-x", "assembler-with-cpp", "-Iinclude", "-"], input=raw, capture_output=True).stdout
        raw = run([str(ROOT / "tools/preproc/preproc"), "-ie", str(source), "charmap.txt"], input=raw, capture_output=True).stdout
        text = convert_assembly(raw.decode())
        if source.name == "sound_data.s":
            for track in ["BGM", "SE1", "SE2", "SE3"]:
                text = re.sub(r"(?m)^gMPlayTrack_" + track + r":.*?^\s*\.size\s+gMPlayTrack_" + track + r"[^\n]*\n",
                              "", text, flags=re.S)
        converted = BUILD / source.relative_to(ROOT)
        converted.write_text(text)
        run(["as", "--32", "-I", str(BUILD), "-I", str(ROOT), "-I", str(ROOT / "sound"),
             "--defsym", "MODERN=1", "--defsym", "FIRERED=1", "--defsym", "REVISION=0",
             "--defsym", "PORTABLE=1", "--defsym", "ENGLISH=1", "-o", str(obj), str(converted)], cwd=BUILD)
        objects.append(obj)
    # Thread completion order must not change executable layout/debug addresses.
    # Keep a matching debug executable for future systemd core-dump diagnosis.
    objects.sort()
    pending = BUILD / "pokefirered.pending"
    run(cc + ["-m32", "-no-pie", "-Wl,--gc-sections", "-Wl,-z,noexecstack",
              "-Wl,--undefined=NativePcTestRoute1Encounters"] +
        [str(p) for p in objects] + libs + ["-lm", "-o", str(pending)])
    binary = BUILD / "pokefirered"
    if binary.exists():
        import hashlib
        import shutil
        build_id = hashlib.sha256(binary.read_bytes()).hexdigest()[:16]
        archive = BUILD / "debug-history"
        archive.mkdir(exist_ok=True)
        saved = archive / ("pokefirered-" + build_id)
        if not saved.exists():
            shutil.copy2(binary, saved)
    pending.replace(binary)
    print("Built", BUILD / "pokefirered")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        if error.stderr:
            sys.stderr.buffer.write(error.stderr)
        raise SystemExit(error.returncode)