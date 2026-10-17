# labs/zeus

Labs and scripts for the Zeus laptop runs (Intel Iris Xe and NVIDIA RTX 4060
Max-Q) and the AMD regression run on helios. `docs/zeus.md` explains the runs
and their results.

| Lab | Host | Host GPU it names |
|---|---|---|
| `intel/vmlab.wcl` | Zeus | `Intel1` |
| `nvidia/vmlab.wcl` | Zeus | `Nvidia1` |
| `amd/vmlab.wcl` | helios | `AMD1` |

## vmlab version and the host config

The lab files use the vmlab 0.25.0-alpha form: `gpu { mode = "vulkan"
host_gpu = "<name>" }`. The name resolves to a PCI address through the host
config, `~/.config/vmlab/config.wcl`, on the machine that runs the lab.

Zeus still runs vmlab 0.24 and has no host config, so these lab files do not
validate there yet. Before the next run on Zeus, install vmlab >= 0.25.0-alpha
and write Zeus's `~/.config/vmlab/config.wcl`:

```
import <vmlab-host.wcl>

// Host GPUs on zeus.
gpu "Intel1"  { address = "0000:00:02.0" }                  // Iris Xe (ANV)
gpu "Nvidia1" { address = "0000:01:00.0"  default = true }  // RTX 4060 Max-Q (nvidia)
```

Check the addresses with `lspci -D | grep -Ei 'vga|3d|display'`.
`labs/zeus/vmlab-iso.sh` changes only `XDG_RUNTIME_DIR` and
`XDG_STATE_HOME`, so its isolated supervisor reads the same host config.

## Evidence

All evidence under `docs/evidence/zeus/` was taken on vmlab 0.24 with the
earlier lab form, `gpu { mode = "vulkan" address = "0000:..." }`. Each run's
copy of the lab file is its `lab-vmlab.wcl` (for example
`docs/evidence/zeus/intel/05-fix-0021/lab-vmlab.wcl`).
