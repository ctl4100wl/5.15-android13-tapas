# ChicKernel: kernel-only installation for Redmi Note 12 4G.
properties() { '
kernel.string=ChicKernel for tapas/topaz (experimental)
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
device.name1=tapas
device.name2=topaz
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; }

# BLOCK=auto can choose init_boot. Explicitly restrict to boot.
BLOCK=boot;
IS_SLOT_DEVICE=auto;
SLOT_SELECT=active;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=0;
NO_VBMETA_PARTITION_PATCH=1;
NO_MAGISK_CHECK=1;
. tools/ak3-core.sh;

# Upstream falls back to unsuffixed boot. Reject that on a slotted device.
case "$SLOT" in
  _a|_b) case "$BLOCK" in
    */boot"$SLOT") ;;
    *) abort "Active boot slot not found; refusing fallback.";;
  esac;;
  '')
    # Missing slot properties must not turn an A/B install into an ambiguous write.
    for d in /dev/block/by-name /dev/block/bootdevice/by-name /dev/block/platform/*/by-name /dev/block/platform/*/*/by-name; do
      if [ -e "$d/boot_a" ] || [ -e "$d/boot_b" ]; then
        abort "Slotted boot detected but active slot is unknown.";
      fi;
    done;
    case "$BLOCK" in */boot) ;; *) abort "Unexpected boot partition.";; esac;;
  *) abort "Invalid boot slot.";;
esac;

ui_print "Kernel-only install: $BLOCK" "Compatibility is not established by this installer.";
split_boot;
# This package targets the inspected stock layout; fail before writing otherwise.
grep -Eq '^HEADER_VER[[:space:]]+\[4\]$' "$SPLITIMG/infotmp" || abort "Expected boot header v4.";
grep -Eq '^RAMDISK_SZ[[:space:]]+\[0\]$' "$SPLITIMG/infotmp" || abort "Expected an empty boot ramdisk.";
[ ! -s "$SPLITIMG/ramdisk.cpio" ] || abort "Unexpected boot ramdisk; refusing this layout.";
flash_boot;
