import json
from pathlib import Path
import subprocess
import sys
import tempfile
cli, catalog = sys.argv[1:]
def run(*args):
    return json.loads(subprocess.check_output([cli, "--catalog", catalog, "--json", *args], text=True))
result=run()
assert result["schema"]==1 and len(result["effects"])==40
assert sum(e["format"]=="native" for e in result["effects"])==38
assert len({e["id"] for e in result["effects"]})==40
items=run("--search","amplifier stereo","--channels","1")["effects"]
assert len(items)==1 and items[0]["label"]=="amp_stereo" and not items[0]["compatible"]
items=run("--search","vowel morph","--format","native")["effects"]
assert len(items)==1 and items[0]["source"]=="hyperdsp_formant_filter" and items[0]["mode"]==1
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp)/"stale.json"
    data=json.loads(Path(catalog).read_text())
    data["effects"][0]["path"]=str(Path(temp)/"missing.so")
    data["effects"].append(data["effects"][0])
    path.write_text(json.dumps(data))
    stale=json.loads(subprocess.check_output([cli,"--catalog",str(path),"--json"],text=True))
    assert len(stale["effects"])==40
    assert sum(not e["compatible"] for e in stale["effects"])==1
    missing=json.loads(subprocess.check_output([cli,"--catalog",str(Path(temp)/"absent.json"),"--json"],text=True))
    assert len(missing["effects"])==38
assert subprocess.run([cli,"--channels","0"],capture_output=True).returncode==2
print("LIBRARY_CLI_PASS=1")
