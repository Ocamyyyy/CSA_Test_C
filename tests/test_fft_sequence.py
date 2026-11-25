#!/usr/bin/env python3
"""Five-mode sequence metadata/orchestration tests; no physical hardware."""
import hashlib,json,os,shlex,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CLI=shlex.split(os.environ.get('FFTTEST_COMMAND',str(ROOT/'build/ffttest-host')))
MOCK=shlex.split(os.environ.get('FFTTEST_SEQUENCE_MOCK',str(ROOT/'build/test_fft_sequence')))
PLAN=ROOT/'fft_sequence_plan.json';CONTRACT=ROOT/'fft_modes_hardware_contract.json';count=0
def call(cmd,ok,needle='',env=None):
 global count;p=subprocess.run(cmd,text=True,capture_output=True,timeout=20,env=env)
 assert (p.returncode==0)==ok,(cmd,p.returncode,p.stdout,p.stderr)
 assert needle in p.stdout,(needle,p.stdout);count+=1;return p
plan=json.loads(PLAN.read_text());assert plan['order']=='S2,S5,S7,S10,S2'
s2=ROOT/plan['s2_input'];s2m=json.loads((ROOT/plan['s2_manifest']).read_text());files={x['name']:x for x in s2m['files']}
assert len(s2.read_bytes())==8192 and hashlib.sha256(s2.read_bytes()).hexdigest()==files['fft_input.bin']['sha256']
paths=[s2,ROOT/plan['s5_input'],ROOT/plan['s7_input'],ROOT/plan['s10_input'],s2]
with tempfile.TemporaryDirectory() as td:
 d=Path(td);out=d/'out'
 base=CLI+['--validate-fft-sequence','--sequence-plan',str(PLAN),'--output-dir',str(out),'--contract',str(CONTRACT)]
 call(base,True,'five independent inputs/outputs; hardware not accessed');assert not out.exists()
 out.mkdir();call(CLI+['--run-fft-sequence','--sequence-plan',str(PLAN),'--output-dir',str(out),'--contract',str(CONTRACT)],False,'contract status must be VERIFIED')
 bad=dict(plan);bad['order']='S2,S5,S10,S7,S2';p=d/'bad.json';p.write_text(json.dumps(bad));call(CLI+['--validate-fft-sequence','--sequence-plan',str(p),'--output-dir',str(out),'--contract',str(CONTRACT)],False,'schema/status/order')
 for fault in [None,'configure-2','dma-2','timeout','status','sync','abort','switch-restore']:
  target=d/f'mock-{fault or "ok"}';target.mkdir();env=dict(os.environ);env.pop('FFTTEST_SEQUENCE_FAULT',None)
  if fault:env['FFTTEST_SEQUENCE_FAULT']=fault
  call(MOCK+[str(x) for x in paths]+[str(target)],fault is None,'SOFTWARE FFT SEQUENCE MODEL ONLY',env)
  made=sorted(target.glob('*.bin'))
  if not fault:
   assert len(made)==5
   for i,f in enumerate(made):
    # Names sort S10/S2/S5/S7, so verify by explicit stage-index outputs below.
    assert f.stat().st_size in (8192,16384)
   for i,(stage,src) in enumerate(zip(['S2','S5','S7','S10','S2'],paths)):
    assert (target/f'{stage}-{i}.bin').read_bytes()==src.read_bytes()
call(CLI+['--validate-fft-sequence','--input','x'],False,'paths come from')
call(CLI+['--run-fft-sequence','--confirm-fresh-reset'],False,'paths come from')
print(f'FFT SEQUENCE SOFTWARE tests PASS ({count} cases; model only, no board/MMIO/DMA/XFFT)')
