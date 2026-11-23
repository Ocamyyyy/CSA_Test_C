#!/usr/bin/env python3
"""Read-only consistency audit of the delivered Windows reference package."""
import hashlib
import json
from pathlib import Path
import struct
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1] / 'references'
checks = 0
covered=set()
for line in (root/'SHA256SUMS.txt').read_text().splitlines():
    digest, name = line.split(maxsplit=1)
    p = root / name.lstrip('*')
    assert p.is_file(), f'missing payload: {p}'
    assert hashlib.sha256(p.read_bytes()).hexdigest() == digest, p
    covered.add(name.lstrip('*'))
    checks += 1

actual={str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt'}
assert covered <= actual, ('missing manifested payload', covered-actual)
# Operators may add board captures beside the immutable original payload.
# Keep verifying every manifested byte; do not present added files as audited.
if actual-covered:
    print('Additional unmanifested reference artifacts (not covered by original SHA256SUMS):', ', '.join(sorted(actual-covered)))
rows=re.findall(r"^\| `([^`]+)` \| `[^`]+` \| [^|]+ \| (\d+) \| `([a-f0-9]{64})` \|$",(root/'MANIFEST.md').read_text(),re.M)
assert len(rows)==11
for name,size,digest in rows:
    data=(root/name).read_bytes()
    assert len(data)==int(size) and hashlib.sha256(data).hexdigest()==digest, name

bd = json.loads((root/'hardware/design_1.bd').read_text())['design']
assert bd['design_info']['tool_version'] == '2018.3'
assert bd['design_info']['device'] == 'xc7z100ffg900-2'

def xci(name, component):
    tree = ET.parse(root/f'hardware/{name}.xci').getroot()
    values = {e.attrib['{http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009}referenceId']: e.text
              for e in tree.iter() if e.tag.endswith('configurableElementValue')}
    assert values['PARAM_VALUE.Component_Name'] == bd['components'][component]['xci_name']
    assert values['RUNTIME_PARAM.SWVERSION'] == '2018.3'
    assert values['PROJECT_PARAM.DEVICE'] == 'xc7z100'
    assert values['PROJECT_PARAM.PACKAGE'] == 'ffg900'
    assert values['PROJECT_PARAM.SPEEDGRADE'] == '-2'
    for k, v in bd['components'][component].get('parameters', {}).items():
        assert values['PARAM_VALUE.'+k] == v['value'], (component,k)
    return values

def expected(values, prefix, fields):
    for k, v in fields.items():
        assert values[prefix+k] == str(v), (prefix+k,values.get(prefix+k),v)

dma = xci('axi_dma','axi_dma_0')
expected(dma,'PARAM_VALUE.',dict(c_include_mm2s=1,c_include_s2mm=1,c_include_sg=0,
    c_include_mm2s_dre=0,c_include_s2mm_dre=0,c_micro_dma=0,c_sg_length_width=15,
    c_addr_width=32,c_m_axi_mm2s_data_width=32,c_m_axi_s2mm_data_width=32,
    c_m_axis_mm2s_tdata_width=32,c_s_axis_s2mm_tdata_width=32))
assert dma['MODELPARAM_VALUE.C_SG_LENGTH_WIDTH'] == '15'
gpio = xci('axi_gpio','axi_gpio_0')
expected(gpio,'PARAM_VALUE.',dict(C_GPIO_WIDTH=3,C_GPIO2_WIDTH=1,C_IS_DUAL=1,
    C_ALL_OUTPUTS=1,C_ALL_INPUTS_2=1,C_DOUT_DEFAULT='0x00000000'))
fft = xci('xfft','xfft_0')
expected(fft,'PARAM_VALUE.',dict(transform_length=4096,run_time_configurable_transform_length='true',
    implementation_options='pipelined_streaming_io',data_format='fixed_point',input_width=16,
    phase_factor_width=16,scaling_options='scaled',rounding_modes='convergent_rounding',
    output_ordering='natural_order',throttle_scheme='nonrealtime',ovflo='false'))
expected(fft,'MODELPARAM_VALUE.',dict(C_OUTPUT_WIDTH=16,C_S_AXIS_CONFIG_TDATA_WIDTH=24,
    C_M_AXIS_DATA_TDATA_WIDTH=32,C_S_AXIS_DATA_TDATA_WIDTH=32,C_HAS_BFP=0,C_HAS_OVFLO=0))
for values, interface in [(dma,'M_AXIS_MM2S'),(dma,'S_AXIS_S2MM'),(fft,'S_AXIS_DATA'),(fft,'M_AXIS_DATA')]:
    expected(values,f'BUSIFPARAM_VALUE.{interface}.',dict(HAS_TLAST=1,HAS_TREADY=1,TDATA_NUM_BYTES=4))
rst = xci('proc_sys_reset','proc_sys_reset_0')
assert rst['PARAM_VALUE.C_EXT_RESET_HIGH'] == '0'

def net(collection, name, key, *ports):
    assert set(bd[collection][name][key]) == set(ports), name
net('interface_nets','axi_dma_0_M_AXIS_MM2S','interface_ports','axi_dma_0/M_AXIS_MM2S','xfft_0/S_AXIS_DATA')
net('interface_nets','xfft_0_M_AXIS_DATA','interface_ports','xfft_0/M_AXIS_DATA','axi_dma_0/S_AXIS_S2MM')
for signal in ['config_tdata','config_tvalid']:
    net('nets','fft_config_ctrl_0_'+signal,'ports','fft_config_ctrl_0/'+signal,'xfft_0/s_axis_'+signal)
net('nets','xfft_0_s_axis_config_tready','ports','xfft_0/s_axis_config_tready','fft_config_ctrl_0/config_tready')
net('nets','fft_config_ctrl_0_config_done','ports','fft_config_ctrl_0/config_done','axi_gpio_0/gpio2_io_i')
net('nets','slice_mode_Dout','ports','slice_mode/Dout','fft_config_ctrl_0/mode_sel')
net('nets','slice_req_Dout','ports','slice_req/Dout','fft_config_ctrl_0/config_req')
for comp, fields in [('slice_mode',dict(DIN_WIDTH=3,DIN_FROM=1,DIN_TO=0,DOUT_WIDTH=2)),
                     ('slice_req',dict(DIN_WIDTH=3,DIN_FROM=2,DIN_TO=2,DOUT_WIDTH=1))]:
    for k,v in fields.items():assert bd['components'][comp]['parameters'][k]['value']==str(v)
reset_ports=bd['nets']['proc_sys_reset_0_peripheral_aresetn']['ports']
for p in ['axi_dma_0/axi_resetn','xfft_0/aresetn','axi_gpio_0/s_axi_aresetn','fft_config_ctrl_0/aresetn']:
    assert p in reset_ports
segments=bd['addressing']['/processing_system7_0']['address_spaces']['Data']['segments']
assert segments['SEG_axi_dma_0_Reg']['offset']=='0x40400000'
assert segments['SEG_axi_gpio_0_Reg']['offset']=='0x41200000'
for channel in ['Data_MM2S','Data_S2MM']:
    segment=next(iter(bd['addressing']['/axi_dma_0']['address_spaces'][channel]['segments'].values()))
    assert segment['offset']=='0x00000000' and segment['range']=='1G'
# Recompute only the configuration bit fields, not FFT data or MATLAB vectors.
assert (0x6AA<<9)|(1<<8)|11 == 0x0D550B
assert sum((0x6AA>>(2*i))&3 for i in range(6)) == 11
for case in ['impulse','tone','random']:
    data=(root/f'vectors/{case}/fft_input.bin').read_bytes()
    assert len(data)==8192
    components=struct.unpack('<4096h',data)
    if case=='impulse': assert components[0]==16384 and not any(components[1:])
    if case=='random': assert min(components)>=-8192 and max(components)<=8191
    print(f'{case}: 8192 bytes, signed-int16 component range {min(components)}..{max(components)}')
print(f'Reference audit PASSED: {checks} SHA-256 entries; XCI/BD parameters, versions, direct AXIS/TLAST and reset topology agree')
print('Missing supplementary evidence: cited tb_fft_2048_radarsat1.v, simulation/ILA traces, MATLAB JSON/.mat, target live DT/vendor GPIO source and deployed-bitstream provenance')
print('Static audit only: no assertion of board DMA success')
