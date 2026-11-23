# FFTTest 2048-point DMA interface

This note describes the existing Vivado project. No Vivado source or IP
configuration was changed while preparing the MATLAB vectors.

## XFFT configuration

| Item | Existing value |
|---|---|
| IP | Xilinx FFT v9.1 |
| Architecture | Pipelined Streaming I/O |
| Runtime length | enabled; 2048 uses `NFFT=11` |
| Data format | fixed point |
| Input | signed 16-bit I + signed 16-bit Q |
| Output | signed 16-bit I + signed 16-bit Q |
| Phase factor | 16 bit |
| Rounding | convergent rounding |
| Scaling | scaled, schedule `12'h6AA` for 2048 |
| Ideal total scale | `2^-11 = 1/2048` |
| Output order | natural order |

Primary sources:

- `FFTTest.srcs/sources_1/bd/design_1/ip/design_1_xfft_0_0/design_1_xfft_0_0.xci`:
  lines 109, 114, 117 and 131-144.
- `FFTTest.srcs/sources_1/new/fft_config_ctrl.v`: lines 9-10 and 45-68.
- `FFTTest.srcs/sim_1/new/tb_fft_2048_radarsat1.v`: lines 188-199.

## 2048-point mode

`mode_sel[1]` selects length and `mode_sel[0]` selects direction:

| Operation | `mode_sel` | 24-bit `config_tdata` |
|---|---:|---:|
| 2048 forward FFT | `2'b01` | `24'h0D550B` |
| 2048 inverse FFT | `2'b00` | `24'h0D540B` |

For this validation use **2048 forward FFT**, `mode_sel=1`. Pulse
`config_req`, wait for `config_done`, then start the DMA frame.

## DMA word and file format

Each complex sample is one 32-bit word:

```text
bits [15:0]  = signed int16 real I
bits [31:16] = signed int16 imaginary Q
```

The `.bin` files contain consecutive `uint32` words in little-endian byte
order. Thus each sample is stored as `I_low, I_high, Q_low, Q_high` bytes.

```text
FFT length             2048 samples
bytes per complex      4
DMA input length       8192 bytes
DMA output length      8192 bytes
input/output bin order natural sample/bin order
```

The Ubuntu application should copy `fft_input.bin` to the MM2S buffer,
transfer exactly 8192 bytes, receive exactly 8192 bytes through S2MM, and
write those bytes unchanged as `fft_actual.bin`.

## MATLAB reference convention

MATLAB treats the stored signed integers as integer units:

```matlab
x = complex(double(input_i),double(input_q));
ideal_fft_scaled = fft(x,2048)/2048;
```

`ideal_fft_scaled` is a numerical reference, not a bit-exact XFFT model.
The IP performs convergent rounding at internal stages, so this delivery
intentionally does not generate `fft_expected.bin`. Claiming that a simple
cast of MATLAB's double result is bit-exact would be incorrect.
