#!/usr/bin/env python3
"""FFT-to-M2 chain metadata and orchestration tests; no physical hardware."""
import copy, hashlib, json, os, shlex, subprocess, tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
REF=ROOT/'references/fft_m2_com'
CLI=shlex.split(os.environ.get('FFTTEST_COMMAND',str(ROOT/'build/ffttest-host')))
MOCK=shlex.split(os.environ.get('FFTTEST_CHAIN_MOCK',str(ROOT/'build/test_fft_m2')))
count=0
def call(cmd,ok,contains='',env=None):
 global count
 p=subprocess.run(cmd,text=True,capture_output=True,timeout=15,env=env)
 assert (p.returncode==0)==ok,(cmd,p.returncode,p.stdout,p.stderr)
 assert contains in p.stdout,(contains,p.stdout);count+=1;return p

m=json.loads((REF/'fft_m2_manifest.json').read_text())
assert m['schema']=='radarsat1-fft-m2-chain-v1'
listed={x['name']:x for x in m['files']}
for name,item in listed.items():
 data=(REF/name).read_bytes();assert len(data)==item['bytes'];assert hashlib.sha256(data).hexdigest()==item['sha256']

def cli(manifest,fftout,final,input=REF/'fft_input.bin',coeff=REF/'m2_coeff.bin',action='--validate-fft-m2'):
 return CLI+[action,'--input',str(input),'--coeff',str(coeff),'--fft-output',str(fftout),'--output',str(final),
  '--manifest',str(manifest),'--contract',str(ROOT/'hardware_contract.json'),'--m2-contract',str(ROOT/'m2_hardware_contract.json')]

with tempfile.TemporaryDirectory() as td:
 d=Path(td);fftout=d/'fft_actual.bin';final=d/'fft_m2_actual.bin'
 call(cli(REF/'fft_m2_manifest.json',fftout,final),True,'hardware not accessed')
 assert not fftout.exists() and not final.exists()
 # Candidate deployment gates must reject run before any output/device access.
 call(cli(REF/'fft_m2_manifest.json',fftout,final,action='--run-fft-m2')+['--confirm-fresh-reset'],False,'contract status must be VERIFIED')
 assert not fftout.exists() and not final.exists()
 mutations=[('sample_count',1024),('status','BOARD_PASS')]
 for key,value in mutations:
  bad=copy.deepcopy(m);bad[key]=value;p=d/f'bad-{key}.json';p.write_text(json.dumps(bad));call(cli(p,fftout,final),False)
 nested=[('fft','output_order','bit-reversed'),('fft','frame_bytes',8188),('m2','vector_len',1024),
         ('frequency_coordinates','fftshift_between_fft_and_m2',True),('validation','board_status','PASS')]
 for obj,key,value in nested:
  bad=copy.deepcopy(m);bad[obj][key]=value;p=d/f'bad-{obj}-{key}.json';p.write_text(json.dumps(bad));call(cli(p,fftout,final),False)
 for filename in ['fft_input.bin','m2_coeff.bin']:
  short=d/filename;short.write_bytes((REF/filename).read_bytes()[:-1])
  call(cli(REF/'fft_m2_manifest.json',fftout,final,input=short if filename=='fft_input.bin' else REF/'fft_input.bin',
           coeff=short if filename=='m2_coeff.bin' else REF/'m2_coeff.bin'),False,'exactly 8192')
  corrupt=d/f'corrupt-{filename}';payload=bytearray((REF/filename).read_bytes());payload[123]^=0x80;corrupt.write_bytes(payload)
  call(cli(REF/'fft_m2_manifest.json',fftout,final,input=corrupt if filename=='fft_input.bin' else REF/'fft_input.bin',
           coeff=corrupt if filename=='m2_coeff.bin' else REF/'m2_coeff.bin'),False,'SHA-256 differs')
 call(cli(REF/'fft_m2_manifest.json',REF/'fft_input.bin',final),False,'must be distinct')

for fault in [None,'fresh-reset','fft-dma','fft-timeout','fft-status','fft-gpio','fft-switch-restore',
              'm2-dma','m2-timeout','m2-status','m2-gpio','m2-switch-restore']:
 with tempfile.TemporaryDirectory() as td:
  d=Path(td);cmd=MOCK+[str(REF/'fft_input.bin'),str(REF/'m2_coeff.bin'),str(d/'fft_actual.bin'),str(d/'fft_m2_actual.bin')]
  env=dict(os.environ);env.pop('FFTTEST_CHAIN_FAULT',None)
  if fault:env['FFTTEST_CHAIN_FAULT']=fault
  p=call(cmd,fault is None,'SOFTWARE FFT-to-M2 CHAIN MODEL ONLY',env)
  if fault and fault.startswith('fft-'):assert not (d/'fft_m2_actual.bin').exists()
  if not fault:assert (d/'fft_actual.bin').read_bytes()==(d/'fft_m2_actual.bin').read_bytes()

call(CLI+['--run','--fft-output','x'],False)
call(CLI+['--validate-fft-m2','--samples','2048'],False,'fixed at 2048')
call(CLI+['--validate-fft-m2','--confirm-fresh-reset'],False,'only for --run-fft-m2')
print(f'FFT-M2 CHAIN SOFTWARE tests PASS ({count} cases; model only, no board/MMIO/DMA)')
