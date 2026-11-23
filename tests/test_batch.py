#!/usr/bin/env python3
"""SOFTWARE ONLY: real Batch orchestration with wrapped DMA echo providers."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOCK = shlex.split(os.environ.get('FFTTEST_BATCH_MOCK', str(ROOT/'build/test_batch')))
CLI = shlex.split(os.environ.get('FFTTEST_COMMAND', str(ROOT/'build/ffttest-host')))
count = 0

def invoke(cmd, ok, env=None):
    global count
    p = subprocess.run(cmd, text=True, capture_output=True, env=env, timeout=15)
    assert (p.returncode == 0) == ok, (cmd, p.returncode, p.stdout, p.stderr)
    count += 1
    return p

def fixture(d, frames=2):
    c = json.loads((ROOT/'hardware_contract.json').read_text())
    c.update(status='VERIFIED', exclusive=True, dma_address_identity=True,
             target_gpio_mapping='VERIFIED', deployment_identity='VERIFIED',
             contract_id='SYNTHETIC-BATCH-SOFTWARE-ONLY', evidence='SYNTHETIC-NOT-BOARD-EVIDENCE')
    keys=['contract_id','nfft','bytes_per_sample','byte_order','iq_order','encoding','direction',
          'scaling','quantization','output_order','config_word']
    m={k:c[k] for k in keys}
    m.update(schema='ffttest-batch-v1',frame_count=frames,frame_bytes=8192)
    (d/'c.json').write_text(json.dumps(c));(d/'m.json').write_text(json.dumps(m))
    data=b''.join(bytes((i*53+j)%256 for j in range(8192)) for i in range(frames))
    (d/'input.bin').write_bytes(data)
    return c,m,data

def mock_args(d,n):
    return MOCK+[str(d/'input.bin'),str(d/'output.bin'),str(d/'c.json'),str(d/'m.json'),str(d/'stats.jsonl'),str(n),'5']

def cli_args(d,n):
    return CLI+['--run-batch','--confirm-fresh-reset','--mode','2048','--frames',str(n),
                '--input',str(d/'input.bin'),'--output',str(d/'output.bin'),
                '--contract',str(d/'c.json'),'--manifest',str(d/'m.json'),'--stats',str(d/'stats.jsonl')]

for n in [2,16,64]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);c,m,data=fixture(d,n)
        p=invoke(mock_args(d,n),True)
        assert (d/'input.bin').read_bytes()==data and (d/'output.bin').read_bytes()==data
        assert f'configured=1 resets=1 submitted={n} completed={n}' in p.stdout
        rows=[json.loads(line) for line in (d/'stats.jsonl').read_text().splitlines()]
        assert len(rows)==n+1
        for i,r in enumerate(rows[:-1]):
            assert r['type']=='frame' and r['frame_index']==i and r['status']=='PASS'
            assert r['input_offset']==r['output_offset']==i*8192
            assert r['rx_bytes']==8192 and r['rx_bytes_valid']
            assert r['mm2s_sr']==r['s2mm_sr']==0x1002
            assert r['dma_us']>=0 and r['frame_us']>=r['dma_us']
        s=rows[-1]
        assert s['successful_frames']==s['requested_frames']==n and s['failed_frame_index'] is None
        assert s['status']=='PASS' and s['actual_output_bytes']==n*8192
    for delta in [-1,1]:
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);_,_,data=fixture(d,n)
            (d/'input.bin').write_bytes(data[:-1] if delta<0 else data+b'x')
            p=invoke(mock_args(d,n),False)
            assert 'exactly' in p.stdout and not (d/'output.bin').exists()
            p=invoke(cli_args(d,n),False)  # Must reject before native guard/device access.
            assert 'exactly' in p.stdout and not (d/'stats.jsonl').exists()

for fault in ['error','timeout','short','ioc-stuck','sync','partial','fsync','stats','input-short','cancel','pre-error','not-idle']:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);_,_,data=fixture(d,16)
        p=invoke(mock_args(d,16),False,dict(os.environ,FFTTEST_MOCK_FAULT=fault))
        assert (d/'output.bin').read_bytes()==data[:8192], (fault,p.stdout)
        assert (d/'input.bin').read_bytes()==data
        rows=[json.loads(line) for line in (d/'stats.jsonl').read_text().splitlines()]
        s=rows[-1]
        assert s['type']=='summary' and s['status']=='FAIL', (fault,rows)
        assert s['successful_frames']==1 and s['failed_frame_index']==1
        assert s['actual_output_bytes']==8192
        if fault!='stats':assert rows[-2]['status']=='FAIL' and rows[-2]['frame_index']==1
        if fault in ['ioc-stuck','input-short','pre-error','not-idle']:
            assert 'submitted=1' in p.stdout
            if fault=='ioc-stuck':assert 'IOC clear/readback failed' in p.stdout
            if fault=='pre-error':assert rows[-2]['s2mm_sr']&0x4020==0x4020
        else:assert 'submitted=2' in p.stdout
        assert 'configured=1 resets=2' in p.stdout

with tempfile.TemporaryDirectory() as td:
    d=Path(td);_,_,data=fixture(d)
    invoke(mock_args(d,2),True,dict(os.environ,FFTTEST_MOCK_FAULT='short-writes'))
    assert (d/'output.bin').read_bytes()==data

for key,value in [('schema','ffttest-manifest-v1'),('frame_count',16),('frame_bytes',8191),
                  ('contract_id','different'),('config_word','0x0D540B'),('direction','inverse')]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);_,m,_=fixture(d);m[key]=value;(d/'m.json').write_text(json.dumps(m))
        invoke(mock_args(d,2),False);invoke(cli_args(d,2),False)
        assert not (d/'output.bin').exists() and not (d/'stats.jsonl').exists()

for n in [0,1,3,4096]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);fixture(d)
        invoke(cli_args(d,n),False)

for key in ['status','exclusive','dma_address_identity','target_gpio_mapping','deployment_identity']:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);c,_,_=fixture(d);c[key]='UNVERIFIED';(d/'c.json').write_text(json.dumps(c))
        invoke(mock_args(d,2),False);invoke(cli_args(d,2),False)
        assert not (d/'output.bin').exists()

for fault in ['error','timeout']:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);fixture(d)
        p=invoke(mock_args(d,2),False,dict(os.environ,FFTTEST_MOCK_FAULT=fault,FFTTEST_MOCK_FAIL_FRAME='0'))
        rows=[json.loads(line) for line in (d/'stats.jsonl').read_text().splitlines()]
        assert rows[-1]['failed_frame_index']==0 and rows[-1]['successful_frames']==0
        assert (d/'output.bin').stat().st_size==0 and 'submitted=1' in p.stdout
for key,value in [('direction','inverse'),('byte_order','big'),('iq_order','Q-low'),
                  ('output_order','bit-reversed'),('config_word','0x0D540B')]:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);c,m,_=fixture(d);c[key]=m[key]=value
        (d/'c.json').write_text(json.dumps(c));(d/'m.json').write_text(json.dumps(m))
        p=invoke(mock_args(d,2),False);assert 'Batch requires 2048 Forward' in p.stdout
for target in ['output.bin','stats.jsonl']:
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);fixture(d);(d/target).write_bytes(b'EXISTING SINGLE-FRAME RESULT')
        invoke(mock_args(d,2),False)
        assert (d/target).read_bytes()==b'EXISTING SINGLE-FRAME RESULT'
with tempfile.TemporaryDirectory() as td:
    d=Path(td);_,_,data=fixture(d)
    args=mock_args(d,2);args[len(MOCK)+1]=str(d/'input.bin')
    invoke(args,False);assert (d/'input.bin').read_bytes()==data
with tempfile.TemporaryDirectory() as td:
    d=Path(td);fixture(d)
    args=cli_args(d,2);args.remove('--confirm-fresh-reset')
    p=invoke(args,False);assert '--confirm-fresh-reset' in p.stdout
    invoke(CLI+['--run-batch','--run'],False)
    invoke(CLI+['--run','--frames','2'],False)
    invoke(CLI+['--validate','--stats',str(d/'unused')],False)

with tempfile.TemporaryDirectory() as td:
    d=Path(td);fixture(d)
    args=cli_args(d,2);pos=args.index('--stats');del args[pos:pos+2]
    p=invoke(args,False);assert '--stats' in p.stdout
    args=mock_args(d,2);args[len(MOCK)+4]=str(d/'output.bin')
    p=invoke(args,False);assert 'distinct paths' in p.stdout and not (d/'output.bin').exists()

print(f'Batch SOFTWARE tests PASSED ({count} cases; DMA echo mocks, no board FFT claim)')
