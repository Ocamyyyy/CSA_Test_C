#!/usr/bin/env python3
"""S5/S7/S10 metadata and orchestration tests; no physical hardware."""
import copy,hashlib,json,os,shlex,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REF=ROOT/'references/fft_modes'
CLI=shlex.split(os.environ.get('FFTTEST_COMMAND',str(ROOT/'build/ffttest-host')))
MOCK=shlex.split(os.environ.get('FFTTEST_MODES_MOCK',str(ROOT/'build/test_fft_modes')))
SPECS={'S5':(4096,16384,'11','0x15550C','FFT'),'S7':(4096,16384,'10','0x15540C','IFFT'),'S10':(2048,8192,'00','0x0D540B','IFFT')}
count=0
def call(cmd,ok,contains='',env=None):
 global count;p=subprocess.run(cmd,text=True,capture_output=True,timeout=15,env=env)
 assert (p.returncode==0)==ok,(cmd,p.returncode,p.stdout,p.stderr);assert contains in p.stdout,(contains,p.stdout);count+=1;return p
def cli(stage,manifest,input,output,contract=ROOT/'fft_modes_hardware_contract.json',action='--validate-fft-mode'):
 return CLI+[action,'--fft-stage',stage,'--input',str(input),'--output',str(output),'--manifest',str(manifest),'--contract',str(contract)]

assert {p.name for p in REF.iterdir() if p.is_dir()}==set(SPECS)
for stage,(samples,frame,mode,word,direction) in SPECS.items():
 case=REF/stage;m=json.loads((case/'manifest.json').read_text());assert m['sample_count']==samples
 assert m['config']['frame_bytes']==frame and m['config']['mode_sel_binary']==mode and m['config']['config_tdata_hex']==word and m['config']['direction']==direction
 files={x['name']:x for x in m['files']};assert set(files)=={'input.bin','reference.mat'}
 for name,item in files.items():data=(case/name).read_bytes();assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256']
 with tempfile.TemporaryDirectory() as td:
  d=Path(td);out=d/'actual.bin';call(cli(stage,case/'manifest.json',case/'input.bin',out),True,'hardware not accessed');assert not out.exists()
  call(cli(stage,case/'manifest.json',case/'input.bin',out,action='--run-fft-mode')+['--confirm-fresh-reset'],False,'contract status must be VERIFIED');assert not out.exists()
  bad=copy.deepcopy(m);bad['config']['mode_sel_binary']='01';p=d/'bad-mode.json';p.write_text(json.dumps(bad));call(cli(stage,p,case/'input.bin',out),False)
  bad=copy.deepcopy(m);bad['config']['frame_bytes']=frame-4;p=d/'bad-frame.json';p.write_text(json.dumps(bad));call(cli(stage,p,case/'input.bin',out),False)
  bad=copy.deepcopy(m);bad['source']['no_additional_fftshift']=False;p=d/'bad-shift.json';p.write_text(json.dumps(bad));call(cli(stage,p,case/'input.bin',out),False)
  short=d/'short.bin';short.write_bytes((case/'input.bin').read_bytes()[:-1]);call(cli(stage,case/'manifest.json',short,out),False,'exactly')
  corrupt=d/'corrupt.bin';data=bytearray((case/'input.bin').read_bytes());data[17]^=1;corrupt.write_bytes(data);call(cli(stage,case/'manifest.json',corrupt,out),False,'SHA-256 differs')
  c=json.loads((ROOT/'fft_modes_hardware_contract.json').read_text());c.update(status='VERIFIED',exclusive=True,dma_address_identity=True,target_gpio_mapping='VERIFIED',deployment_identity='VERIFIED',evidence='SYNTHETIC SOFTWARE MODE MOCK, not board evidence')
  cp=d/'contract.json';cp.write_text(json.dumps(c))
  for fault in [None,'dma-error','timeout','status','gpio','switch-restore','output']:
   target=d/f"{stage}-{fault or 'ok'}.bin";env=dict(os.environ);env.pop('FFTTEST_MODE_FAULT',None)
   if fault:env['FFTTEST_MODE_FAULT']=fault
   call(MOCK+[stage,str(case/'input.bin'),str(target),str(case/'manifest.json'),str(cp)],fault is None,'SOFTWARE FFT MODE MODEL ONLY',env)
   if not fault:assert target.read_bytes()==(case/'input.bin').read_bytes() and len(target.read_bytes())==frame
call(CLI+['--validate-fft-mode'],False,'--fft-stage')
call(CLI+['--validate-fft-mode','--fft-stage','S6'],False,'must be S2, S5, S7 or S10')
call(CLI+['--validate-fft-mode','--fft-stage','S5','--mode','4096'],False,'use --fft-stage')
call(CLI+['--run','--fft-stage','S5'],False,'requires --validate-fft-mode')
print(f'FFT MODES SOFTWARE tests PASS ({count} cases; model only, no board/MMIO/DMA/XFFT)')
