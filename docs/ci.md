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
