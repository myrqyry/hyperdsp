"""Fail if a required DSP, frontend or fixture test disappeared or was skipped."""
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

expected = (Path(__file__).with_name("ci-test-inventory.txt")).read_text().splitlines()
cases = ET.parse(sys.argv[1]).findall(".//testcase")
names = [case.get("name") for case in cases]
if sorted(names) != sorted(expected) or len(cases) < int(sys.argv[2]):
    raise SystemExit(f"Incomplete test inventory: missing={set(expected)-set(names)}, extra={set(names)-set(expected)}")
for case in cases:
    if case.find("failure") is not None or case.find("error") is not None or case.find("skipped") is not None or case.get("status") in ("notrun", "disabled"):
        raise SystemExit(f"Unsuccessful test: {case.get('name')}")
print(f"Verified {len(cases)} required DSP, libobs, Qt and LADSPA cases")
