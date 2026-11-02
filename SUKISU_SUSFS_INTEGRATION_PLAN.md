# SukiSU / SUSFS Integration Diagnosis and Plan

## Evidence

The failing workflow is `.github/workflows/fastbuild_6.12.23.yml`.
Actions run 41324294873 used workflow commit
`d81836eecc9bba68e62ad58a7350d402d34ad7a1`.

The latest user-provided linker output no longer reports the original
`ksu_handle_faccessat` and `ksu_handle_sys_reboot` failures. It reports:

| Missing symbol | Required integration |
| --- | --- |
| `ksu_handle_stat` | Parsed-filename sucompat handler |
| `ksu_is_init_rc_hook_enabled` | Init-rc static key and lifecycle |
| `ksu_handle_execveat` | Boot-event and sucompat handler |
| `ksu_handle_post_execveat_sucompat` | Post-exec su-session handling |
| `ksu_handle_sys_read` | Globally visible init-rc read handler |

Clean local source revisions researched:

- SukiSU-Ultra: `56d45a19193a41bf42c8db655c9d2cfba44c48f1`.
- susfs4ksu: `925d85529e635fbcb1081f5f9ef59a9667e44d23`.

These are research baselines, not independently verified runner checkout SHAs.
Public Actions metadata was inspected; full runner logs require authentication.

## Root Cause

The workflow pairs a SUSFS v2 direct-hook kernel patch with SukiSU's
unconverted syscall-register hook implementation. Its Kconfig-only patch and
two-function compatibility adapter implement only part of the required contract.

Additional defects cannot be diagnosed by missing symbol names alone:

- SUSFS expects `ksu_su_compat_enabled` to be a static key; SukiSU defines a bool.
- `ksu_handle_execveat_sucompat` has the same name but incompatible argument
  types in the two implementations. C linking does not validate this contract.
- SUSFS requires initialization, boot events, process marking, and SELinux SID
  integration, not just globally visible functions.
- A dry-run of the attached SUSFS companion patch fails against SukiSU in eight
  files: Kbuild, core/init.c, hook/setuid_hook.c, policy/allowlist.c,
  policy/app_profile.h, runtime/boot_event.c, runtime/ksud_integration.c, and
  supercall/dispatch.c.
- The workflow fetches cctv18/susfs4oki, not the attached susfs4ksu development
  branch. Their preinit and userspace interfaces must not be mixed blindly.
- Kernel 6.12 supports both `kernelsu-objs` and `kernelsu-y`; changing between
  those object lists was not a demonstrated root-cause fix.

## Implementation Plan

1. Pin the kernel, SukiSU, and vendor SUSFS revisions. Fetch repository-owned
   integration files from the workflow commit, not a moving branch.
2. Replace the partial adapter with a complete SUSFS companion integration
   adapted to the pinned SukiSU source. Preserve SukiSU manager and extension
   functionality; do not substitute another KernelSU fork silently.
3. Use one coherent hook mode. Convert relevant code to the direct-hook ABI,
   static keys, init-rc and boot-event lifecycle, process marking, and SELinux
   integration. Handle preinit compatibility explicitly.
4. Fail closed on patch conflicts. Validate patch applicability, source symlink,
   effective config, and every required integration entry point.
5. Compile affected objects and inspect their built-in archive. Then build Image
   with fresh output and without restored ccache for the first validation run.
6. Separately validate device boot, root grants, modules, and SUSFS commands.
   Successful compilation alone is not proof of runtime correctness.

## Status

- Diagnosis and implementation plan exported.
- Complete companion adaptation exported as `other_patch/sukisu_susfs_full.patch`.
- Matched vendor kernel patch exported as `other_patch/sukisu_susfs_kernel.patch`.
- Workflow uses the paired patches, strict applicability checks, a separate cache
  key, and built-in archive verification. The earlier adapter is no longer used.
- Full Image build with the Actions toolchain and device validation pending.

## Implemented Source Pins

| Component | Commit |
| --- | --- |
| cctv18 kernel | `1fdec1073c8fa24282dcbe8395593759c2ce0d17` |
| SukiSU-Ultra | `56d45a19193a41bf42c8db655c9d2cfba44c48f1` |
| cctv18/susfs4oki | `5f236a7b573c5a21ad0e5871ee96b63b89486596` |

Repository-owned patches are downloaded from `GITHUB_SHA`.
The kernel patch re-anchors one SELinux hunk to the pinned vendor tree's SECMARK
comment; the upstream Android KMI comment was absent in that tree.

The adaptation preserves SukiSU's manager, extensions, version metadata,
mount-hide initialization, and the ARM64 text-patching helper used by mount-hide.
It removes the duplicate syscall-hook implementation and uses SUSFS direct hooks.
Preinit is explicitly disabled with `KSU_NO_PREINIT` for this integration; it must
not be reported as supported until a compatible preinit path is implemented.

## Verification Completed

- `git apply --check` passes for the companion patch on untouched pinned SukiSU.
- The matched kernel patch applies cleanly to the pinned vendor kernel.
- ARM64 `gki_defconfig` generation passes.
- The complete SukiSU directory and SELinux directory compile.
- Every SUSFS-modified kernel C object and `fs/susfs.o` compile.
- All thirteen checked direct-hook functions and static keys are present in
  SukiSU's `built-in.a`.
- Comparing integration references with definitions across the compiled objects
  finds no unresolved `ksu_`, `__ksu_`, `susfs_`, or `my_security_` interfaces.
- `actionlint` and `bash -n` for every workflow run block pass.

Compilation used unwrapped Clang from a temporary Nix environment, not the
workflow's Android Clang 19 distribution. One newer-Clang warning in untouched
networking headers was suppressed locally with
`-Wno-default-const-init-var-unsafe`; that flag was not added to the workflow.
This check did not include Rust, the complete optional-feature combination,
full Image linking, packaging, or device runtime behavior. Those remain required
before treating the resulting kernel as ready to flash.
