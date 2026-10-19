#!/usr/bin/env bash
# Turn full page heap and WER local crash dumps on (or off) for one
# executable in a lab's Windows guest, without gflags: the Image File
# Execution Options values gflags /p /enable <exe> /full writes
# (GlobalFlag 0x02000000 = FLG_HEAP_PAGE_ALLOCS, PageHeapFlags 0x3 = full
# page heap with stack traces), in both registry views, and
# WER LocalDumps for <exe> (mini dumps with heap, DumpType 1 plus the
# custom flags, to C:\win3d\dumps).
#
#   docs/evidence/dwmheap/src/pageheap.sh (copied from docs/evidence/unid3d/src/) <lab-dir> <vm> <exe-name> on|off
set -euo pipefail
lab=$(cd "$1" && pwd) vm=$2 exe=$3 mode=$4
cd "$lab"
gps() { vmlab exec --timeout 120 "$vm" -- powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "$1" | tr -d '\r'; }
if [ "$mode" = on ]; then
    gps "foreach (\$v in 'SOFTWARE', 'SOFTWARE\\WOW6432Node') {
           \$k = \"HKLM:\\\$v\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\$exe\"
           New-Item -Force \$k | Out-Null
           New-ItemProperty -Force \$k GlobalFlag -PropertyType DWord -Value 0x02000000 | Out-Null
           New-ItemProperty -Force \$k PageHeapFlags -PropertyType DWord -Value 0x3 | Out-Null }
         \$d = 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\$exe'
         New-Item -Force \$d | Out-Null
         New-Item -ItemType Directory -Force C:\\win3d\\dumps | Out-Null
         New-ItemProperty -Force \$d DumpFolder -PropertyType ExpandString -Value 'C:\\win3d\\dumps' | Out-Null
         New-ItemProperty -Force \$d DumpCount -PropertyType DWord -Value 10 | Out-Null
         New-ItemProperty -Force \$d DumpType -PropertyType DWord -Value 1 | Out-Null
         Get-ItemProperty \"HKLM:\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\$exe\" | Select-Object GlobalFlag, PageHeapFlags | Format-List"
else
    gps "foreach (\$v in 'SOFTWARE', 'SOFTWARE\\WOW6432Node') {
           Remove-Item -Force -Recurse -ErrorAction SilentlyContinue \"HKLM:\\\$v\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\$exe\" }
         Remove-Item -Force -Recurse -ErrorAction SilentlyContinue 'HKLM:\\SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\$exe'
         'page heap off for $exe'"
fi
