#!/usr/bin/env python3
"""Smoke-test the actual native game, using temporary saves and dummy devices."""
import os
import array
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    binary = ROOT / "build/pc/pokefirered"
    if not binary.is_file():
        raise SystemExit("Build the game first: Makefile.native game")
    with tempfile.TemporaryDirectory(prefix="firered-smoke-") as temporary:
        directory = Path(temporary)
        environment = os.environ.copy()
        environment.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                           FIRERED_SAVE_PATH=str(directory / "test.sav"),
                           FIRERED_CAPTURE=str(directory / "frame.bmp"),
                           FIRERED_AUDIO_CAPTURE=str(directory / "audio.pcm"),
                           FIRERED_BACKEND_STATS=str(directory / "backend.json"),
                           FIRERED_UNTHROTTLED="1", FIRERED_SMOKE_INPUT="1")
        keyboard_environment = environment.copy()
        keyboard_environment.pop("FIRERED_SMOKE_INPUT", None)
        subprocess.run([str(binary), "--test-keyboard"], env=keyboard_environment,
                       check=True, timeout=10)
        subprocess.run([str(binary), "--test-video"], env=keyboard_environment,
                       check=True, timeout=10)
        subprocess.run([str(binary), "--frames", "7000"], env=environment,
                       check=True, timeout=60)
        image = (directory / "frame.bmp").read_bytes()
        if image[:2] != b"BM" or len(image) < 240 * 160 * 3:
            raise SystemExit("FAIL: missing or invalid rendered game frame")
        stats = json.loads((directory / "backend.json").read_text())
        if stats["audio_batches"] != stats["frames"] or stats["audio_nonzero"] == 0:
            raise SystemExit("FAIL: audio submission is missing, silent, or not once per frame")
        if stats["audio_device"] == 0 or stats["key_events"] == 0:
            raise SystemExit("FAIL: no audio device or no keyboard events processed")
        samples = array.array("h")
        samples.frombytes((directory / "audio.pcm").read_bytes())
        if len(samples) != stats["audio_samples"] or not any(samples):
            raise SystemExit("FAIL: missing or silent PCM capture")
        bad = directory / "invalid.sav"
        bad.write_bytes(b"not a valid save")
        environment["FIRERED_SAVE_PATH"] = str(bad)
        result = subprocess.run([str(binary), "--frames", "1"], env=environment,
                                capture_output=True, timeout=10)
        if result.returncode == 0 or bad.read_bytes() != b"not a valid save":
            raise SystemExit("FAIL: invalid save was accepted or modified")
        print("PASS: 7000 game frames, SDL keyboard events, non-silent PCM, once-per-frame audio, invalid-save protection")
        print("This does not establish complete gameplay or save round-trip correctness.")


if __name__ == "__main__":
    main()