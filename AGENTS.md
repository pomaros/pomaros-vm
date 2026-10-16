# Working on PomAROS VM

Private repository. Publish only to this private origin; upstream publication
requires John's explicit instruction. MIT for host/tool code; preserve the
licenses and provenance of imported files. OS changes belong in the isolated
APL core checkout `../aros-core-pomaros`, not in the hosted PomAROS tree.

Guest x18 is an ordinary caller-saved register. No guest build may reserve it.
Host code follows the macOS ABI. Keep hardware virtualization tests separate
from TCG tests and from physical Apple tests. A successful VM start is not a
successful guest boot.

Use isolated file-backed disks and bound every automated guest run. Keep the
exact binary hashes, logs and strict test verdicts under ignored `artifacts/`.
Record decisions, measured results and remaining work in `docs/STATUS.md`.
