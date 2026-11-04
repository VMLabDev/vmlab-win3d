# Dedicated video memory and Venus residency

## Status

The dedicated-memory conversion is not implemented. Windows still sees the
host-visible region as an aperture. Patch `patches/kmd/0005` independently removes
the allocator's 16 GiB ceiling and rejects requests whose page rounding overflows.
It does not change the segment type or establish native graphics acceptance.

The original KMD-only scope assumes that Venus device-local allocations occupy
the shared-memory region. The pinned Mesa source contradicts that assumption.
Implementing the complete feature requires a residency contract across Mesa,
the KMD, and the host renderer before the segment flags can change safely.

## WDDM contract

Microsoft distinguishes memory segments, which hold allocation contents, from
apertures, which map system pages. VidMm places and pages memory-segment
allocations. A CPU-visible memory segment must expose the contents at its
advertised CPU physical address.
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentflags

Dedicated video memory is the sum of memory-segment sizes without
`PopulatedFromSystemMemory`. An aperture's `CommitLimit` limits system-memory
commitment, not dedicated capacity. Reporting the BAR size as a memory segment
changes the allocation and paging contract as well as the reported number.
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/calculating-graphics-memory

`DXGK_QUERYSEGMENTOUT3` and `DXGK_QUERYSEGMENTOUT4` must describe the same
underlying region. The WDDM 2 form uses the caller's descriptor stride.
`DXGK_SEGMENTDESCRIPTOR4.CommitLimit` applies only to apertures.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout3
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout4
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentdescriptor4

## Existing allocation paths

These locations refer to the KMD source at pin `b99d951a` and an existing Mesa
build source tree based on pin `374c3022`, with an older subset of local patches.
Current patches 0011, 0067, and 0085 do not add the missing device-local allocation
tracking or ICD residency calls. Paths below are relative to those source roots.

- F1: `viogpu/viogpu3d/viogpu_adapter.cpp:828-885,927-1017` reports the
  capability length as segment 2, with `Aperture = TRUE` in both query forms.
  Segment 1 backs legacy resources and paging buffers with guest RAM.
- F2: `viogpu/viogpu3d/viogpu_allocation.cpp:831-847,863-909` selects segment 2
  for every blob. Only mappable blobs reserve a private `m_blob_offset`.
  `EscapeResourceMapBlob` maps that offset, independently of VidMm placement.
- F3: `viogpu/viogpu3d/driver.cpp:802-923` supports aperture map/unmap and
  page-table updates. It has no memory-segment transfer, fill, or discard path.
- F4: `src/virtio/vulkan/vn_device_memory.c:32-51,437-442` creates ordinary
  device-local allocations through Venus `vkAllocateMemory`, without a D3DKMT
  allocation. Host-visible BAR BO creation is deferred until mapping at
  `src/virtio/vulkan/vn_device_memory.c:652-667`.
- F5: `src/virtio/vulkan/vn_renderer_d3dkmt.c:1666-1681` requests
  `USE_MAPPABLE` only for host-visible memory. The renderer rejects mappable
  export of non-host-visible Vulkan memory in virglrenderer 1.3.0,
  `src/venus/vkr_device_memory.c:541-547`.
- F6: `src/gallium/winsys/yttrium/gdi/yttrium_resource.c:1747-1768` also uses
  Venus-only backing for ordinary render/depth targets. D3DKMT backing there
  depends on `shared_target`.

The BAR is therefore a mapping window onto selected host allocations. Its size
alone neither reserves host VRAM nor makes every Venus allocation visible to
VidMm. Forcing `USE_MAPPABLE` cannot make non-host-visible memory CPU-accessible.

## Proposed segment layout

The following decisions describe the target implementation, not current behavior.

| ID | Decision |
| --- | --- |
| D1 | Retain segment 1 as the single aperture for legacy guest-RAM resources and paging buffers. |
| D2 | Describe segment 2 as CPU-visible memory only after the backing and paging prerequisites below work. Use `Aperture = FALSE`, `CpuVisible = TRUE`, and `PopulatedFromSystemMemory = FALSE`. Set `ApplicationTarget` and `LocalBudgetGroup` in QUERYSEGMENT4. Leave aperture-only `CommitLimit` zero. |
| D3 | Give VidMm sole ownership of placement in segment 2. A resident blob maps at the VidMm-assigned segment offset. Remove the competing private allocator from that allocatable range. |
| D4 | Keep the existing guest-RAM page-table segment separate and `PopulatedFromSystemMemory = TRUE`. It does not subtract from BAR capacity. |
| D5 | Do not advertise preservation through standby or hibernate without a tested preservation mechanism. Keep reset generation and lost-device semantics from patch 0004. |

Let `L` be the validated 64-bit capability length and `R` a page-aligned,
explicitly reserved transport region. The reported dedicated size is
`floor((L - R) / PAGE_SIZE) * PAGE_SIZE`. Its CPU base is the translated BAR
address plus the capability offset plus `R`. Its GPU base is segment-relative.
QUERYSEGMENT3 and QUERYSEGMENT4 must use identical arithmetic.

No fixed reservation is justified by the existing source. Command rings and
feedback currently consume ordinary blob ranges. The complete design must
either put them under VidMm too or measure and reserve a disjoint transport
pool. This document does not invent an `R` value or promise that all `L` bytes
are available while a private allocator still occupies them.

## Allocation and paging prerequisites

Every device-local Venus allocation needs a kernel-visible identity, size,
context, generation, and residency lifetime. This includes allocations that
currently bypass D3DKMT. The ICD and Gallium UMD must register and retire them,
and synchronize Venus submissions with WDDM residency. Existing mapped pointers
must remain valid for their promised lifetime.

For host-visible allocations, import the host blob before publishing a resident
BAR range. Map it at VidMm's offset and wait for the map response before CPU
access. A failed import must fail the allocation or paging request and unwind
placement. Keep host-reported cache attributes.

Non-host-visible allocations require a host-side staging/transfer mechanism
that preserves their Vulkan contents, bindings, and synchronization. They
cannot satisfy a CPU-visible segment by mapping unrelated zero-filled pages.
If they use a separate non-CPU-visible segment, its capacity and budget must
share the same configured total rather than double-counting it.

`BuildPagingBuffer` must implement actual transfer, fill, and discard operations
for every segment it reports. Page-in restores content before signaling
completion. Page-out copies current content to the supplied system-memory MDL
before unmapping or reusing the BAR range. Fill initializes the actual backing.
Discard may release content only when VidMm permits it. Chunk transfers and
check offset, length, alignment, MDL bounds, and arithmetic overflow.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_buildpagingbuffer
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer

Physically addressed allocations need `AccessedPhysically`. Use
`ExplicitResidencyNotification` to track commit and eviction transitions.
`SynchronousPaging` orders WDDM allocation use, but cannot order Venus commands
that bypass WDDM submission. Those commands need the new cross-layer contract.
`EvictionSegmentSet = 0` does not make an allocation unevictable.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_notifyresidency

## Eviction and timeout recovery

Ordinary eviction preserves live Vulkan objects and their contents. It must not
be implemented as device loss. TDR does lose the renderer context, so patch
0004 deliberately invalidates blobs rather than importing stale blob IDs.
Preserve that distinction.

Stop new submissions and paging before reset. Wait for backend quiescence, drain
old-generation callbacks, and revoke CPU mappings before recycling BAR ranges.
Retain reservations when a stale mapping cannot be revoked. Restore legacy
guest-RAM resources, but require lost Venus devices to recreate their objects.
Do not reset the private allocator during TDR while old allocations survive.
`docs/tdr-recovery.md` records the existing host-quiescence limitation.

## Verification gate

Before changing segment reporting, exercise page-in, dirty page-out, clean
discard, fragmented placement, repeated eviction, mapping lifetime, and reset
during paging. Check contents after eviction, not only allocation success.

Then build package 0.1.4 and run on disposable Windows 11 `#win3d` guests using
the requested installed backend. At default VRAM and 16 GiB, retain DXGI byte
counts, dxdiag, and `Win32_VideoController.AdapterRAM`. Treat the WMI field as
additional evidence, not the authority for a 64-bit capacity.

Run all ten render probes, forced TDR with x64/x86 D3D11 and Vulkan readback,
and a GPU-memory test keeping more than 4 GiB live with verified contents.
Use multiple sub-4-GiB CPU mappings because the current `DxgkCbMapMemory` ABI
takes an ULONG byte count. Do not run Heaven.

These native checks and the package version bump remain pending. The allocator
unit test proves range arithmetic only, not WDDM residency or GPU behavior.
