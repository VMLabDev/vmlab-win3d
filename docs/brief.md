# Brief: vmlab-win3d

You are the orchestrator for this repository. Read this whole file before
acting. Return to it whenever scope is in doubt.

## The goal

Build a Windows guest driver package for vmlab. You install it once in a Windows
10 or 11 guest. After that, unmodified Direct3D 9, 10, 11 and 12 applications
and OpenGL applications run hardware-accelerated in the VM with no per-app DLLs,
no overrides and no configuration, and they show on vmlab's console (VNC).

All rendering runs on the **host's Vulkan stack**. The guest reaches it through
virtio-gpu **Venus**, which is vmlab's `gpu { mode = "vulkan" }`. QEMU runs it
as `virtio-gpu-gl,venus=true` (`/home/wil/dev/vmlab/src/qemu/cmdline.rs`). The
guest-side stack this implies:

1. A virtio-gpu WDDM kernel-mode driver with Venus support.
2. A system-wide Vulkan ICD that forwards to the host over Venus.
3. Direct3D and OpenGL implemented **on top of that Vulkan**, registered so the
   OS loads them for every application. Translation layers are acceptable and
   expected, for example DXVK and vkd3d-proton for Direct3D and Mesa Zink for
   OpenGL.

A native D3D12 DDI is out of scope. Presentation has to be good enough that a
game is watchable on the console. Frame-pacing research beyond that is out of
scope.

## Prior art: `/home/wil/dev/virtio`

A codex session worked on this, starting from virtio-win PR 943, and it got
sidetracked. It went deep into a native WDDM 2.0 D3D11 UMD, a D3D12 bridge,
UAV and compute-shader corner cases, and present-timing investigations. Mine it
for findings. Do not continue its threads. Read, in this order:

1. `/home/wil/dev/virtio/EXPERIMENTS.md`, the running summary.
2. `/home/wil/dev/virtio/.scratch/handoff/20261017-021318-windows-3d-paused.md`.
3. In `/home/wil/dev/virtio/.scratch/prototype/driver-integrated-3d/20261016-001605-8bac/`:
   the latest chronological entries of `NATIVE-WDDM20.md` and `VERDICT.md`.
   Older entries there describe superseded states.
4. `/home/wil/dev/virtio/.scratch/plans/*/INTAKE.md` and
   `/home/wil/dev/virtio/.scratch/research/`.

From those, write `docs/prior-art.md`. It should cover:
- what was proven to work, and on which component, such as the PR 943 KMD,
  "Yttrium", DXVK or vkd3d;
- what failed or stayed unresolved;
- which artefacts are reusable here, as sources or patches, not binaries of
  unknown provenance;
- how driver signing and installation were handled.

Delegate the reading to a worker if it is large, then check the summary against
the sources before relying on it.

`/home/wil/dev/virtio` belongs to that session. Read it and never write to it.
Its labs `virtio-pr943` and `virtio-clean-install` are paused, and they are not
yours to start, stop or destroy.

## Constraints

- **The host and vmlab.** vmlab is at `/home/wil/dev/vmlab`, installed as
  v0.23.0-alpha. Its skills `vmlab` and `verify` explain labs, templates and the
  CLI. Local Windows templates live under
  `~/.local/share/vmlab/templates/x86_64/` (`windows-11`, `windows-10`). Use your
  own lab names (`win3d-*`) in your scratch dirs, and destroy them when done.
- **Changes vmlab needs.** If vmlab itself needs a change (a GPU mode, profile
  or display flag), do not edit that repo. Write it up with the `/handoff` skill
  to `/home/wil/dev/vmlab` and keep going on an assumption.
- **Signing.** Windows loads only signed kernel drivers. Settle early how the
  package installs on a stock vmlab Windows template, and record it as a
  decision in `docs/`. Options include test signing with secure boot off and a
  template hook, among others. The codex attempt solved this somehow, so find
  out how.
- **Builds.** Builds must be reproducible from this repo by script, preferably
  in a container on this Linux host (mingw/clang cross toolchains, Meson for
  Mesa). Record every upstream pin by version or commit and hash. Commit no
  vendored binary unless it comes with a script that rebuilds it.
- **Visibility.** This repo is local only. Push nothing and create no GitHub
  repo until the user asks.
- **sudo.** There is usually nobody to type a sudo password. Prefer user-level
  installs and containers.

## How you work

You orchestrate, and you delegate the hands-on work. Use the `herdr` skill to
start other Claude instances, one per independent slice, each in its own herdr
tab and git worktree:

```
herdr agent start <name> --kind claude --pane <pane> -- --dangerously-skip-permissions
```

Own the decomposition yourself. Give each worker a self-contained brief file
with its acceptance criteria, and have it write a state file you watch. Merge
one branch at a time, and reap each worker's pane, worktree and branch as soon
as its work has merged. Check that workers are still moving. An agent left idle
on an unfinished background shell has stalled, and you should nudge it.

`/map`, `/to-spec`, `/tickets` and `/fleet` are available if you want a
ticketed plan.

## Done

The package is done when all of the following hold:

1. One script builds the whole package from this repo, with no manual steps.
2. On a fresh vmlab VM from the local `windows-11` template with `gpu { mode =
   "vulkan" }`, one documented install command (plus a reboot if needed) gives
   all of these:
   - `vulkaninfo` lists the Venus device;
   - an unmodified Direct3D 9, 11 and 12 application and an OpenGL application,
     each with no app-local graphics DLLs, render on the GPU and are visible in
     a vmlab console screenshot;
   - `dxdiag` reports hardware acceleration.
3. `docs/` explains install, uninstall, the component stack and its upstream
   pins, the signing decision, known limits, and the evidence for each item in
   2.

Report progress to the user in your own pane. When something can only be
decided by the user, ask there with a recommended option and continue on
everything that does not depend on the answer.
