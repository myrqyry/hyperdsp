# CI qualification

Shared obs-plugin-ci pin e9eb704437e606721ee92d7d064c4b8168fd8f53.
Full profile: OBS 32.2.0 and 33.0.3 on Linux x86-64, Windows x64, macOS Intel and macOS Apple Silicon.
The first HyperDSP CI workflow originally required all 54 named tests; the current inventory is 59; the
JUnit gate rejects missing, extra, skipped or failed tests. Qt Builder,
Clock Sync and LADSPA SDK fixture support must remain enabled.
The caller uses the shared SDK shell recipe, Rust pack and payload-manifest
commands instead of local archive writers. Versioned runtime packages
retain the required trusted project-built helper tools, 11 graph presets
and license. Source is exported from HEAD without agent/project-state
files; the manifest verifies the actual binary archive payload.

## Current qualification

PR #2 merged at 5a09e5c. The exact final PR head 2b7dafe passed [run 46088111680](https://github.com/myrqyry/hyperdsp/actions/runs/46088111680), following the all-green [run 46084707743](https://github.com/myrqyry/hyperdsp/actions/runs/46084707743) at c6fea47.

| Target | OBS 32.2.0 | OBS 33.0.3 |
| --- | --- | --- |
| Linux x86-64 | 59/59, package passed | 59/59, package passed |
| Windows x64 | 59/59, package passed | 59/59, package passed |
| macOS Intel | 59/59, package passed | 59/59, package passed |
| macOS Apple Silicon | 59/59, package passed | 59/59, package passed |

All eight passing PR legs retain the Builder, Clock Sync, native LADSPA fixture, package checks and installed helper tests. Windows stages CMake-selected SDK runtime DLLs only in the build tree, checks their digests and performs a fresh DLL load preflight before CTest. The Windows stack-limited native-filter test uses heap-owned engines; no production processing was changed. Missing and empty runtime manifests have distinct diagnostics. Native packages do not duplicate host SDK DLLs. Native Windows/macOS audible audition is still unimplemented. These tests qualify the specified SDKs and package/runtime fixtures, not physical device playback, arbitrary third-party effects, desktop interaction or universal hard real-time performance.

Post-merge [main run 46089690710](https://github.com/myrqyry/hyperdsp/actions/runs/46089690710) exposed two Apple Silicon failures after the green PR: OBS 32 Color fallback RMS 0.321089 versus dry 0.282843, and OBS 33 LADSPA contention fixture with zero sample mismatches but zero property reads (14 settings updates). Both reached 58/59. This is a repeatability issue to investigate, not proof of a new DSP regression or a reason to weaken assertions. The PR qualification table above is for the exact tested PR head, not a claim that every subsequent run passes.

Merged main was rebuilt locally against OBS 32.2.0: all 59 required cases passed in 38.81 seconds, with exact JUnit inventory verification. The staged 20-file legacy package passed dynamic library resolution, actual libobs catalog, installed scanner/CLI and isolated mixed-output capture (187 blocks, 191488 sample frames per track, zero errors/missing/invalid). Installed package content matched staging byte-for-byte and the installed-module catalog smoke test passed in an isolated configuration. Existing desktop OBS was not restarted; the live session and physical audio remain separate qualification.

## Historical qualification log

The following entries record earlier checkpoints and their then-current blockers. Later successful runs supersede earlier pending status; test counts and observations are retained for provenance.

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
matrix coverage by disabling features. No visibility changes are performed by qualification.

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

## Hosted OBS 33 regression (run 45919855507)

OBS 32.2 passed 54/54; OBS 33.0.3 compiled and passed 52/54, with failures
in the Effect Builder canvas and Spectral partition test. The Builder's
custom cable press selected the cable, but its unmatched Qt base-class
release could clear that selection. Paired event handling now preserves
selection through release. The widget test chooses an exposed point on
the intended wire, verifies actual selection and disconnect/reconnect,
and exercises last-input rejection instead of relying on an unchanged
JSON result when no cable was selected.

The exact Spectral failure reproduced locally with GCC 15.2.0 at
`-O3 -DNDEBUG -march=x86-64-v3`: Smear 44.1 kHz, first mismatch sample 1254,
maximum absolute difference 6.23614e-6 (limit 1e-6). GCC15 baseline passed;
contract-off, vectorization-off and no-inline experiments isolated a
compiler-specialization/contraction difference. Keeping the FFT-hop body
out of line restores identical block/scalar results, with no changes to
algorithm, tolerance, FFT size or global optimization options. GCC15
baseline/v3/native and GCC13/Clang18 native builds now report zero partition
and latency-alignment error. The same native test logs the first mismatch
and maxima if a future compiler regresses. Hosted OBS33 rerun is required
for confirmation; this is not a new hardware or listening benchmark.

## Native matrix pilot

The hosted Linux rerun at bcbb632 (run 45942590209) passed both OBS
32.2.0 and OBS 33.0.3 with the complete 56-test inventory. The native
Windows x64, macOS Intel, and macOS arm64 caller now uses shared CI
engine e9eb704 and the pinned native libobs/frontend imported targets.
The existing Linux recipe and artifact policy remain unchanged.

Native dependency source archives are SHA-256 pinned in
`.github/scripts/ci-native.py`: Jansson 2.14.1 (MIT), libsamplerate
0.2.2 (BSD), LADSPA SDK 1.17 (LGPL API header). The first two are built
statically and notices are staged. No unrelated plugin runtime libraries
are copied. The native fixture is a test-only amplifier implementing the
SDK example port contract, built for the target architecture and excluded
from the package. All 56 HyperDSP cases are required on each native leg;
no dependency self-tests or skipped required project cases are permitted.
The native hook also tests the staged module's catalog callback and actual
installed probe/scanner/CLI, verifies notice and preset hashes, checks the
package inventory and creates the correct Windows layout or macOS bundle.
The SDK and runtime test paths are isolated. Python 3 remains a required
external scanner dependency. The standalone CLI requires the matching OBS
runtime to be discoverable by the system loader.

Manual portable dependency build: unpack the three pinned source archives
into a user-owned directory with their original top-level directory names,
then configure with `-DHYPERDSP_PORTABLE_DEPS=ON`,
`-DHYPERDSP_DEPS_ROOT=/path/to/deps`, `-DHYPERDSP_NATIVE_FIXTURE=ON`,
`-DHYPERDSP_REQUIRE_BUILDER=ON`, `-DBUILD_TESTING=ON`.
On Windows/macOS also use `-DHYPERDSP_NATIVE_SDK=ON` and configure the
matching OBS libobs/frontend and Qt6 SDK locations through CMake.
Native qualification requires actual hosted results, not just CMake
configuration. A/B isolated audition rendering is retained; audible output
selection/playback on Windows and macOS is still unimplemented (Linux
PulseAudio/PipeWire tools remain unchanged). No native audio device or
physical listening claims are made by the CI suite.

Local portable source-dependency qualification: all 56 project cases in
the first run passed (13 dependency self-tests were inadvertently registered
but not built; corrected the CMake cache scope and independently verified
the exact 56-name registration). Native imported SDK configuration also
registered exactly 56 cases against the local OBS SDK. The 20-file staged
Linux package passed actual libobs catalog callback, probe/scan, CLI and
Unicode-path discovery. The first staged CLI run exposed a missing return
when extracting the common entry point; explicit return and rerun passed.
These are same-host Linux results, not Windows/macOS runtime confirmation.


## Reconciled real-time optimization qualification (2026-11-27)

The previous full matrix (run 45960030248) passed both Linux legs but all
six Windows/macOS legs stopped at dependency configuration: CMake 4 removed
compatibility with the old Jansson minimum policy version. Reproduced on
Linux with CMake 4.2.3; after addressing Jansson, the same issue surfaced in
libsamplerate. Both pinned dependencies now use a dependency-scoped policy
floor of 3.5 on CMake 4+, preserving a caller's higher floor. Archive
versions and hashes remain unchanged. No test or feature is disabled.

Reconciled the real-time optimization branch with main, including the local
Builder theme checkpoint 0e740cf. A fresh Linux Release build using
CMake 4.2.3, the pinned source dependencies and the native amplifier fixture
registered exactly 59 tests; 59/59 passed (51.91 s), and the JUnit inventory
gate accepted all 59. The staged 20-file package passed the real libobs
catalog, installed scanner, CLI and isolated three-track mixed-output
tests (187 contiguous blocks, zero errors). GCC 15.2 x86-64-v3 FFT and
spectral tests passed with zero packet-partition error.

Both hosted Linux and native callers require all 59 cases. Hosted
qualification of the reconciled commit is still required before claiming
Windows/macOS support; a local CMake 4 result does not substitute for it.


## Native Qt SDK test-driver correction (2026-11-27)

Both Linux legs passed for 9700cec (workflow dispatch 46065496698 and
PR #2 run 46065660442). The six native dispatch logs now fail at
`Required Qt Test unavailable`, after getting past the dependency policy
failure. Local configuration with Qt6Test discovery disabled reproduced
the error. Replaced only the test driver with a small Qt Widgets event
helper that delivers real press/move/release and key events and pumps
the event loop. Every existing Builder and filter-analysis assertion and
interaction is retained; the three widget cases remain mandatory whenever
the required Builder is enabled. No separate Qt runtime, production
behavior change or reduced inventory. The three affected cases passed
locally without QtTest, with the full 59-case portable-dependency suite
used as the regression gate.

## First native test execution (2026-11-27)

PR #2 run 46069144154 at 10801c4: Linux 2/2 passed; all four
macOS targets compiled and reached all 59 CTests (50–52 passed per
target). Windows stopped at MSVC C2375 in the test-only LADSPA
amplifier (SDK declaration versus dllexport redeclaration).
Export the existing C-linkage symbol through a Windows .def file
instead; a small x86-64 Windows-target Clang/LLD PE/COFF link test
using the pinned SDK declaration confirmed the exact ladspa_descriptor
export. This is not a full Windows build.

macOS timeouts affected long paced OBS integration tests; logs show
source timestamp drift, and graph-recovery took 20.88 s versus roughly
four seconds locally. Use absolute steady-clock packet deadlines in
the eleven paced source fixtures instead of accumulating sleep_for
overshoot. No packet counts, signal checks, or timeout limits changed.
The native timeout cause is not proven until the rerun. Filter-analysis
also intermittently failed numeric/region edit assertions; use the
C locale for numeric input and print entered text, focus, committed
frequency and band settings. The widget test driver now has per-test
instance gesture state, explicit event modifiers, numeric ASCII key
mapping, and a precise timer/event-loop wait. Review suggestions
addressed without changing production code or removing assertions.

Local CMake 4.2.3 portable-dependency build: 59/59 tests passed
(38.59 s), exact JUnit inventory verified. Native CI must still
confirm the fixes; this does not establish full Windows/macOS support.

## Windows-only remaining qualification (2026-11-27)

Run 46081948114 at 4788bdd passed both Linux and all four macOS
legs, including package verification. Both Windows targets compiled
and ran CTest (35/59 passed each). 23 tests failed to load the plugin
with Windows error 126; the Builder test, which links the frontend
and Widgets directly, passed. This points to dependency discovery in
the dynamically loaded test process, not a reported DSP mismatch.
Stage CMake-selected runtime DLLs beside the Windows build-tree test
executables, verify their digests against the SDK, and perform a
fresh-process DLL load preflight. Do not include these copies in the
installed plugin package. The missing dependency itself has not yet
been identified; native rerun must confirm the correction.

The other Windows failure was the standalone native-filter test.
Reproduced on Linux with a 1 MiB stack limit (exit 139): nested
NativeFilter instances contain more than 1 MiB of comb delay storage.
Allocate the test engines on the heap, as the production adapter
already does. The same limited-stack test now passes, preserving all
numerical and reset assertions. Full local portable dependency
regression: 59/59 passed in 38.80 seconds, exact JUnit inventory
verified. Windows-specific DLL staging and load preflight await CI.

## Post-merge repeatability corrections

Main run 46089690710 finished with six platform legs passing and two
Apple Silicon failures. Color measured fallback before the external
snapshot's documented 500 ms validity window elapsed; LADSPA's unpaced
history fixture completed only 14 updates, before its first property
read at update 16. These conditions were reproduced with the production
module on Linux: a 350 ms delayed Sound read caused fallback RMS
0.586232 (expected 0.282843), and 25 ms spacing of LADSPA structural
updates caused one update and no property reads despite zero waveform
errors over 480000 samples.

The Color fixture retains the delayed producer and advances paced
audio through expiry and ramp settling before its unchanged capture
window. Shared history and latency fixtures require bounded control
progress between source segments, including an entire property-read
interval before the final quarter when metadata checks are enabled.
The writer remains active during audio submission; waits occur only
in the test driver, never inside an audio callback. The LADSPA
replacement fixture retains its deliberate writer throttle. No
production DSP, sample counts, tolerances, CTest timeout or inventory
changed. Focused Linux rerun: Color fallback 0.282843; LADSPA 25
structural edits, one metadata read, zero errors. Full Linux suite
59/59 in 39.16 seconds, exact JUnit inventory verified. Native rerun
is needed to establish repeatability on the previously failing host.
