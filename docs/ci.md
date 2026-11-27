# CI qualification

Shared obs-plugin-ci pin 359da119f3dd3d1e2d392cbf7e65f5766221cc68.
Linux profile: OBS32.2.0 on Ubuntu24 and OBS33.0.3 on Ubuntu26.
The first HyperDSP CI workflow originally required all 54 named tests; the current inventory is 59; the
JUnit gate rejects missing, extra, skipped or failed tests. Qt Builder,
Clock Sync and LADSPA SDK fixture support must remain enabled.
The caller uses the shared SDK shell recipe, Rust pack and payload-manifest
commands instead of local archive writers. Versioned runtime packages
retain the required trusted project-built helper tools, 11 graph presets
and license. Source is exported from HEAD without agent/project-state
files; the manifest verifies the actual binary archive payload.

Local qualification: fresh Release build against coherent installed OBS
32.2.0 SDK, 54/54 CTests passed in isolated Xvfb/home, 156.27 seconds.
Both package layouts independently extracted and verified, including
actual libobs LADSPA catalog callback, SDK amp.so scanner and installed
effect-library CLI. Before the scanner fix the real modern layout failed
CATALOG_OBS_PASS; after the fix both layouts passed. Each binary archive
has 17 regular files. Modern layout tested using the OBS32 binary, not
proof of OBS33 ABI/runtime support. Native hosted SDK qualification
remains pending. Windows/macOS need a real portability pass for POSIX
dynamic loading, scanner process lifecycle and dependencies; no nominal
matrix coverage by disabling features. Private repository stays private.

## Host portability checkpoint

The loader and discovery tools now share a native library boundary:
POSIX dlopen and Windows LoadLibraryExW with UTF-8 path conversion,
absolute library paths, and platform-specific discovery extensions.
The scanner owns a process group on POSIX and a kill-on-close job on
Windows. macOS bundle-relative Resources/tools discovery is supported
in the scanner. Two named regressions cover native discovery and
scanner start, result, cancellation and restart. Linux focused tests
passed; the new Windows branch has not been compiled or executed and
macOS has not been tested. Native dependency provisioning, an amplifier
fixture for each target, CMake module/bundle layout, native audio
audition, and native SDK qualification remain open. Linux CI remains
the only enabled profile. HyperDSP run 45917243129 was budget-blocked
before compilation. No change to repo visibility or billing.

Both extracted 17-file Linux packages passed real LADSPA scanner and
CLI checks. Full Linux regression subsequently passed all 56 named tests,
with zero failures or skips. Both layouts used the OBS32.2.0 binary;
this is not OBS33 ABI qualification.
