import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

scanner, probe, plugin = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    first=root/"first";first.mkdir()
    second=root/"second";second.mkdir()
    (first/"amp.so").symlink_to(plugin.resolve())
    (second/"alias.so").symlink_to(plugin.resolve())
    output=root/"catalog.json"
    env=dict(os.environ,LADSPA_PATH=os.pathsep.join(map(str,[first,second])))
    result=subprocess.run([sys.executable,str(scanner),"--probe",str(probe),"--output",str(output)],env=env,check=True,capture_output=True,text=True)
    data=json.loads(output.read_text())
    assert data["schema"]==1 and len(data["effects"])==2, result.stdout
    assert {e["label"] for e in data["effects"]}=={"amp_mono","amp_stereo"}
    assert not data["failed"]
    assert all(e["controls"][0]["port"]==0 and e["controls_complete"] for e in data["effects"])
    # Explicit path arguments override environment, and an empty scan is valid.
    empty=root/"empty";empty.mkdir()
    subprocess.run([sys.executable,str(scanner),"--probe",str(probe),"--output",str(output),str(empty)],env=env,check=True)
    assert json.loads(output.read_text())["effects"]==[]
    print("CATALOG_PATH_PASS=1")
