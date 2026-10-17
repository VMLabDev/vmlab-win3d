# dxil-spirv patches

None. dxil-spirv is used unpatched at the commit pinned in
`build/d3d12/PINS` (`8434b063`), which is newer than vkd3d-proton v3.0.1's
gitlink (`62dbb07f`). That bump fixes the invalid SPIR-V that crashed the host
driver under Godot D3D12 (prior art §2.2).

The prior art also carried an unexported 823-line local diff on top of
`8434b063`. It added texture-sampler metadata and static-sampler plumbing for
its YUY2 emulation, and changed sparse-feedback loads. It was dropped with the
YUY2 work; see `build/d3d12/README.md`, "Patches".

`build/d3d12/container-build.sh` applies any `*.patch` placed here, in order.
