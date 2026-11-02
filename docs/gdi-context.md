# GDI contexts after guest reboot

## Cause

Driver `100.6.101.58100` creates a host context only when a user-mode driver
sends `VIOGPU_CTX_INIT`. A kernel GDI device can present without that escape.
It still attaches allocations and submits a coherent upload followed by a
56-byte virgl `RESOURCE_COPY_REGION` command on its allocated context ID.

On vmlab-hv, a Windows reboot exposed these commands on an uncreated context.
The host reported `invalid context id` and rejected `TRANSFER_TO_HOST_3D` and
`SUBMIT_3D`. Both Vulkan architectures could still enumerate Venus, because
their UMD initialization created separate, valid contexts.

## Fix

Patch `0003-kmd-create-context-for-gdi-devices.patch` creates a legacy context
in `VioGpuDevice` construction, before allocation opens enqueue attachments.
The existing `Init` path destroys and replaces that context when a UMD selects
Venus or another capset. The existing destructor releases it. Device creation
also requires initialized hardware so it cannot queue this command on a closed
adapter.

This fix first shipped in KMD `100.6.101.58101`, above the old package.
The timeout-recovery candidate uses `100.6.101.58102`; see `tdr-recovery.md`.
Upstream licence headers are unchanged; `viogpu_device.cpp` remains MPL-2.0.
No QEMU code is used.

## Verification

The KMD cross-build and package signature verification pass. A private Windows
clone accepted the test-signed package through `pnputil`, reported driver
`100.6.101.58101`, and had device problem code 0.

The regression test lives in the hypervisor checkout at
`crates/vmlab-hv/tests/venus/windows.rs`. Enable
`VMLAB_HV_TEST_VENUS_RENDER_NODE` and point `VMLAB_HV_TEST_WIN3D_DISK` at a
private image containing the updated driver. The test forces a guest reboot
in the same hypervisor process, runs the template's `win3d-verify.ps1 -Phase
Vulkan`, and rejects every GPU command error in the host log.

On 2026-11-02 it passed on the NVIDIA RTX 5090 and the AMD Ryzen 9 9950X iGPU
with both x64 and x86 Venus enumeration and no invalid-context warnings.
The same strict check failed
with driver `58100`. No owner's VM was updated or restarted.

This historical check covers guest reboot and device construction. Patch 0004
also restores legacy contexts after D3/D0 and adapter timeout recovery.
