#!/usr/bin/env python3
"""Apply the UAPI 5 feature rollback to the exact known-working SuSFS base."""
import json
import pathlib
import re
import subprocess
import sys

BASE = '715962008d9bc6ced66fc1efd4a61b252bf19284'
root = pathlib.Path(sys.argv[1]).resolve()
patch = pathlib.Path(__file__).resolve().parents[1] / 'patches/ksun-susfs-uapi5.patch'

def git(*args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()

assert git('rev-parse', 'HEAD') == BASE, 'Unexpected SuSFS revision; refusing to patch'
assert not git('status', '--porcelain', '--untracked-files=no'), 'Driver has tracked modifications'
assert git('rev-parse', '--is-shallow-repository') == 'false', 'Full history is required for accurate metadata'
count = int(git('rev-list', '--count', 'HEAD'))
version = 30000 + count
# Keep the actual upstream identity and explicitly label our local UAPI rollback.
tag = git('describe', '--tags', '--always', 'HEAD') + '-uapi5'
assert re.fullmatch(r'[A-Za-z0-9._+-]+', tag), 'Unexpected tag characters'
git('apply', '--check', str(patch))
git('apply', str(patch))

p = root / 'kernel/Kbuild'
s = p.read_text()
a = s.index('LPATH :=')
b = s.index('KSU_NEW_DCACHE_FLUSH :=', a)
# Freeze real source-derived metadata before Bazel hides Git from the compiler.
s = s[:a] + f'''# Generated outside Bazel from pinned source {BASE}; local UAPI 5 rollback.
KSU_VERSION := {version}
KSU_VERSION_TAG := {tag}
$(info -- KernelSU-Next version: $(KSU_VERSION))
$(info -- KernelSU-Next tag: $(KSU_VERSION_TAG))
ccflags-y += -DKSU_VERSION=$(KSU_VERSION)
ccflags-y += -DKSU_VERSION_TAG=\\"$(KSU_VERSION_TAG)\\"

''' + s[b:]
p.write_text(s)
header = (root / 'uapi/supercall.h').read_text()
assert re.search(r'KERNEL_SU_UAPI_VERSION\s*=\s*5\s*;', header)
assert 'KSU_IOCTL_LOOKUP_SYMBOLS' not in header
assert 'KSU_GET_INFO_FLAG_PREINIT' not in header
assert 'preinit/blob.o' not in s and 'preinit/preinit.o' not in s
for name in ('core/init.c', 'runtime/ksud_integration.c', 'supercall/dispatch.c', 'hook/syscall_event_bridge.c'):
    code = (root / 'kernel' / name).read_text()
    assert 'ksu_preinit_' not in code, name
    assert 'KSU_IOCTL_LOOKUP_SYMBOLS' not in code, name
assert 'config KSU_SUSFS' in (root / 'kernel/Kconfig').read_text()
assert 'susfs_init();' in (root / 'kernel/core/init.c').read_text()
assert 'susfs_start_sdcard_monitor_fn();' in (root / 'kernel/supercall/dispatch.c').read_text()
report = dict(driver_base=BASE, uapi=5, version=version, tag=tag,
              change='Remove module preinit and symbol-lookup UAPI 6; preserve manual SuSFS 2.3 hooks')
print(json.dumps(report, indent=2))
if len(sys.argv) > 2:
    pathlib.Path(sys.argv[2]).write_text(json.dumps(report, indent=2) + '\n')
