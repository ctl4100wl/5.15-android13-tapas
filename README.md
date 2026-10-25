# ChicKernel AnyKernel3 packaging change

Copy `.github/workflows/main.yml`, `scripts/package-anykernel3.sh`, and
`packaging/anykernel.sh` into the same paths in ctl4100wl/5.15-android13-tapas.
Commit them on the branch you build, normally `kernel-ksun-susfs`, and run
Build ChicKernel. This bundle is a local proposed change; nothing was pushed.

The complete replacement workflow is based on your main.yml blob
4827b17a64ef20a702db42c29f7d32c977a1726f; the main and kernel-ksun-susfs
copies matched at inspection. It adds a checkout (the existing workflow only
uses repo sync), one packaging step per matrix job, and an artifact upload.
Existing build/cache steps and raw Image uploads remain.

Outputs inside the two GitHub artifact downloads:

- ChicKernel-KSUN-AnyKernel3.zip and its SHA-256 checksum
- ChicKernel-KSUN-SuSFS-AnyKernel3.zip and its SHA-256 checksum

Extract the GitHub artifact download first: the inner AnyKernel3 ZIP is the
installer. No newly compiled kernel or flashable release is included here.

## Inspected stock image

The requested /mnt/data/boot.img was absent. The referenced conversation exposed
its attachment at the cache path recorded in stock-inspection.json, which was
read directly. Inspection found:

- 100663296 bytes (96 MiB file size, not proof of device partition size).
- ANDROID! header v4; header size 1584; 4096-byte header page.
- Raw ARM64 kernel, 47577600 bytes, ARM64 magic at kernel offset 56.
- Boot ramdisk size 0; empty boot command line; OS version field 0.
- Boot v4 signature size 0, with an AVBf footer at the file end.
- Full-file SHA-256: dcdabe280990a7d6f26e2aa4512a49558bc496a91e6ab6215b95d70f01aab6b0

The image alone does not establish device codename, active slot, ROM identity,
bootloader state, vendor module compatibility, or the cause of the black screen.
The historical OS2.0.209.0.VMTMIXM filename was not independently authenticated.

## Installer behavior

AnyKernel3 is pinned to upstream commit
020dfeccf9d7e962a48400fc94d3e451df92eead. Its actual property syntax,
setup_ak, split_boot and flash_boot implementations were inspected. This is
properties() with quoted key=value lines, and uppercase shell control variables.

BLOCK=boot avoids BLOCK=auto, which can select init_boot. IS_SLOT_DEVICE=auto
uses upstream active-slot detection, and SLOT_SELECT=active prevents the OTA
postinstall inactive-slot default. The script then refuses a fallback to
unsuffixed boot when a slot was detected, refuses unknown/invalid slots, and
refuses a missing slot when normal by-name directories expose boot_a/boot_b.
It allows a genuinely unslotted boot layout. No hard-coded mmcblk index is used.
A recovery that cannot expose the correct slot/partition must be fixed before
installation; do not bypass these guards.

Device checking permits only tapas/topaz. Module installation and systemless
module helpers are off. The only payload is this matrix target's raw Image.
There are no stock boot images, DTBs, DTBOs, modules, ramdisk overlays, or
vendor_boot/init_boot files in the package. The packager rejects non-raw-ARM64
inputs, verifies the ZIP CRCs and exact Image SHA-256, and fails on missing inputs.
It intentionally does not search for the first Image in bazel-out.

split_boot reads the installed selected boot image; flash_boot repacks against
that image. There is no ramdisk unpack/edit/repack and no permission rewrites.
The installer requires v4 and an empty boot ramdisk, matching the supplied image;
it aborts on another layout. Any ramdisks in init_boot/vendor_boot remain outside
this operation. Original header settings and other boot components are reused by
upstream magiskboot; size/hash/signature metadata necessarily changes as needed.
This is not a promise of byte-for-byte metadata preservation.

PATCH_VBMETA_FLAG=0 disables automatic boot vbmeta flag changes;
NO_VBMETA_PARTITION_PATCH=1 disables upstream auxiliary vbmeta partition patching;
NO_MAGISK_CHECK=1 disables opportunistic Magisk kernel/DTB changes. There is no
vbmeta flashing or AVB bypass in this change. The existing AVB footer is evidence
of verification metadata, not proof verification is disabled. A new kernel cannot
retain Xiaomi's valid stock signature. Verify the device's actual bootloader/AVB
requirements independently. Stock Magisk/root state is not guaranteed to survive.

## Verification and limitations

Passed: shell syntax for the helper/installer and workflow run blocks; YAML parse;
seven mocked pre-flash scenarios (slots a/b, unslotted boot, fallback rejection,
invalid slot, wrong header, unexpected ramdisk). Both variant package paths were
exercised with the stock kernel as a fixture, with ZIP CRC/structure and payload
hash checks passing. Fixture ZIPs were kept outside this deliverable.

The fixture runs fetched the pinned revision from the already cloned local
upstream repository because a later GitHub DNS lookup failed. CI uses HTTPS.
The production installer also checks RAMDISK_SZ=0 explicitly. No recovery-side
execution, actual partition writes, on-device repack comparison, full Bazel
build, hardware test, KMI/vendor module test, or KSUN/SuSFS runtime test occurred.
Those remain required before calling either kernel compatible or working.

## Recovery precautions

The previous black-screen build is not made compatible by wrapping it in a ZIP.
Keep the known-good boot image from the exact installed ROM build, and preferably
a recovery backup of the currently installed boot partition, on your PC before
any test. AnyKernel3's temporary boot.img is not a durable backup: cleanup is on
for successful installs. Do not rely on the phone's internal storage alone.

Confirm the unlocked bootloader, reliable fastboot access, exact ROM build,
product, and current slot before changing boot. Record:

    fastboot devices
    fastboot getvar product
    fastboot getvar current-slot
    fastboot getvar unlocked

If the device is already stuck, restore known-good boot before trying new builds.
For an A/B device, restore only the slot actually modified, after confirming it:

    fastboot flash boot_a path/to/exact-known-good-boot.img
    fastboot reboot

Use boot_b instead only if b was the modified slot. On a verified unslotted
layout use boot. These are conditional recovery examples, not instructions to
flash both slots. Avoid erasing data, changing slots blindly, touching init_boot,
vendor_boot, dtbo or vbmeta, or relocking the bootloader as a troubleshooting step.

Where supported, temporary boot of a separately prepared and inspected test boot
image is preferable for an initial test; this ZIP itself is not fastboot-bootable.
If temporary boot is unsupported, that does not establish that permanent flashing
is safe. Installation requires a compatible recovery/flasher able to run AK3,
read the correct device properties, and access the selected boot block device.

After any successful test, check logs and kernel release plus display, touch,
Wi-Fi, mobile data, camera, audio, charging, suspend/resume and vendor module loads.
A boot animation or working KernelSU manager alone is insufficient validation.

## Sources inspected

- User workflow: https://github.com/ctl4100wl/5.15-android13-tapas/blob/main/.github/workflows/main.yml
- Pinned AK3 sample: https://github.com/osm0sis/AnyKernel3/blob/020dfeccf9d7e962a48400fc94d3e451df92eead/anykernel.sh
- Pinned AK3 core: https://github.com/osm0sis/AnyKernel3/blob/020dfeccf9d7e962a48400fc94d3e451df92eead/tools/ak3-core.sh
- Pinned AK3 documented options: https://github.com/osm0sis/AnyKernel3/blob/020dfeccf9d7e962a48400fc94d3e451df92eead/README.md
