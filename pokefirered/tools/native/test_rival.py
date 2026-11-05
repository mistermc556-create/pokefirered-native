#!/usr/bin/env python3
"""Exercise real trainer transitions and rival battles through GDB, with isolated saves.

This initializes a controlled party after normal startup; it does not navigate
the entire Oak's Lab story or prove every field-script continuation correct.
"""

import argparse
import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run_case(species, trainer):
    with tempfile.TemporaryDirectory(prefix=f"firered-rival-{species}-") as temporary:
        directory = Path(temporary)
        script = directory / "test.gdb"
        script.write_text(f"""set debuginfod enabled off
set pagination off
break VBlankIntrWait if sFrames == 7000
run
call ZeroPlayerPartyMons()
call ZeroEnemyPartyMons()
call CreateMon(&gPlayerParty[0], {species}, 5, 20, 1, 12345, 0, 0)
set gPlayerPartyCount = 1
set gTrainerBattleOpponent_A = {trainer}
set sTrainerBattleMode = 9
set sRivalBattleFlags = 3
call StartTrainerBattle()
disable 1
break ReturnFromBattleToOverworld
continue
if gBattleOutcome == 1 || gBattleOutcome == 2
printf "RIVAL_COMPLETE outcome=%d frames=%lu\\n", gBattleOutcome, sFrames
end
""")
        environment = os.environ.copy()
        environment.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                           FIRERED_UNTHROTTLED="1", FIRERED_SMOKE_INPUT="1",
                           FIRERED_SAVE_PATH=str(directory / "test.sav"))
        # Do not inherit diagnostic paths pointing at the developer's files.
        for name in ["FIRERED_CAPTURE", "FIRERED_AUDIO_CAPTURE", "FIRERED_BACKEND_STATS"]:
            environment.pop(name, None)
        result = subprocess.run(["gdb", "-q", "-batch", "-x", str(script), "--args",
                                 str(ROOT / "build/pc/pokefirered"), "--frames", "20000"],
                                env=environment, text=True, capture_output=True, timeout=150)
        if result.returncode or "RIVAL_COMPLETE outcome=" not in result.stdout:
            raise RuntimeError(f"Starter {species} failed:\n{result.stdout}\n{result.stderr}")
        return next(line for line in result.stdout.splitlines() if line.startswith("RIVAL_COMPLETE"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--starter", type=int, choices=[1, 4, 7], help="default: all three starters")
    args = parser.parse_args()
    if shutil.which("gdb") is None:
        raise SystemExit("GDB is required for the rival integration test")
    cases = [(1, 326), (4, 327), (7, 328)]
    if args.starter:
        cases = [case for case in cases if case[0] == args.starter]
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(cases)) as pool:
        tasks = {pool.submit(run_case, *case): case[0] for case in cases}
        for task in concurrent.futures.as_completed(tasks):
            print(f"PASS starter={tasks[task]}: {task.result()}", flush=True)


if __name__ == "__main__":
    main()