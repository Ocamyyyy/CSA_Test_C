#!/usr/bin/env python3
"""All VERIFIED values here are synthetic test fixtures, never hardware evidence."""
import json
import os
import shlex
import pathlib
import subprocess
import tempfile

exe = shlex.split(os.environ.get('FFTTEST_COMMAND', str(pathlib.Path('build/ffttest-host').resolve())))
count = 0

def run(args, ok, contains=''):
    global count
    p = subprocess.run(exe + args, capture_output=True, text=True, timeout=5)
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stdout, p.stderr)
    assert contains in p.stdout, (contains, p.stdout)
    count += 1

with tempfile.TemporaryDirectory() as td:
    d = pathlib.Path(td)
    c = dict(schema='ffttest-contract-v1', status='VERIFIED', contract_id='SYNTHETIC-TEST-ONLY',
             evidence='SYNTHETIC-NOT-HARDWARE', nfft=2048, bytes_per_sample=4,
             byte_order='little', iq_order='I-low', encoding='signed-int16',
             direction='forward', scaling='test-only', quantization='test-only',
             output_order='natural', config_word='test-only', handshake='rising-edge-sticky-done-initial-low',
             tlast='frame-2048', sync_semantics='upstream-4.5.3', exclusive=True,
             dma_address_identity=True, micro_dma=False, target_gpio_mapping='VERIFIED', deployment_identity='VERIFIED', mode_sel=2, btt_bits=14,
             address_bits=32, alignment=4, dma_window_base=0, dma_window_size=1073741824, mode0_line=0, mode1_line=1,
             req_line=2, done_line=3, setup_us=1)
    shared = ['contract_id', 'nfft', 'bytes_per_sample', 'byte_order', 'iq_order',
              'encoding', 'direction', 'scaling', 'quantization', 'output_order', 'config_word']
    m = {k: c[k] for k in shared}
    m['schema'] = 'ffttest-manifest-v1'
    cp, mp, ip = d/'c.json', d/'m.json', d/'i.bin'
    def save():
        cp.write_text(json.dumps(c))
        mp.write_text(json.dumps(m))
    save()
    ip.write_bytes(bytes(8192))
    args = ['--validate', '--mode', '2048', '--contract', str(cp), '--manifest', str(mp), '--input', str(ip)]
    run(['--help'], True)
    run([], False)
    run(['--run', '--inspect'], False)
    run(['--validate', '--timeout-ms', '0'], False)
    run(args, True, 'hardware not accessed')
    for n in [0, 8191, 8193]:
        ip.write_bytes(bytes(n)); run(args, False, 'exactly 8192')
    ip.write_bytes(bytes(8192))
    for k in shared:
        old=m[k];m[k]='mismatch';save();run(args,False);m[k]=old
    for k, v in [('status','UNVERIFIED'),('btt_bits',13),('mode_sel',4),('alignment',3),
                 ('done_line',2),('exclusive',False),('dma_address_identity',False),
                 ('setup_us',0),('handshake','pulse'),('micro_dma',True),('evidence','UNVERIFIED')]:
        old=c[k];c[k]=v;save();run(args,False);c[k]=old
    save()
    run(args + ['--mode','1024'],False)
    run(args + ['--log',str(d/'test.log')],True)
    run(args + ['--log',str(d/'test.log')],False,'new log')
    cp.write_text('{"status":"VERIFIED","status":"VERIFIED"}')
    run(args,False,'invalid flat JSON')
    run(['--run','--mode','2048','--contract','examples/hardware_contract.json',
         '--manifest','examples/fft_test_manifest.json','--input',str(ip)],False,'status')
print(f'CLI tests PASSED ({count} cases; no hardware accessed)')

# Additional stage-2 gates; original 35 cases above remain exercised.
with tempfile.TemporaryDirectory() as td:
    d=pathlib.Path(td)
    cp,mp,ip=d/'c.json',d/'m.json',d/'i.bin'
    cp.write_text(json.dumps(c));mp.write_text(json.dumps(m));ip.write_bytes(bytes(8192))
    base=['--contract',str(cp),'--manifest',str(mp),'--input',str(ip),'--mode','2048']
    run(['--run']+base,False,'--confirm-fresh-reset')
    old=c['dma_window_size'];c['dma_window_size']=0;cp.write_text(json.dumps(c))
    run(['--validate']+base,False,'DMA address window')
    c['dma_window_size']=old
    c['handshake']='level-high-return-zero';cp.write_text(json.dumps(c))
    run(['--validate']+base,False,'handshake')
print(f'CLI total PASSED ({count} cases including stage-2 gates)')

with tempfile.TemporaryDirectory() as td:
    d=pathlib.Path(td);cp,mp,ip=d/'c.json',d/'m.json',d/'i.bin'
    c['handshake']='rising-edge-sticky-done-initial-low'
    mp.write_text(json.dumps(m));ip.write_bytes(bytes(8192))
    base=['--validate','--contract',str(cp),'--manifest',str(mp),'--input',str(ip),'--mode','2048']
    for key in ['target_gpio_mapping','deployment_identity']:
        c[key]='UNVERIFIED';cp.write_text(json.dumps(c));run(base,False,key);c[key]='VERIFIED'
    run(['--run','--confirm-fresh-reset','--contract','hardware_contract.json',
         '--manifest','fft_test_manifest.json','--input',str(ip),'--mode','2048'],False,'status')
print(f'CLI final PASSED ({count} cases; board gates remain active)')
