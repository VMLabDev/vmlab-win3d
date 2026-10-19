# `vmlab cp` hang, windows-10 on Nvidia1, 2026-10-19

vmlab 0.30.0-alpha. In `tests/e2e/run.sh --template windows-10 --host-gpu Nvidia1
--lab win3d-rc3-w10-nvidia1 --apps`, step 4 (`scripts/install-into-vm.sh`) ran
`vmlab cp <repo>/out/package/vmlab-win3d-0.1.1 win:C:/vmlab-win3d` at 18:06:27
(+10:00). The copy stopped making progress and never returned; it was killed
at about 18:28.

- Guest (`guest-listing.txt`, taken about 18:27 through `vmlab exec`, which
  still answered): 16 of the bundle's 29 files, 30,535,684 of 102,444,141
  bytes. Every file at the top level and under `runtime\` is there at its
  full size. The whole `driver\` directory (13 files, 71.9 MB, the largest
  being 20.8 MB) is missing, so the copy stalled at or before its first
  file in `driver\`. Guest write times are 1:06:28 AM guest time (the copy's
  first second).
- Host (`ps.txt`, `proc.txt`): PID 174419, 33 threads, all in `futex_wait`
  except one in `epoll_wait`; `rchar` 18,916 bytes, so it had read almost
  none of the 102 MB it copied 30 MB of (the files may be sent by another
  path). `/proc/<pid>/stack` needs root.
- Lab daemon log (`labd.log`): no line after `lab.up` at 08:00:23Z (18:00:23
  local); nothing logged for the copy. It does log `host.disk_low`
  (5 % free on `/home`, 205 GB available, `host-df.txt`) at lab start.
- `status-v.txt`: the VM running, ready, agent `f1e4515`.
- Five other VMs ran on the host at the same time (three on Nvidia2, one more
  on Nvidia1, one on AMD1); their identical `vmlab cp` of the same bundle
  finished.
- `run.log` and `run-log-tail.txt`: the e2e script's log; `serial-tail.txt`,
  `qemu-log-tail.txt`: the VM's logs.
