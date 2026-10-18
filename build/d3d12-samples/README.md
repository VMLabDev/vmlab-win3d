# d3d12-samples: Microsoft's D3D12 samples, built unmodified

This build compiles five samples from Microsoft's
[DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples)
for x64 Windows. They are test applications for the D3D12 path
(`docs/d3d12app.md`); they are not part of the package.

| Sample | What it exercises |
| --- | --- |
| `D3D12HelloTriangle` | Device, swap chain, one draw |
| `D3D12HelloTexture` | A texture upload and a static sampler |
| `D3D12Bundles` | Bundles, many draws, PIX markers |
| `D3D12nBodyGravity` | Compute on two extra queues, cross-queue fences, a geometry shader |
| `D3D12ExecuteIndirect` | `ExecuteIndirect` with a root CBV argument, compute culling, a count buffer |

## Build

```sh
build/d3d12-samples/build.sh            # downloads once, then reuses .work/
build/d3d12-samples/build.sh --clean    # discard downloads, sources and the CRT/SDK first
```

The script runs in the shared toolchain image (`build/toolchain/`) as the
calling user. It needs Docker and network access, and no sudo. It writes only
to `out/d3d12-samples/`:

```
out/d3d12-samples/x64/<Sample>/   <Sample>.exe, its .cso shaders and data files
out/d3d12-samples/SHA256SUMS
out/d3d12-samples/.work/          downloads, the samples checkout, xwin's cache, the CRT/SDK
```

Two runs from the same pins give the same files byte for byte
(`docs/evidence/d3d12app/build/`).

## How the samples are built

The samples need the MSVC ABI: DirectX-Headers' MSVC return-value forms, WRL,
and `__uuidof`. llvm-mingw cannot build them, so this build uses:

- **clang-cl and lld-link** from the shared toolchain (clang 23.1.3, the
  compiler in llvm-mingw 20261006), run with `--target=x86_64-pc-windows-msvc`;
- **the MSVC CRT 14.44.17.14 and the Windows SDK 10.0.26100**, fetched by
  [xwin](https://github.com/Jake-Shadle/xwin) 0.10.0 from a pinned Visual Studio
  package manifest, and checked against a hash of the whole splatted tree;
- **DXC v1.8.2505.1 for Linux**, for the projects' SM 6.0 shader steps;
- the samples' own NuGet packages for headers: `Microsoft.Direct3D.D3D12`
  1.618.3 (the Agility SDK headers and d3dx12) and `WinPixEventRuntime`
  (`pix3.h`).

`build-samples.py` reads each sample's `.vcxproj` and carries out its
`Release|x64` configuration: the `ClCompile` sources, the preprocessor
definitions, `CharacterSet`, the link libraries, `DelayLoadDLLs`, and each
`CustomBuild` step (the `dxc.exe` shader commands and the data-file copies).
No sample file is edited. Three things differ from a Visual Studio build:

- **No `D3D12\` folder.** The Agility SDK package copies `D3D12Core.dll` and
  `D3D12SDKLayers.dll` next to the executable unless
  `Microsoft_Direct3D_D3D12_SkipLibraryCopy` is set. This build does not
  deploy them, so the samples run on the system `d3d12.dll`. The executables
  still export `D3D12SDKVersion = 618` and `D3D12SDKPath = .\D3D12\`.
- **Header case.** The samples include `<D3Dcompiler.h>`; the SDK file is
  `d3dcompiler.h`. Linux file names are case-sensitive, so the build adds a
  link with the sample's spelling in a shim include directory.
- **No PDBs and no LTCG.** `GenerateDebugInformation` and
  `WholeProgramOptimization` are not carried out.

The executables import `VCRUNTIME140.dll` and `MSVCP140.dll` (`/MD`, as the
projects specify). A guest needs the Visual C++ 2015-2022 runtime;
`tests/apps/d3d12/run.sh` installs the pinned redistributable.

## Licences

The samples are MIT-licensed. xwin downloads the MSVC CRT and the Windows SDK
under Microsoft's licence terms (`--accept-license`). Nothing it downloads is
committed or shipped in the package.
