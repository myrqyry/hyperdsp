"""Test the real extracted package, including installed LADSPA tools."""
from pathlib import Path
import subprocess
import sys
import tarfile
import os

ROOT = Path(__file__).resolve().parents[1]
archive, build, version, output = sys.argv[1:]
build, output = Path(build).resolve(), Path(output).resolve()
module = "bin/64bit/hyperdsp.so" if version == "32.2.0" else "hyperdsp.so"
if version not in ("32.2.0", "33.0.3"):
    raise SystemExit("Unsupported version")
graphs = {p.relative_to(ROOT / "examples/graphs").as_posix(): p for p in (ROOT / "examples/graphs").rglob("*.json")}
expected = {module, "LICENSE", "data/LICENSE", "tools/hyperdsp-ladspa-probe",
            "tools/hyperdsp-effects", "tools/scan-ladspa.py"} | {f"data/graphs/{n}" for n in graphs}
expected = {f"hyperdsp/{name}" for name in expected}
with tarfile.open(archive, "r:gz") as tar:
    members = tar.getmembers()
    if any(not m.isfile() for m in members) or len(members) != len(expected) or {m.name for m in members} != expected:
        raise SystemExit("Unexpected installed package inventory")
    output.mkdir()
    tar.extractall(output, filter="data")
plugin = output / "hyperdsp"
for name in ("LICENSE", "data/LICENSE"):
    if (plugin / name).read_bytes() != (ROOT / "LICENSE").read_bytes():
        raise SystemExit("License mismatch")
for name, source in graphs.items():
    if (plugin / "data/graphs" / name).read_bytes() != source.read_bytes():
        raise SystemExit(f"Preset mismatch: {name}")
if (plugin / "tools/scan-ladspa.py").read_bytes() != (ROOT / "tools/scan-ladspa.py").read_bytes():
    raise SystemExit("Scanner script mismatch")
for tool in ("hyperdsp-ladspa-probe", "hyperdsp-effects"):
    if not os.access(plugin / "tools" / tool, os.X_OK):
        raise SystemExit(f"Tool executable mode lost: {tool}")
subprocess.run(["xvfb-run", "-a", str(build / "verify-catalog-obs"), str(plugin / module), str(plugin / "data"), "/usr/lib/ladspa/amp.so"], check=True)
catalog = output / "catalog.json"
subprocess.run([sys.executable, str(plugin / "tools/scan-ladspa.py"), "--probe", str(plugin / "tools/hyperdsp-ladspa-probe"), "--output", str(catalog), "/usr/lib/ladspa/amp.so"], check=True)
subprocess.run([sys.executable, str(ROOT / "tests/verify-library-cli.py"), str(plugin / "tools/hyperdsp-effects"), str(catalog)], check=True)
print(f"Verified extracted {version} layout, installed scanner, effect CLI and {len(graphs)} presets")
