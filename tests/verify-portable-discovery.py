import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

scanner, probe, plugin = (Path(p).resolve() for p in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='hyperdsp-discovery-') as temp:
    root=Path(temp)
    directory=root/'effects with spaces café'; directory.mkdir()
    # macOS LADSPA libraries may use .dylib and Windows uses .dll.
    # The same native fixture under another suffix tests discovery, not foreign ABI.
    suffix='.dll' if sys.platform=='win32' else '.dylib' if sys.platform=='darwin' else '.SO'
    candidate=directory/('amp'+suffix)
    shutil.copy2(plugin,candidate)
    output=root/'catalog.json'
    subprocess.run([sys.executable,str(scanner),'--probe',str(probe),'--output',str(output),str(directory)],check=True)
    data=json.loads(output.read_text())
    assert {e['label'] for e in data['effects']}=={'amp_mono','amp_stereo'}, data
    assert not data['failed'], data
    assert {Path(e['path']) for e in data['effects']}=={candidate.resolve()}
    assert {e['path'] for e in data['effects']}=={candidate.resolve().as_posix()}
    print('PORTABLE_DISCOVERY_PASS=1')
