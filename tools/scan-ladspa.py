#!/usr/bin/env python3
"""Explicit, out-of-process LADSPA discovery; never loads libraries in OBS."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sysconfig
import math

def category(name):
    groups = {
        "Dynamics": ("compress", "limit", "gate", "expander", "duck"),
        "EQ / Filter": ("equaliz", "eq", "filter", "pass", "shelf"),
        "Delay / Reverb": ("delay", "echo", "reverb"),
        "Modulation": ("chorus", "flanger", "phaser", "tremolo", "vibrato"),
        "Distortion": ("distort", "saturat", "overdrive", "clip", "tube"),
        "Pitch": ("pitch", "transpose", "harmon"),
        "Utility": ("amplif", "gain", "balance", "pan", "meter", "invert"),
    }
    return next((group for group, terms in groups.items() if any(t in name.lower() for t in terms)), "Other")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=Path(__file__).with_name("hyperdsp-ladspa-probe"))
    parser.add_argument("--output", type=Path, default=Path(os.environ.get("XDG_CONFIG_HOME", Path.home()/".config"))/"obs-studio"/"plugin_config"/"hyperdsp"/"ladspa-catalog.json")
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args()
    if not args.probe.is_file():
        parser.error("build hyperdsp-ladspa-probe and pass --probe")
    configured = os.environ.get("LADSPA_PATH")
    defaults = [Path.home()/".ladspa", Path.home()/".local/lib/ladspa",
                Path("/usr/lib/ladspa"), Path("/usr/local/lib/ladspa"), Path("/usr/lib64/ladspa")]
    if multiarch := sysconfig.get_config_var("MULTIARCH"):
        defaults += [Path("/usr/lib")/multiarch/"ladspa", Path("/usr/local/lib")/multiarch/"ladspa"]
    roots = args.paths or ([Path(p).expanduser() for p in configured.split(os.pathsep) if p] if configured is not None else defaults)
    paths = sorted({p.resolve() for root in roots for p in ([root] if root.is_file() else root.glob("*.so")) if p.is_file()})
    effects, failed = [], []
    for path in paths:
        if "\t" in str(path) or "\n" in str(path):
            continue
        try:
            proc = subprocess.run([str(args.probe.resolve()), str(path)], stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, errors="replace", timeout=3,
                                  cwd="/tmp")
        except (subprocess.TimeoutExpired, OSError):
            failed.append(str(path))
            continue
        if proc.returncode:
            failed.append(str(path))
            continue
        current = None
        expected = None
        def finish():
            if current is not None and expected is not None:
                current["controls_complete"] = len(current["controls"]) == expected
        for line in proc.stdout.splitlines():
            parts = line.split("\t")
            if len(parts)==6 and parts[0]=="C" and current is not None and expected is not None:
                try:
                    port, flags, lo, hi = int(parts[1]), int(parts[3]), float(parts[4]), float(parts[5])
                    if (0<=port<256 and 0<=flags<=65535 and math.isfinite(lo) and math.isfinite(hi)
                        and len(current["controls"])<256 and not any(c["port"]==port for c in current["controls"])):
                        current["controls"].append(dict(port=port,name=parts[2] or "Control",flags=flags,lower=lo,upper=hi))
                except ValueError:
                    pass
                continue
            finish()
            current, expected = None, None
            if len(parts) not in (4,5) or parts[3] not in ("1","2") or not parts[0] or not parts[1]:
                continue
            label,name,maker,channels = parts[:4]
            current=dict(path=str(path), label=label, name=name, maker=maker,
                         channels=int(channels), category=category(name+" "+label))
            if len(parts)==5 and parts[4].isdigit() and int(parts[4])<=256:
                expected=int(parts[4]);current["controls"]=[]
            effects.append(current)
        finish()
    effects.sort(key=lambda e: (e["name"].casefold(), e["path"], e["label"]))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w",dir=args.output.parent,delete=False) as f:
        json.dump(dict(schema=1,effects=effects,failed=failed),f,ensure_ascii=True)
        temp=f.name
    os.replace(temp,args.output)
    print(f"{len(effects)} compatible descriptors from {len(paths)} libraries; {len(failed)} probes failed/skipped; {args.output}")
if __name__ == "__main__":
    main()
