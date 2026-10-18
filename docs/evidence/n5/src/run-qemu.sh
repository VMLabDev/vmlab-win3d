#!/usr/bin/env bash
# Run the n5 Nvidia1 lab's VM under a QEMU this shell owns, with virtio-gpu
# fence trace events, so host-side fence latency can be measured. The lab must
# be down. Same argv as vmlab's (qemu.log) minus TPM, NIC and vmlab's sockets.
# Usage: run-qemu.sh <out-dir> [qemu-binary] [extra env assignments...]
set -euo pipefail
OUT=$1; shift; QEMU=${1:-/home/wil/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64}; [ $# -gt 0 ] && shift
LAB=/home/wil/dev/vmlab-win3d-wt/n5/.scratch/e2e/windows-11-nvidia1/lab/.vmlab/vms/win
mkdir -p "$OUT"; R=/run/user/1000/n5q; mkdir -p $R
env VK_LOADER_DRIVERS_SELECT='*nvidia*' VK_ADD_IMPLICIT_LAYER_PATH=/home/wil/.local/share/vmlab/vk-layer/0df9161736fcf95d VMLAB_VK_PCI_ADDRESS=0000:01:00.0 "$@" \
"$QEMU" -name n5-selfrun -L /usr/share/qemu -machine q35 -accel kvm -cpu host,vmx=off,svm=off -smp 4 -m 8192M \
  -display none -vnc unix:$R/vnc.sock,share=ignore -monitor unix:$R/mon.sock,server=on,wait=off \
  -serial file:$OUT/serial.log \
  -drive if=pflash,format=raw,readonly=on,file=/home/wil/.local/share/vmlab/guest/firmware/x86_64/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=$LAB/OVMF_VARS.fd \
  -device virtio-serial-pci \
  -blockdev driver=qcow2,node-name=disk0,file.driver=file,file.filename=$LAB/disk0.qcow2 -device virtio-blk-pci,drive=disk0 \
  -nic none \
  -device virtio-vga-gl,venus=true,blob=true,hostmem=4G -display egl-headless,rendernode=/dev/dri/renderD128 \
  -usb -device usb-tablet \
  -msg timestamp=on -trace virtio_gpu_fence_ctrl -trace virtio_gpu_fence_resp -trace virtio_gpu_cmd_suspended -trace virtio_gpu_cmd_ctx_submit -trace virtio_gpu_cmd_ctx_create -D $OUT/trace.log
echo "qemu exit $?"
