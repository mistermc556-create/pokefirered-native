#!/usr/bin/env python3
"""Validate the linked Route 1 table and 1024 real wild-Pokemon generations."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    if shutil.which("gdb") is None:
        raise SystemExit("GDB is required for the native encounter integration test")
    with tempfile.TemporaryDirectory(prefix="firered-encounters-") as temporary:
        directory = Path(temporary)
        script = directory / "test.gdb"
        script.write_text("""set debuginfod enabled off
set pagination off
break VBlankIntrWait if sFrames == 7000
run
set $result = (int)NativePcTestRoute1Encounters()
printf "ROUTE1_RESULT=%d\\n", $result
""")
        environment = os.environ.copy()
        environment.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                           FIRERED_UNTHROTTLED="1", FIRERED_SMOKE_INPUT="1",
                           FIRERED_SAVE_PATH=str(directory / "test.sav"))
        for name in ["FIRERED_CAPTURE", "FIRERED_AUDIO_CAPTURE", "FIRERED_BACKEND_STATS"]:
            environment.pop(name, None)
        result = subprocess.run(["gdb", "-q", "-batch", "-x", str(script), "--args",
                                 str(ROOT / "build/pc/pokefirered"), "--frames", "7100"],
                                env=environment, text=True, capture_output=True, timeout=120)
        if result.returncode or "ROUTE1_RESULT=0" not in result.stdout:
            raise SystemExit(f"FAIL: Route 1 encounters\n{result.stdout}\n{result.stderr}")
        print("PASS: 12 canonical Route 1 slots; 1024 generated encounters match species and levels")


if __name__ == "__main__":
    main()