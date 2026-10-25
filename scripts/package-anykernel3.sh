#!/usr/bin/env bash
# Usage: scripts/package-anykernel3.sh /explicit/bazel/dist/Image KSUN [output-directory]
set -euo pipefail
[[ $# -ge 2 && $# -le 3 ]] || { echo 'Usage: package-anykernel3.sh IMAGE VARIANT [OUT]' >&2; exit 2; }
image=$(realpath "$1")
variant=$2
case "$variant" in KSUN|KSUN-SuSFS) ;; *) echo 'Unknown variant' >&2; exit 2;; esac
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
out=$(mkdir -p "${3:-artifacts}" && realpath "${3:-artifacts}")
# Match the inspected stock payload format. Never guess among multiple Bazel outputs.
python3 - "$image" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    h=f.read(64)
assert len(h)==64 and h[56:60]==b'ARM\x64', 'Expected raw ARM64 Image, not boot.img, gzip, or a ZIP'
PY
ak3_revision=020dfeccf9d7e962a48400fc94d3e451df92eead
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
git -C "$stage" init -q
git -C "$stage" remote add origin https://github.com/osm0sis/AnyKernel3.git
git -C "$stage" fetch -q --depth=1 origin "$ak3_revision"
git -C "$stage" checkout -q --detach FETCH_HEAD
[[ $(git -C "$stage" rev-parse HEAD) == "$ak3_revision" ]]
# Only upstream installer/runtime, our config, and this target's Image enter the ZIP.
rm -rf "$stage/.git" "$stage/.github" "$stage/modules" "$stage/patch" "$stage/ramdisk"
find "$stage" -maxdepth 1 -type f ! -name LICENSE -delete
cp "$root/packaging/anykernel.sh" "$stage/anykernel.sh"
cp "$image" "$stage/Image"
sh -n "$stage/anykernel.sh"
name="ChicKernel-${variant}-AnyKernel3.zip"
rm -f "$out/$name"
python3 - "$stage" "$out/$name" <<'PYZIP'
import pathlib,sys,zipfile
root=pathlib.Path(sys.argv[1])
with zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED) as z:
    for item in ('META-INF','tools','anykernel.sh','Image','LICENSE'):
        p=root/item
        for f in (sorted(p.rglob('*')) if p.is_dir() else [p]):
            if f.is_file(): z.write(f,f.relative_to(root).as_posix())
PYZIP
python3 - "$out/$name" "$image" <<'PY'
import hashlib,sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.testzip() is None
    required={'Image','anykernel.sh','tools/ak3-core.sh','tools/magiskboot','META-INF/com/google/android/update-binary'}
    assert required <= set(z.namelist())
    assert not any(n.endswith('.img') or n.startswith(('ramdisk/','modules/','patch/')) for n in z.namelist())
    with open(sys.argv[2],'rb') as f:
        assert hashlib.sha256(z.read('Image')).digest()==hashlib.file_digest(f,'sha256').digest()
PY
(cd "$out" && sha256sum "$name" > "$name.sha256")
printf 'Created %s\n' "$out/$name"
