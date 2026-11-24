#!/usr/bin/env python3
"""M2 software tests only: integer model + real control flow, no board access."""
import copy
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MOCK=shlex.split(os.environ.get('FFTTEST_M2_MOCK',str(ROOT/'build/test_m2')))
CLI=shlex.split(os.environ.get('FFTTEST_COMMAND',str(ROOT/'build/ffttest-host')))
count=0

def call(args,ok,env=None,contains=''):
    global count
    p=subprocess.run(args,capture_output=True,text=True,env=env,timeout=15)
    assert (p.returncode==0)==ok,(args,p.returncode,p.stdout,p.stderr)
    assert contains in p.stdout,(contains,p.stdout)
    count+=1
    return p

def q15(x):
    q,r=divmod(x,32768)
    if r>16384 or (r==16384 and q%2):q+=1
    return min(32767,max(-32768,q))

def fixture(d,n=7):
    c=json.loads((ROOT/'m2_hardware_contract.json').read_text())
    c.update(status='VERIFIED',exclusive=True,dma_address_identity=True,
             deployment_identity='VERIFIED',target_gpio_mapping='VERIFIED',
             evidence='SYNTHETIC SOFTWARE M2 test fixture, not board evidence')
    m=json.loads((ROOT/'m2_manifest.json').read_text())
    m.update(samples=n,input_bytes=n*4,coeff_bytes=n*4,output_bytes=n*4)
    # Ties-to-even, negative ties, complex products, 33-bit accumulation and saturation.
    pattern=[(1,0,16384,0),(3,0,16384,0),(-1,0,16384,0),(-3,0,16384,0),
             (-32768,-32768,-32768,-32768),(32767,-32768,32767,32767),(1234,-5678,20000,-22000)]
    data=bytearray();coeff=bytearray();expected=bytearray()
    for i in range(n):
        di,dq,ci,cq=pattern[i%len(pattern)]
        data+=struct.pack('<hh',di,dq);coeff+=struct.pack('<hh',ci,cq)
        expected+=struct.pack('<hh',q15(di*ci-dq*cq),q15(di*cq+dq*ci))
    (d/'input.bin').write_bytes(data);(d/'coeff.bin').write_bytes(coeff)
    (d/'c.json').write_text(json.dumps(c));(d/'m.json').write_text(json.dumps(m))
    return c,m,bytes(data),bytes(coeff),bytes(expected)

def mock(d,n):return MOCK+[str(d/'input.bin'),str(d/'coeff.bin'),str(d/'output.bin'),str(d/'c.json'),str(d/'m.json'),str(n)]
def cli(d,n,action='--validate-m2'):
    return CLI+[action,'--samples',str(n),'--input',str(d/'input.bin'),'--coeff',str(d/'coeff.bin'),
                '--output',str(d/'output.bin'),'--contract',str(d/'c.json'),'--manifest',str(d/'m.json')]

for n in [1,7,2048,4096]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);c,m,data,coeff,expected=fixture(d,n)
        call(cli(d,n),True,contains='offline validation PASS')
        assert not (d/'output.bin').exists()
        p=call(mock(d,n),True,contains='SOFTWARE M2/Switch/DMA MODEL ONLY')
        assert (d/'output.bin').read_bytes()==expected
        assert (d/'input.bin').read_bytes()==data and (d/'coeff.bin').read_bytes()==coeff
        assert 'starts=2 clear=1 DMA_resets=1 MM2S=2 S2MM=1 Switch_commits=4' in p.stdout
        assert 'Original Switch routes restored/read back' in p.stdout
        if n>=7:assert 'sat_i_nonzero=1 sat_q_nonzero=1' in p.stdout
    for target in ['input.bin','coeff.bin']:
        for delta in [-1,1]:
            with tempfile.TemporaryDirectory() as td:
                d=Path(td);fixture(d,n);p=d/target;data=p.read_bytes();p.write_bytes(data[:-1] if delta<0 else data+b'x')
                call(cli(d,n),False,contains='exactly')
                assert not (d/'output.bin').exists()

with tempfile.TemporaryDirectory() as td:
    d=Path(td);_,_,_,_,expected=fixture(d)
    call(mock(d,7),True,dict(os.environ,FFTTEST_M2_INITIAL_ERROR='1'))
    assert (d/'output.bin').read_bytes()==expected

for fault in ['initial-busy','stale-done','busy-start','bad-length','internal','load-tlast','load-tkeep',
              'process-tlast','process-tkeep','dma-error','dma-timeout','done-timeout','short-rx',
              'abort-timeout','cancel','switch-readback','switch-timeout','output-error',
              'clear-timeout','stale-process','coeff-invalid','status-unstable','gpio-request']:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);_,_,data,coeff,_=fixture(d)
        p=call(mock(d,7),False,dict(os.environ,FFTTEST_M2_FAULT=fault))
        assert (0<len((d/'output.bin').read_bytes())<28) if fault=='output-error' else not (d/'output.bin').read_bytes(),(fault,p.stdout)
        assert (d/'input.bin').read_bytes()==data and (d/'coeff.bin').read_bytes()==coeff
        if fault=='abort-timeout':assert 'QUARANTINE' in p.stdout and 'routes NOT restored' in p.stdout
        if fault=='stale-done':assert 'did not clear old sticky completion' in p.stdout
        if fault.endswith('tlast'):assert 'code=3(TLAST)' in p.stdout
        if fault.endswith('tkeep'):assert 'code=4(TKEEP)' in p.stdout

for field in ['status','exclusive','dma_address_identity','deployment_identity','target_gpio_mapping']:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);c,_,_,_,_=fixture(d);c[field]='UNVERIFIED';(d/'c.json').write_text(json.dumps(c))
        call(cli(d,7,'--run-m2'),False,contains='M2 contract')
        assert not (d/'output.bin').exists()
for key,value in [('samples',8),('input_bytes',27),('coeff_bytes',32),('output_bytes',0),('tkeep',7),
                  ('contract_id','wrong'),('byte_order','big'),('coeff_format','Q2.14'),
                  ('iq_order','Q-low'),('arithmetic','truncate'),('tlast','every-sample')]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);_,m,_,_,_=fixture(d);m[key]=value;(d/'m.json').write_text(json.dumps(m))
        call(cli(d,7),False);assert not (d/'output.bin').exists()
for key,value in [('m2_gpio_base',0x41200000),('input_switch_base',0x43c10000),('gpio_width',16),
                  ('gpio2_width',16),('btt_bits',14),('alignment',8),('switch_register_protocol','unknown')]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);c,_,_,_,_=fixture(d,4096);c[key]=value;(d/'c.json').write_text(json.dumps(c))
        call(cli(d,4096),False)
for n in [0,4097,8192]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);fixture(d);call(cli(d,n),False,contains='1..4096')
for path in ['output.bin','input.bin','coeff.bin']:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);fixture(d)
        if path=='output.bin':(d/path).write_bytes(b'PREVIOUS FFT GOLDEN RESULT')
        before=(d/path).read_bytes();args=mock(d,7);args[len(MOCK)+2]=str(d/path)
        call(args,False);assert (d/path).read_bytes()==before
call(CLI+['--run','--samples','7'],False)
call(CLI+['--run-batch','--coeff','x'],False)
call(CLI+['--inspect-m2','--run'],False)
call(CLI+['--run-m2','--mode','2048'],False)
call(MOCK+['--inspect'],True,contains='no Switch/DMA/control writes PASS')
print(f'M2 SOFTWARE tests PASS ({count} cases; no physical board or DMA access)')
