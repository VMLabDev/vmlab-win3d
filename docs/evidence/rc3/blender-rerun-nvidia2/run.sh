#!/usr/bin/env bash
# Blender as tests/e2e/apps.sh runs it, every capture kept. $1 lab dir, $2 out dir, $3 esc frame (0 = none)
LAB=$1 OUT=$2 ESC=${3:-8}; mkdir -p $OUT; n=0
(cd $LAB && vmlab exec --timeout 490 win -- powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& C:\\win3d\\guest\\Run-Probe.ps1 -Path 'C:\\apps\\blender-4.2.3-windows-x64\\blender.exe' -ArgumentList '--factory-startup' -Raw -Name blender-$ESC -OutDir C:\\win3d\\results\\apps -Seconds 90 -TimeoutSec 90; exit \$LASTEXITCODE") > $OUT/run.out 2>&1 &
pid=$!
while kill -0 $pid 2>/dev/null; do sleep 4; n=$((n+1)); [ "$n" = "$ESC" ] && (cd $LAB && vmlab vm sendkeys win esc) && echo "esc at $n"; (cd $LAB && vmlab vm screenshot win $OUT/f$(printf %03d $n).png >/dev/null 2>&1); done
wait $pid; echo rc=$?
