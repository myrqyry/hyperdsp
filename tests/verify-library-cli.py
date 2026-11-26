import json
from pathlib import Path
import subprocess
import sys
import tempfile
cli, catalog = sys.argv[1:]
def run(*args):
    return json.loads(subprocess.check_output([cli, "--catalog", catalog, "--json", *args], text=True))
result=run()
assert result["schema"]==1 and len(result["effects"])==42
assert sum(e["format"]=="native" for e in result["effects"])==40
assert len({e["id"] for e in result["effects"]})==42
assert sum(e["graph_supported"] for e in result["effects"])==42
color=next(e for e in result["effects"] if e["id"]=="native:hyperdsp_color_filter:3")
params={p["key"]:p for p in color["parameters"]}
assert params["drive"]["min"]==-24 and params["drive"]["max"]==36 and params["drive"]["default"]==0
assert [c["value"] for c in params["oversample"]["choices"]]==[1,2,4]
assert result["graph_utilities"][0]["id"]=="gain"
multiband=next(e for e in result["effects"] if e["id"]=="native:hyperdsp_multiband_filter:1")
mb={p["key"]:p for p in multiband["parameters"]}
stereo=next(e for e in run("--channels","1")["effects"] if e["source"]=="hyperdsp_stereo_filter")
assert not stereo["compatible"] and len(stereo["parameters"])==7
assert len(mb)==17 and not mb["low_hz"]["bindable"] and mb["low_ratio"]["bindable"]
amp=next(e for e in result["effects"] if e.get("label")=="amp_mono")
assert amp["controls_complete"] and amp["latency_known"] is False
assert next(p for p in amp["parameters"] if p["key"]=="port_0")["default"]==1
assert next(p for p in amp["parameters"] if p["key"]=="port_0")["bindable"] is True
assert params["drive"]["bindable"] is True and not params["oversample"].get("bindable",False)
assert run("--rate","96000")["sample_rate"]==96000
for effect in result["effects"]:
    if effect["graph_supported"]:
        assert effect["parameters"] and len({p["key"] for p in effect["parameters"]})==len(effect["parameters"])
flanger=next(e for e in result["effects"] if e["id"]=="native:hyperdsp_time_filter:4")
delay=next(p for p in flanger["parameters"] if p["key"]=="delay_ms")
assert delay["min"]==.1 and delay["max"]==10 and delay["default"]==2
formant=next(e for e in result["effects"] if e["id"]=="native:hyperdsp_formant_filter:2")
assert len(formant["parameters"])==17 and next(p for p in formant["parameters"] if p["key"]=="f3")["default"]==2900
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
    assert len(stale["effects"])==42
    assert sum(not e["compatible"] for e in stale["effects"])==1
    missing=json.loads(subprocess.check_output([cli,"--catalog",str(Path(temp)/"absent.json"),"--json"],text=True))
    assert len(missing["effects"])==40
assert subprocess.run([cli,"--channels","0"],capture_output=True).returncode==2
print("LIBRARY_CLI_PASS=1")
