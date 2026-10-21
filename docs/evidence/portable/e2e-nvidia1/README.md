# e2e on Nvidia1 with the portable release tarballs

`tests/e2e/run.sh --template windows-11 --host-gpu Nvidia1 --lab win3d-portable-e2e`,
run on 2026-10-21 from the fresh clone that built the release
`0.1.1-alpha-portable1` (`../release.txt`), through an isolated vmlab
supervisor (`isolated-supervisor.sh`: `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`
and `XDG_STATE_HOME` under the worktree's `.scratch/`). `host-config.wcl` is
the copy of the user's `~/.config/vmlab/config.wcl` whose `Nvidia1` block
points `qemu_binary` and `qemu_env` at the release tarballs installed into
the worktree's `.scratch/prefix/` with their `install.sh`; the user's config
and `~/.local/opt/` were not touched.

- `results.txt`: **PASS: 12 of 12 Done checks pass**.
- `status-v.txt`: `qemu=.../.scratch/prefix/qemu-vmlab-egl/bin/qemu-system-x86_64`
  and `env=RENDER_SERVER_EXEC_PATH=.../.scratch/prefix/virglrenderer-vmlab/libexec/virgl_render_server`.
- `qemu-argv-env.txt`: the argv (that binary, `-L /usr/share/qemu`, egl-headless,
  `virtio-vga-gl,venus=true,blob=true,hostmem=4G`) and the env line with
  `RENDER_SERVER_EXEC_PATH`.
- `kernel-log.txt`: no Xid and no NVRM line in the host kernel log during the run.
- `host.txt`: helios, vmlab 0.31.0-alpha, nvidia-utils 615.78.08, QEMU 11.1.50 (vmlab d8a6048aae11).
- The rest is the usual acceptance set (`tests/e2e/run.sh`): console PNGs,
  probe results, dxdiag, install journal.

The lab was destroyed by the run (`run.log`: `lab "win3d-portable-e2e" destroyed`)
and the supervisor stopped.
