#!/usr/bin/env python3
"""Run actual Windows feedback fault probes in a private Wine prefix."""

import argparse
import json
import os
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binaries", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--expect-crash", action="store_true")
    arguments = parser.parse_args()
    arguments.out.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ, WINEPREFIX=str(arguments.prefix.resolve()),
                       WINEDEBUG="-all", WINEDLLOVERRIDES="winemenubuilder.exe=d")
    results = []
    try:
        for arch in ("x64", "x86"):
            for mode in ("control", "read-revoked", "write-revoked", "read-unmapped", "write-unmapped"):
                probe = arguments.binaries / arch / "feedback_loss_probe.exe"
                result = subprocess.run(["timeout", "--kill-after=5s", "45s", "wine", str(probe.resolve()), f"--{mode}"],
                                        env=environment, capture_output=True, timeout=55)
                stem = arguments.out / f"{arch}-{mode}"
                stem.with_suffix(".stdout").write_bytes(result.stdout)
                stem.with_suffix(".stderr").write_bytes(result.stderr)
                records = [json.loads(line) for line in result.stdout.decode(errors="replace").splitlines()
                           if line.startswith("{")]
                final = next((record for record in records if "vk_result" in record), None)
                crash_expected = arguments.expect_crash and mode != "control"
                if crash_expected:
                    passed = result.returncode == 5 and final is None and any(record.get("phase") == "access" for record in records)
                else:
                    passed = result.returncode == 0 and final is not None and final["result"] == "pass"
                summary = dict(arch=arch, mode=mode, exit_code=result.returncode,
                               crash_expected=crash_expected, passed=passed, report=final)
                results.append(summary)
                print(json.dumps(summary), flush=True)
    finally:
        subprocess.run(["wineserver", "-k"], env=environment, check=False, timeout=10)
        (arguments.out / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
    if len(results) != 10 or not all(result["passed"] for result in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
