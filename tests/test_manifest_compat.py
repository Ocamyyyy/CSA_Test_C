#!/usr/bin/env python3
"""Real imported MATLAB manifests; OFFLINE validation only, never DMA.
All VERIFIED contracts below are temporary synthetic validation fixtures.
The original metadata, input BIN and MATLAB references are strictly read-only.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
PROBE=shlex.split(os.environ.get('FFTTEST_MANIFEST_PROBE',str(ROOT/'build/manifest_probe')))
count=0

def call(args,ok,contains=''):
    global count
    p=subprocess.run(PROBE+args,text=True,capture_output=True,timeout=10)
    assert (p.returncode==0)==ok,(args,p.returncode,p.stdout,p.stderr)
    assert contains in p.stdout,(contains,p.stdout)
    count+=1
    return p

def validate(c,m,inp,n,ok,contains=''):
    return call(['validate',str(c),str(m),str(inp),str(n)],ok,contains)

def test_contract(path):
    c=json.loads((ROOT/'hardware_contract.json').read_text())
    c.update(status='VERIFIED',exclusive=True,dma_address_identity=True,
             target_gpio_mapping='VERIFIED',deployment_identity='VERIFIED',
             evidence='SYNTHETIC OFFLINE metadata validation fixture, not board approval')
    path.write_text(json.dumps(c))
    return c

files=[p for p in (ROOT/'references').rglob('*') if p.is_file()]
before={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
batch_sources=[ROOT/f'references/vectors/batch_{n}' for n in [2,16,64]]
if not all((source/'batch_manifest.json').is_file() and (source/'fft_batch_input.bin').is_file()
           for source in batch_sources):
    print('Manifest compatibility external-snapshot tests SKIPPED: references/vectors/batch_{2,16,64} are not present')
    print('Batch parser tests and 70 synthetic Batch orchestration/error-path tests remain active')
    sys.exit(0)
for n in [2,16,64]:
    source=ROOT/f'references/vectors/batch_{n}'
    original=json.loads((source/'batch_manifest.json').read_text())
    assert original['frame_count']==n and len(original['selected_columns'])==n
    assert isinstance(original['binary_readback_exact'],bool) and original['binary_readback_exact']
    assert original['mode_sel']=='0b01' and original['config_word']=='0x0D550B'
    assert hashlib.sha256((source/'fft_batch_input.bin').read_bytes()).hexdigest()==original['input_sha256']
    call(['legacy',str(source/'batch_manifest.json')],False,'invalid flat JSON')
    call(['parse',str(source/'batch_manifest.json')],True)
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);cp=d/'c.json';mp=d/'m.json';c=test_contract(cp)
        # Original bytes are legal JSON but dataset ID is NOT hardware identity.
        validate(cp,source/'batch_manifest.json',source/'fft_batch_input.bin',n,original['contract_id']==c['contract_id'],
                 'hardware not accessed' if original['contract_id']==c['contract_id'] else 'contract_id must identify hardware')
        fixed=copy.deepcopy(original)
        fixed['dataset_id']=original.get('dataset_id',original['contract_id']);fixed['contract_id']=c['contract_id']
        # Also retain the original mis-bound-ID negative case when an operator
        # has imported the corrected Windows metadata into references.
        misbound=copy.deepcopy(fixed);misbound['contract_id']=fixed['dataset_id'];mp.write_text(json.dumps(misbound))
        validate(cp,mp,source/'fft_batch_input.bin',n,False,'contract_id must identify hardware')
        assert {k:v for k,v in fixed.items() if k not in ('contract_id','dataset_id')}=={
            k:v for k,v in original.items() if k not in ('contract_id','dataset_id')}
        mp.write_text(json.dumps(fixed,indent=2))
        p=validate(cp,mp,source/'fft_batch_input.bin',n,True,'hardware not accessed')
        assert fixed['dataset_id'] in p.stdout
        assert ','.join(map(str,original['selected_columns'])) in p.stdout
        only_id=copy.deepcopy(fixed);del only_id['dataset_id'];mp.write_text(json.dumps(only_id))
        validate(cp,mp,source/'fft_batch_input.bin',n,False,'requires dataset_id')
        wrong=[('frame_count',n+1),('frame_bytes',8191),('total_bytes',n*8192+1),('nfft',1024),
               ('nfft',2048.0),('mode_sel','0b10'),('mode_sel',False),('config_word','0x0D540B'),
               ('byte_order','big-endian'),('fft_direction','inverse'),('fft_output_order','bit-reversed'),
               ('fft_output_scale',1/4096),('word_layout','bits[31:16]=I, bits[15:0]=Q'),
               ('component_type','float32 I/Q'),('frame_layout','column-major'),
               ('sample_order','bit-reversed'),('binary_readback_exact',False),('binary_readback_exact','true'),
               ('selected_columns',original['selected_columns'][:-1]),('selected_columns',[2017]*n),
               ('selected_columns',[1.5]+original['selected_columns'][1:]),
               ('scale_factor',-1),('scale_factor','185.1384772515982'),('scale_factor',0),
               ('direction','inverse'),('fft_quantization','different hardware rounding'),('dataset_id',[])]
        for key,value in wrong:
            bad=copy.deepcopy(fixed);bad[key]=value;mp.write_text(json.dumps(bad))
            validate(cp,mp,source/'fft_batch_input.bin',n,False)
        mp.write_text(json.dumps(fixed))
        for key in ['status','exclusive','dma_address_identity','target_gpio_mapping','deployment_identity']:
            bad=copy.deepcopy(c);bad[key]='UNVERIFIED';cp.write_text(json.dumps(bad))
            validate(cp,mp,source/'fft_batch_input.bin',n,False)
        cp.write_text(json.dumps(c))
        small=d/'input.bin'
        for size in [n*8192-1,n*8192+1]:
            small.write_bytes(bytes(size));validate(cp,mp,small,n,False,'exactly')
        badtext=json.dumps(fixed).replace('"0b01"','0b01')
        mp.write_text(badtext);call(['parse',str(mp)],False,'invalid Batch JSON')
        # Scientific notation is legal JSON and denotes the same output scale.
        text=json.dumps(fixed).replace('0.00048828125','4.8828125e-4')
        mp.write_text(text);validate(cp,mp,source/'fft_batch_input.bin',n,True,'hardware not accessed')
        # Mode is also accepted as an actual JSON unsigned integer.
        fixed['mode_sel']=1;mp.write_text(json.dumps(fixed))
        validate(cp,mp,source/'fft_batch_input.bin',n,True,'hardware not accessed')
        # Never mutate the hardware contract ID to make a dataset ID match.
        assert c['contract_id']=='FFTTest-ref-3656ea4e7423-20261111-2048fwd'
        assert not (d/'unused-output.bin').exists() and not (d/'unused-stats.jsonl').exists()
assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in before.items())
print(f'Manifest compatibility PASS ({count} offline cases; real 2/16/64 manifests; references unchanged; no hardware)')
