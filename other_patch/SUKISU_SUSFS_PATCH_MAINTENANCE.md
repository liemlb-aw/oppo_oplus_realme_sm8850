# SukiSU / SUSFS Patch Maintenance

This guide describes how to update the two patches consumed by the sm8850 build workflow:

- `sukisu_susfs_full.patch` modifies the SukiSU checkout under `KernelSU/`.
- `sukisu_susfs_kernel.patch` modifies the Android common kernel checkout under `common/`.

These patches form one integration. Do not update either one in isolation and assume the combination still works. SUSFS changes kernel call sites and ABI expectations; the SukiSU patch must provide the matching implementation, state, and lifecycle.

## Why Both Patches Exist

SUSFS v2's kernel patch expects direct KernelSU hook entry points and static keys. SukiSU's default syscall-hook path has different function signatures and ownership. A shim that defines only the symbols named in one linker error is insufficient: the linker may accept functions whose C ABI is wrong, and other call sites or boot-time behavior may still be missing.

The full SukiSU patch adapts the SUSFS companion integration to SukiSU while preserving SukiSU-specific manager and extension code. The kernel patch records the matching SUSFS kernel changes and any necessary hunk re-anchoring for the target kernel revision.

The original investigation found that:

- An early adapter removed the first two undefined symbols, but the next link exposed missing `ksu_handle_stat`, init-rc, execveat, and read hooks.
- The upstream SUSFS companion patch does not apply cleanly to SukiSU without adaptation.
- The same-named `ksu_handle_execveat_sucompat` APIs used different argument layouts. Linking alone cannot validate such an ABI mismatch.
- SukiSU's built-in extension advertisement is not a replacement for SUSFS initialization, process marking, SELinux SID setup, or direct-hook integration.

## Source Pairing

Always record and build against exact commit SHAs for all three inputs:

1. The Android common kernel source.
2. SukiSU Ultra.
3. The SUSFS fork and branch used by the workflow.

The workflow currently consumes `cctv18/susfs4oki` on its Android 16 / 6.12 development branch. Do not substitute the separate `simonpunk/susfs4ksu` checkout without confirming that its patch API and feature set match.

As of the current patch revision, local research used SukiSU `48b5e405271caf2d6a0fe9cf5c787090c4f9d67e` and the workflow pins SUSFS `5f236a7b573c5a21ad0e5871ee96b63b89486596`. Kernel commit `1fdec1073c8fa24282dcbe8395593759c2ce0d17` was used for earlier validation, but the corresponding workflow pin is commented out. These are historical maintenance baselines, not a guarantee of the revisions a run currently fetches. Before regenerating or releasing patches, pin the actual workflow inputs and verify the checked-out SHAs in its logs.

## Prepare a Scratch Workspace

Never regenerate patches in the original upstream checkouts. Use a disposable workspace and leave the repositories clean:

```bash
set -euo pipefail

WORK="$(mktemp -d /tmp/sukisu-susfs-refresh.XXXXXX)"
KERNEL_SHA="<full-kernel-commit-sha>"
SUKISU_SHA="<full-sukisu-commit-sha>"
SUSFS_SHA="<full-susfs-commit-sha>"

# Clone/fetch each source and detach at the exact workflow revisions.
git clone https://github.com/cctv18/android_kernel_common_oneplus_sm8850.git "$WORK/common"
git -C "$WORK/common" fetch origin "$KERNEL_SHA"
git -C "$WORK/common" checkout --detach FETCH_HEAD

git clone https://github.com/SukiSU-Ultra/SukiSU-Ultra.git "$WORK/KernelSU"
git -C "$WORK/KernelSU" checkout --detach "$SUKISU_SHA"

git clone https://github.com/cctv18/susfs4oki.git "$WORK/susfs4oki"
git -C "$WORK/susfs4oki" fetch origin "$SUSFS_SHA"
git -C "$WORK/susfs4oki" checkout --detach FETCH_HEAD

printf 'kernel=%s\nsukisu=%s\nsusfs=%s\n' \
  "$(git -C "$WORK/common" rev-parse HEAD)" \
  "$(git -C "$WORK/KernelSU" rev-parse HEAD)" \
  "$(git -C "$WORK/susfs4oki" rev-parse HEAD)"
```

Use a full clone when a three-way merge needs the patch's historical base objects. A shallow checkout can lack those objects and make `git apply --3way` fall back to a plain apply.

## Regenerate `sukisu_susfs_full.patch`

This patch is based on a SukiSU tree and includes the SUSFS companion changes adapted to that tree.

1. Inspect upstream changes in all affected kernel paths before applying anything:

   ```bash
   git -C "$WORK/KernelSU" diff --stat "$OLD_SUKISU_SHA..$SUKISU_SHA" -- kernel
   git -C "$WORK/KernelSU" diff "$OLD_SUKISU_SHA..$SUKISU_SHA" -- \
     kernel/core kernel/feature kernel/hook kernel/policy kernel/runtime \
     kernel/selinux kernel/sulog kernel/supercall kernel/Kbuild kernel/Kconfig
   ```

2. Read the matching SUSFS companion patch from the checked-out SUSFS revision:

   ```text
   kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch
   ```

   Treat it as the source of intended SUSFS behavior, not as a guaranteed clean patch for SukiSU. Apply it in the scratch tree using three-way application where possible. Resolve every conflict intentionally, preserving newer SukiSU behavior that is not part of the obsolete syscall-hook mechanism. Do not use `|| true`, ignore `.rej` files, or discard a file just to make patch application succeed.

3. Preserve the integration contract. At minimum, review these areas against the matching SUSFS kernel patch:

   - `ksu_su_compat_enabled` must have the static-key type and semantics expected by SUSFS.
   - sucompat, stat, faccessat, execveat, post-exec, and reboot handlers must use the exact argument signatures expected by the target kernel call sites.
   - init-rc read/fstat and volume-down handlers must be wired through their expected static keys.
   - `susfs_init()`, first-zygote / second-stage boot handling, process no-su/umounted marking, and SELinux SID refresh must run at the intended lifecycle points.
   - remove the old syscall hook / kprobe registrations where the SUSFS kernel patch replaces them. Keep unrelated SukiSU facilities, such as manager/extension paths and any shared text-patching helper still used by another feature.
   - decide explicitly whether preinit is supported. If its implementation requires the removed syscall table, disable its objects and provide the matching `KSU_NO_PREINIT` stubs; do not leave partial preinit references.
   - keep SukiSU's userspace API and SUSFS userspace command IDs in sync.

4. Resolve all conflicts and run whitespace checks. Inspect the complete diff, not only the linker symbols. Reconcile Kconfig, Kbuild, headers, initialization, exits, event handlers, and shared declarations.

5. Regenerate the patch from the exact clean base commit. Stage only intended integration files in the scratch checkout, then export a full-index binary-capable diff:

   ```bash
   git -C "$WORK/KernelSU" add -A kernel
   git -C "$WORK/KernelSU" diff --cached --check
   git -C "$WORK/KernelSU" diff --cached --binary --full-index "$SUKISU_SHA" -- kernel \
     > /path/to/oppo_oplus_realme_sm8850/other_patch/sukisu_susfs_full.patch
   ```

   Review that the resulting patch includes only intended `kernel/` changes and has no conflict markers or rejection files.

## Regenerate `sukisu_susfs_kernel.patch`

This patch must match both the selected kernel revision and SUSFS revision. The workflow copies SUSFS implementation files separately, then applies this patch to the kernel tree.

1. In the scratch kernel checkout, copy the SUSFS source and headers from the pinned SUSFS checkout, as the workflow does:

   ```bash
   cp "$WORK/susfs4oki/kernel_patches/fs/"* "$WORK/common/fs/"
   cp "$WORK/susfs4oki/kernel_patches/include/linux/"* "$WORK/common/include/linux/"
   ```

2. Start from the matching upstream kernel patch:

   ```text
   kernel_patches/50_add_susfs_in_gki-android16-6.12.patch
   ```

   First test it without changing the tree:

   ```bash
   git -C "$WORK/common" apply --check \
     "$WORK/susfs4oki/kernel_patches/50_add_susfs_in_gki-android16-6.12.patch"
   ```

   If a hunk rejects because the pinned kernel moved, re-anchor that hunk manually after comparing the exact old and new source. Preserve upstream kernel changes. Never ignore rejected hunks. Record intentional re-anchors in the commit and review their behavior.

3. Apply the upstream kernel patch and any reviewed target-specific adaptations. Verify the SUSFS implementation files, `CONFIG_KSU_SUSFS` definitions, and call sites are all present.

4. Export the complete diff from the pinned kernel base, including new tracked files:

   ```bash
   git -C "$WORK/common" add -A
   git -C "$WORK/common" diff --cached --check
   git -C "$WORK/common" diff --cached --binary --full-index "$KERNEL_SHA" -- . \
     > /path/to/oppo_oplus_realme_sm8850/other_patch/sukisu_susfs_kernel.patch
   ```

   This patch should contain kernel changes only. Do not include edits to `drivers/Makefile` or `drivers/Kconfig` if the workflow wires SukiSU through its setup script.

## Validate the Pair Before Updating the Workflow

Perform these checks against fresh scratch clones at the exact SHAs that the workflow will use:

```bash
# SukiSU-side patch
 git -C "$WORK/KernelSU" apply --check \
   /path/to/oppo_oplus_realme_sm8850/other_patch/sukisu_susfs_full.patch

# Kernel-side patch, after copying SUSFS fs/ and include/linux/ files
 git -C "$WORK/common" apply --check \
   /path/to/oppo_oplus_realme_sm8850/other_patch/sukisu_susfs_kernel.patch
```

Then build with the same target config and compiler family as CI:

1. Generate `gki_defconfig`; confirm `CONFIG_KSU=y` and `CONFIG_KSU_SUSFS=y` in `out/.config`.
2. Build SELinux generated headers / objects, then the full SukiSU built-in directory.
3. Compile every kernel object changed by `sukisu_susfs_kernel.patch`.
4. Inspect the SukiSU `built-in.a` with `llvm-nm --defined-only`. Verify all functions and keys referenced by the kernel patch are defined, including faccessat/stat, execveat/pre-exec/post-exec, init-rc read/fstat, reboot, setresuid, input-event, static keys, and SID helpers.
5. Search for stale references to removed syscall-hook or kprobe paths. Check the exact signatures in both declarations and definitions; symbol presence by itself is not enough.
6. Build a fresh `Image` without restoring an old ccache on the first integration run. Ccache keys should include the SukiSU SHA, SUSFS SHA, kernel SHA, and integration-patch revision.
7. Record compiler, config, source SHAs, patch SHAs, build result, and any warnings. After a successful build, validate boot, root grant, module management, and SUSFS commands on a device.

If any source SHA changes, repeat the applicability and focused compile checks for both patches. If the SUSFS or kernel patch rejects, rebase that hunk explicitly; do not patch only the reported missing symbol. If only SukiSU changes, rebase `sukisu_susfs_full.patch` and still compile its callers against the unchanged kernel patch.

## Update Workflow Pins

Before using regenerated patches in CI, update the source pins in `.github/workflows/fastbuild_6.12.23.yml` and enable the checkout assertions. At present, `KERNEL_COMMIT` and `SUKISU_COMMIT` are commented out; SUSFS is pinned. Make all three exact SHAs explicit for a validated release, and verify the log prints the matching checked-out revisions. Update the SUSFS branch name and upstream patch path if the kernel or Android version changes.

The workflow applies `sukisu_susfs_full.patch` to `KernelSU/` first, then copies SUSFS sources/headers and applies `sukisu_susfs_kernel.patch` to `common/`. Preserve this ordering and fail the run on either `git apply --check` or application failure.

## Current Known Baselines

These values describe the patch state after the November 7, 2026 SukiSU update; re-check them before reusing them:

- SukiSU upstream `main`: `9246a1c66e4945fb9d2333597ddeb9f087f24baa`.
- SUSFS workflow pin: `5f236a7b573c5a21ad0e5871ee96b63b89486596`.
- Kernel `1fdec1073c8fa24282dcbe8395593759c2ce0d17` was used in earlier isolated validation but is currently not enforced by the workflow.
- Since `48b5e405`, SukiSU added SELinux policy-generation tracking in `kernel/selinux/rules.c`, `sepolicy.c`, `sepolicy.h`, and `feature/selinux_hide.c`. The refreshed full patch is based on `9246a1c6`, keeps the new policy metadata APIs and calls `susfs_set_batch_sid()` after `reset_avc_cache(pol->latest_granting)`.
- `sukisu_susfs_kernel.patch` was not changed for this SukiSU-only update; its kernel and SUSFS bases are unchanged.

The refreshed full patch passed `git apply --check` and application on a clean SukiSU `9246a1c6` checkout. The hook adaptation's compiler validation was performed on the earlier SukiSU/kernel pair; re-run the listed compile and device checks for the new pair before release.
