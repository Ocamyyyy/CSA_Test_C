# FFTTest Ubuntu user-space reference package

Generated on 2026-11-11 from the current Windows FFTTest and FFTValidation
trees. This package is documentation and test input only. Its contents are
read-only references and are not inputs to the Ubuntu C build.

## Copied files

| Package path | Original Windows path | Purpose | Bytes | SHA-256 |
|---|---|---|---:|---|
| `hardware/design_1.bd` | `D:\Workspace\SARTest\FFTTest\FFTTest.srcs\sources_1\bd\design_1\design_1.bd` | Current block design, including reset, address and stream connections | 38571 | `3656ea4e7423e923935b32cedf8bcab7016ebad18d0e54cd3897d6d7eac133cb` |
| `hardware/fft_config_ctrl.v` | `D:\Workspace\SARTest\FFTTest\FFTTest.srcs\sources_1\new\fft_config_ctrl.v` | User-maintained runtime FFT configuration RTL | 2439 | `b14f04f4a0888c644065018efd0209442f57d2a7aecd91a2d2130564a05e5f58` |
| `hardware/xfft.xci` | `D:\Workspace\SARTest\FFTTest\FFTTest.srcs\sources_1\bd\design_1\ip\design_1_xfft_0_0\design_1_xfft_0_0.xci` | Xilinx FFT v9.1 configuration | 34889 | `8961d8ec88115fca1f0195f4666b476ba88179a7fa880fb3c7f06205c092ff66` |
| `hardware/axi_dma.xci` | `D:\Workspace\SARTest\FFTTest\FFTTest.srcs\sources_1\bd\design_1\ip\design_1_axi_dma_0_0\design_1_axi_dma_0_0.xci` | AXI DMA v7.1 Simple Mode configuration | 73797 | `f1e85417fb871217e1fa755fc4a695ad43383d72d361fe3b90331e2def3a4cea` |
| `hardware/axi_gpio.xci` | `D:\Workspace\SARTest\FFTTest\FFTTest.srcs\sources_1\bd\design_1\ip\design_1_axi_gpio_0_0\design_1_axi_gpio_0_0.xci` | AXI GPIO v2.0 channel widths and directions | 17084 | `68c549e4738553f84f1a9f4f770b3a4e21db2fd3bfd3ede1e423b725b18bc8e2` |
| `hardware/proc_sys_reset.xci` | `D:\Workspace\SARTest\FFTTest\FFTTest.srcs\sources_1\bd\design_1\ip\design_1_proc_sys_reset_0_0\design_1_proc_sys_reset_0_0.xci` | Processor System Reset v5.0 traceability | 8936 | `6ba97d784a29eeeb7f3cb662e99d2c71f8e343cfd4c1e8f8e169288c6f29d6f1` |
| `matlab/fft_config.md` | `D:\Workspace\SARTest\FFTValidation\fft_config.md` | MATLAB/Ubuntu FFT interface convention | 2479 | `bc47c29dfce37db447d1fe6b636a4f470787ce17fb665d44cd4fb25dfeeb45c2` |
| `matlab/generate_test_data.m` | `D:\Workspace\SARTest\FFTValidation\generate_test_data.m` | Reproducible MATLAB vector generator | 4971 | `1cf0f22f9b42ba552e617ce50f444334a85ce929cb8d332608938190a8528aaa` |
| `vectors/impulse/fft_input.bin` | `D:\Workspace\SARTest\FFTValidation\impulse\fft_input.bin` | 2048-sample impulse input | 8192 | `49e81a27be2fd1344ef7cea91044ada40db62d1a843e06251aedadb890f331b4` |
| `vectors/tone/fft_input.bin` | `D:\Workspace\SARTest\FFTValidation\tone\fft_input.bin` | 2048-sample complex tone, bin 37 | 8192 | `83db91178f2f3585292e0e70b70d19e36e7abd18df1cfaff3c4818600502f11b` |
| `vectors/random/fft_input.bin` | `D:\Workspace\SARTest\FFTValidation\random\fft_input.bin` | 2048-sample deterministic random input | 8192 | `3e27b5d49b908c8859faf29051151aafa92aec2f5d6594aab5a93fa857e725ab` |

`MANIFEST.md` and `SHA256SUMS.txt` are generated package metadata and do
not replace any source file. `SHA256SUMS.txt` covers every payload above
and `MANIFEST.md`; by standard practice it does not contain its own hash.

## Confirmed hardware parameters

| Item | Confirmed value |
|---|---|
| Target | JFMQL100TAI900 / PCIE715 |
| Vivado | 2018.3 |
| AXI DMA | v7.1, MM2S and S2MM enabled, Scatter/Gather disabled (Simple Mode), 32-bit streams, no DRE |
| AXI DMA control base | `0x40400000` |
| AXI GPIO | v2.0, dual channel; base `0x41200000` |
| GPIO channel 1 | 3-bit output: bits `[1:0]=mode_sel`, bit `[2]=config_req` |
| GPIO channel 2 | 1-bit input: bit `[0]=config_done` |
| XFFT | v9.1, Pipelined Streaming I/O, fixed point |
| FFT operation | 2048-point forward FFT |
| Runtime mode | `mode_sel=2'b01`, `config_tdata=24'h0D550B` |
| Components | signed 16-bit I and signed 16-bit Q; 32-bit AXIS word |
| Packing | `[31:16]=Q`, `[15:0]=I` |
| File byte order | little-endian (`I_low, I_high, Q_low, Q_high`) |
| FFT scaling | schedule `12'h6AA`, total scale `1/2048`, convergent rounding |
| Output order | natural order |
| Clock | FCLK0 = 50 MHz |
| Frame | 2048 complex words = 8192 input bytes = 8192 output bytes |

### Reset and stream topology evidence

The copied `design_1.bd` is the current file modified on 2026-11-10. Its
`proc_sys_reset_0_peripheral_aresetn` net connects
`proc_sys_reset_0/peripheral_aresetn` to the AXI DMA, XFFT, AXI
interconnects, AXI GPIO, and `fft_config_ctrl_0/aresetn`. This is the
post-fix reset topology. `processing_system7_0/FCLK_RESET0_N` drives
`proc_sys_reset_0/ext_reset_in`.

The DMA MM2S stream connects directly to `xfft_0/S_AXIS_DATA`; the XFFT
`M_AXIS_DATA` connects directly to DMA S2MM. The 8192-byte DMA transfer and
AXI Stream TLAST must therefore describe the same 2048-sample frame.

## Ubuntu development notes

1. Treat all `references/` content as read-only. Do not compile these files
   into the user-space C project and do not use the copied XCI/BD to rebuild
   hardware on Ubuntu.
2. AXI GPIO is managed by the Linux `gpio-xilinx` driver. Discover its
   actual `gpiochip` and line offsets from the running system; do not assume
   global GPIO numbers from this package.
3. The AXI DMA device-tree node is expected to be disabled. The user-space
   test accesses the Simple Mode DMA register block through `/dev/mem` and
   polls status; it must not race a bound kernel DMA driver.
4. Use `/dev/udmabuf0` for DMA buffers. Read the current physical address
   and size from the running system/sysfs; never permanently hard-code the
   buffer physical address.
5. GPIO configuration handshake must follow the real RTL: set
   `GPIO[1:0]=01`, ensure `config_req` is low, create a low-to-high edge on
   `GPIO[2]`, poll `GPIO2[0]=config_done`, then return `config_req` low so a
   later request can create another rising edge.
6. Before DMA, verify that MM2S produces TLAST on word 2047 and that the
   XFFT/S2MM path returns one 2048-word frame. Program both transfer lengths
   as exactly 8192 bytes.
7. Do not reimplement FFT arithmetic in the Ubuntu application. It only
   sends `fft_input.bin` and saves the unmodified 8192-byte return data.
8. All three supplied inputs are exactly 8192 bytes.
9. Follow the udmabuf driver's documented cache synchronization mechanism
   before MM2S and after S2MM; this requirement cannot be inferred from the
   XCI files alone.

## Items requiring target-Linux confirmation

- Actual `gpiochip` number and line offsets assigned by `gpio-xilinx`.
- `/dev/udmabuf0` physical address, allocated size, and cache-sync ABI.
- That no kernel driver is bound to the disabled AXI DMA node at runtime.
- `/dev/mem` access policy and page mapping on the target image.
- Interrupt wiring is present in the BD, but this reference workflow uses
  Simple Mode polling; Linux IRQ numbers are intentionally not specified.
