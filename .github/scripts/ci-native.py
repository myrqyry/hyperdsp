"""Native HyperDSP qualification with all maintained DSP, Qt and LADSPA tests."""
import os
from pathlib import Path
import plistlib
import shutil
import sys
import tarfile

import native_caller as caller
import native_sdk as sdk

MODULE = 'hyperdsp'
DEPS = (
    ('jansson', 'https://codeload.github.com/akheron/jansson/tar.gz/refs/tags/v2.14.1', '979210eaffdffbcf54cfc34d047fccde13f21b529a381df26db871d886f729a4'),
    ('samplerate', 'https://codeload.github.com/libsndfile/libsamplerate/tar.gz/refs/tags/0.2.2', '16e881487f184250deb4fcb60432d7556ab12cb58caea71ef23960aec6c0405a'),
    ('ladspa', 'https://deb.debian.org/debian/pool/main/l/ladspa-sdk/ladspa-sdk_1.17.orig.tar.gz', 'd9d596171d93f9c226fcdb7e27c6f917422ac487efe2c05e0a18094df4268061'),
)


def configure(root, work, target, version):
    deps = work / 'hyperdsp-deps'
    deps.mkdir()
    for name, url, sha in DEPS:
        archive = deps / (name + '.tar.gz')
        sdk.download(url, archive, sha)
        with tarfile.open(archive) as tar:
            tar.extractall(deps, members=[m for m in tar.getmembers() if not (m.issym() or m.islnk())], filter='data')
    return ['-DHYPERDSP_NATIVE_SDK=ON', '-DHYPERDSP_PORTABLE_DEPS=ON',
            '-DHYPERDSP_NATIVE_FIXTURE=ON', '-DHYPERDSP_REQUIRE_BUILDER=ON',
            '-DHYPERDSP_BUILD_BUILDER=ON', '-DHYPERDSP_BUILD_CLOCK_SYNC=ON', '-DBUILD_TESTING=ON',
            '-DHYPERDSP_PACKAGE_LAYOUT=modern', f'-DHYPERDSP_DEPS_ROOT={deps.as_posix()}',
            f'-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}']


def runtime_env(work, target, version):
    obs = work / f'obs-studio-{version}'
    obs_deps, qt = sdk.dependency_dirs(obs, target, version)
    roots = [work / 'sdk', work / 'obs-build', obs_deps, qt]
    env = os.environ.copy()
    home = work / 'test-home'
    home.mkdir()
    for key in ('HOME', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_DATA_HOME'):
        path = home / key.lower()
        path.mkdir()
        env[key] = str(path)
    env.pop('HYPERDSP_AUDITION_TEST_SINK', None)
    env['QT_QPA_PLATFORM'] = sdk.qt_test_platform(qt, target)
    env['QT_PLUGIN_PATH'] = str(qt / 'plugins')
    if target == 'windows-x64':
        dirs = sorted({p.parent for r in roots for p in r.rglob('*.dll')})
        env['PATH'] = os.pathsep.join([*(str(p) for p in dirs), str(Path(sys.executable).parent), env.get('PATH', '')])
    else:
        dirs = sorted({p.parent for r in roots for p in r.rglob('*.dylib')})
        frameworks = sorted({p.parent for r in roots for p in r.rglob('*.framework') if p.is_dir()})
        env['DYLD_LIBRARY_PATH'] = os.pathsep.join(map(str, dirs))
        env['DYLD_FRAMEWORK_PATH'] = os.pathsep.join(map(str, frameworks))
    return env


def stage(root, installed, stage_root, target, version):
    work = installed.parent
    build = work / 'build'
    env = runtime_env(work, target, version)
    if target == 'windows-x64':
        manifest = build / 'runtime-dlls-RelWithDebInfo.txt'
        linked = [Path(line) for line in manifest.read_text().splitlines() if line.strip()]
        module_dir = build / 'RelWithDebInfo'
        if not linked:
            raise ValueError('No linked runtime DLLs reported by CMake')
        for dll in linked:
            staged = module_dir / dll.name
            if not staged.is_file() or sdk.digest(staged) != sdk.digest(dll):
                raise ValueError(f'Wrong or missing test runtime DLL: {staged}')
        print('Windows linked test runtime DLLs: ' + ', '.join(p.name for p in linked), flush=True)
        # The package itself remains free of duplicated host SDK libraries.
        env['PATH'] = str(module_dir) + os.pathsep + env['PATH']
        # Test loading independently before running the full suite. Use a
        # fresh Python process so already-loaded Qt cannot mask dependencies.
        # LOAD_WITH_ALTERED_SEARCH_PATH includes the DLL directory and PATH,
        # matching the search used by libobs rather than ctypes defaults.
        sdk.run(sys.executable, '-c',
                'import ctypes,sys; ctypes.WinDLL(sys.argv[1], winmode=8); print("HYPERDSP_DLL_LOAD_PASS", flush=True)',
                module_dir / 'hyperdsp.dll', env=env)
    report = work / 'native.xml'
    sdk.run('ctest', '--test-dir', build, '-C', 'RelWithDebInfo', '--output-on-failure', '--no-tests=error', '--output-junit', report, env=env)
    sdk.run(sys.executable, root / 'tests/verify-ci-report.py', report, '59')
    binary, data, notice = caller.layout(stage_root, MODULE, target, version)
    suffix = '.exe' if target == 'windows-x64' else ''
    module = installed / ('hyperdsp.dll' if suffix else 'hyperdsp.so')
    if not suffix and not module.is_file():
        module = installed / 'hyperdsp.dylib'
    tools = ('hyperdsp-ladspa-probe'+suffix, 'hyperdsp-effects'+suffix, 'scan-ladspa.py')
    dep_root = work / 'hyperdsp-deps'
    assets = {'LICENSE': root/'LICENSE', 'data/LICENSE': root/'LICENSE',
              'data/licenses/jansson/LICENSE': dep_root/'jansson-2.14.1/LICENSE',
              'data/licenses/libsamplerate/LICENSE': dep_root/'libsamplerate-0.2.2/COPYING',
              'data/licenses/ladspa-sdk/LICENSE': dep_root/'ladspa_sdk_1.17/doc/COPYING',
              'tools/scan-ladspa.py': root/'tools/scan-ladspa.py'}
    for path in (root/'examples/graphs').rglob('*.json'):
        assets['data/graphs/'+path.relative_to(root/'examples/graphs').as_posix()] = path
    expected = {module, *(installed / name for name in assets), *(installed/'tools'/tool for tool in tools)}
    entries = list(installed.rglob('*'))
    if any(p.is_symlink() for p in entries) or {p for p in entries if p.is_file()} != expected:
        raise ValueError('Unexpected native install inventory')
    for name, source in assets.items():
        if sdk.digest(installed/name) != sdk.digest(source):
            raise ValueError('Changed package asset: '+name)
    binary.parent.mkdir(parents=True)
    shutil.copy2(module, binary)
    shutil.copytree(installed/'data', data)
    # macOS scanner discovers Resources/tools; Windows retains plugin-root/tools.
    tool_dir = data/'tools' if target.startswith('macos-') else notice.parent/'tools'
    shutil.copytree(installed/'tools', tool_dir)
    if not notice.exists():
        shutil.copy2(installed/'LICENSE', notice)
    if target.startswith('macos-'):
        (binary.parent.parent/'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': MODULE, 'CFBundleIdentifier': 'io.github.myrqyry.hyperdsp', 'CFBundleName': MODULE, 'CFBundlePackageType': 'BNDL', 'CFBundleVersion': '0.1.0'}))
        for tool in tools[:2]:
            sdk.run(*sdk.mac_arch_command(tool_dir/tool, target))
            sdk.run('codesign', '--force', '--sign', '-', tool_dir/tool)
            sdk.run('codesign', '--verify', '--strict', tool_dir/tool)
    # Exercise the staged binary, scanner and CLI, not just build-tree files.
    def find_one(name):
        matches = list(build.rglob(name))
        if len(matches) != 1:
            raise ValueError(f'Expected one {name}, got {matches}')
        return matches[0]
    amp = find_one('amp.dll' if suffix else 'amp.so')
    sdk.run(find_one('verify-catalog-obs'+suffix), binary, data, amp, env=env)
    catalog = work/'stage-catalog.json'
    sdk.run(sys.executable, tool_dir/'scan-ladspa.py', '--probe', tool_dir/tools[0], '--output', catalog, amp, env=env)
    sdk.run(sys.executable, root/'tests/verify-library-cli.py', tool_dir/tools[1], catalog, env=env)
    print(f'Native package verified: {len([p for p in stage_root.rglob("*") if p.is_file()])} files; all 59 tests and installed tools')
    return binary
