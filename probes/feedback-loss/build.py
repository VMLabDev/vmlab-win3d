#!/usr/bin/env python3
"""Link a Windows fault-injection probe against the materialized Mesa build."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess


def build(work: Path, output: Path, arch: str) -> None:
    directory = work / f"build-{arch}"
    commands = json.loads((directory / "compile_commands.json").read_text())
    command = next(item for item in commands if item["file"].endswith("yttrium_venus2_batch.c"))
    arguments = iter(shlex.split(command["command"]))
    compiler = []
    for argument in arguments:
        if argument in ("-MQ", "-MF", "-o", "-c"):
            next(arguments)
        elif argument != "-MD":
            compiler.append(argument)
    target = output / arch
    target.mkdir(parents=True, exist_ok=True)
    object_file = target / "feedback_loss_probe.obj"
    source = Path(__file__).with_name("feedback_loss_probe.c")
    subprocess.run(compiler + ["-c", str(source), "-o", str(object_file)], cwd=directory, check=True)

    dll = "src/gallium/targets/d3d10umd/viogpu_d3d10.dll"
    link = subprocess.check_output(["ninja", "-t", "commands", dll], cwd=directory, text=True).splitlines()[-1]
    arguments = iter(shlex.split(link))
    linker = []
    for argument in arguments:
        if argument == "-o":
            next(arguments)
            linker.extend(["-o", str(target / "feedback_loss_probe.exe")])
        elif argument.endswith("d3d10_gdi.c.obj"):
            linker.extend([argument, str(object_file)])
        elif argument == "-shared" or argument.endswith(".def") or argument.startswith("-Wl,--out-implib="):
            continue
        else:
            linker.append(argument)
    subprocess.run(linker, cwd=directory, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()
    for arch in ("x64", "x86"):
        build(arguments.work.resolve(), arguments.out.resolve(), arch)


if __name__ == "__main__":
    main()
