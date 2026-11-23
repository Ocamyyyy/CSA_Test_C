# FFTTest_UserSpace — Repository Agent Instructions

## 1. Project scope and responsibility

This repository is the **Linux AArch64 userspace validation and execution software** for the thesis mainline ③ SAR/CSA FPGA work, currently targeting the PCIE715 board and its JFMQL100TAI900 device. Its job is to execute and measure FFT frames through the already implemented PS–AXI DMA–XFFT–AXI DMA datapath. It is **not** the Vivado FPGA design, the MATLAB reference model, or the mainline ② runtime scheduler.

Keep these three environments distinct:
- **Windows / Vivado hardware**: `D:\Workspace\SARTest\FFTTest`.
- **Windows / MATLAB tests**: `D:\Workspace\SARTest\FFTValidation` and source SAR model `D:\Workspace\SAR-Synthetic-Aperture-Radar`.
- **Ubuntu VM / userspace code**: `/home/fmsh/FFTTest_UserSpace` (this repository).

`references/` contains an imported hardware/data **snapshot**, not automatically the latest Windows state. Never silently edit the external Windows repositories, claim a local snapshot is current, or require cross-repository paths to compile this project. If a source/header/library/build script is genuinely needed from elsewhere, copy the necessary version into this repository and record its origin, version, license where relevant, and any modifications. The repository must build independently with its declared AArch64 cross-toolchain.

## 2. Architecture and development boundaries

Maintain clear boundaries:
1. **CLI / input & metadata**: argument parsing, hardware contract and manifest checks, file size, output naming, frame accounting, and statistics.
2. **Execution orchestration**: single-frame or sequential Batch lifecycle, once-only setup, per-frame operations, error handling and cleanup.
3. **Hardware adapters**: DMA MMIO, `gpio-xilinx` character device, `u-dma-buf` mapping and cache synchronization.

Prefer minimal modifications to existing `include/`, `src/`, and tests. Reuse the proven single-frame implementation and hardware adapters rather than adding a second DMA/GPIO stack. Do not couple this program to mainline ② `FpgaBackend`; provide a clean low-level interface that could later be reused there. Do not introduce unrelated frameworks, permanent test scaffolding, or broad refactors.

## 3. Validated baseline: preserve it

The existing `--run`, `--inspect`, `--validate`, CLI contracts, and single-frame validation must remain compatible. A 2048-point forward FFT on the physical board has already passed an impulse test and a RADARSAT-1 real-echo single-frame test against MATLAB. The real-data comparison achieved relative L2 error ≈0.1685%, SNR ≈55.47 dB, and correlation ≈0.99999858. These are **single-frame board results**, not Batch validation.

Current 2048 forward-mode data contract:
- `NFFT=2048`; 2048 complex samples × 4 bytes = **8192 bytes per frame**.
- Signed int16 I/Q; low halfword I, high halfword Q; little-endian.
- Forward FFT, natural output order, `SCALE_SCH=0x6AA` and total gain `1/2048`; configuration word `0x0D550B`.
- AXI DMA base `0x40400000`, AXI GPIO base `0x41200000`.
- Board-observed `udmabuf0` physical base `0x30000000`, driver version `4.5.3`; **discover actual properties from sysfs at runtime**, never hardcode the buffer address as a substitute.
- GPIO mode bits offsets 0/1, request offset 2, done offset 3; check live mapping and contract.

Do not change any hardware-sensitive assumption, FFT mode/length, frame encoding, or original golden tests without explicit justification and corresponding reference/board evidence.

## 4. Current milestone: sequential RADARSAT-1 FFT Batch

Implement and validate **2 → 16 → 64 frames**, in that order, using a single configured 2048-point FFT core and per-frame AXI DMA Simple Mode transfers. This is a **CPU-submitted sequential Batch**, not a giant one-shot DMA transfer and not gapless hardware streaming. No new Vivado IP, bitstream, DFX, scatter-gather DMA, double buffering, or parallel pipeline is in scope.

Keep `--run` unchanged and introduce a separate Batch command if needed, such as `--run-batch --frames N --input ... --output ... --manifest ... --contract ... --stats ...`.

Shared Windows/Linux Batch file contract:
- One headerless binary input containing N consecutive frames, **8192 bytes per frame**, with exactly `N*8192` bytes; the output is the same format and order, with no header or padding.
- Manifest must unambiguously state the contract identity, frame count/size, FFT parameters, encoding/endianness, and frame ordering. Validate all relevant fields and sizes **before** hardware access; reject mismatch and integer overflow.
- MATLAB alone produces input vectors and the floating-point golden FFT of the **quantized, actually transmitted** int16 data. Never generate fake `fft_batch_output.bin` or silently alter FPGA output to pass comparison.
- Batch test vectors use real RADARSAT-1 echo columns and a **single I/Q scale factor shared across 2/16/64 frames**. Preserve selected column IDs and frame ordering in metadata.

Batch execution rules:
1. Verify contract, deployment, exclusivity, addresses, file paths, and fresh global-reset prerequisites before the **one initial configuration**. `--confirm-fresh-reset` is a truthful operator declaration, not a reset operation.
2. Map/open resources, initialize DMA, and configure FFT via GPIO **once at Batch start**. Successful `config_done` is sticky high; keep `config_req` low, never expect a new done edge each frame or call `fft_configure()` inside the loop.
3. For **every frame**, read exactly 8192 bytes, prepare input and output buffers, perform required device cache synchronization, verify idle/no-error status, clear stale MM2S/S2MM IOC (DMASR W1C bit `0x1000`) and check it cleared, reprogram **both** addresses and `LENGTH=8192`.
4. Arm **S2MM before MM2S**; await fresh completion of **both** channels (`IOC` and `Idle`), check errors and actual received length, using bounded timeouts. Do not infer completion from a stale IOC or require observing a transient Busy state.
5. Synchronize buffers back to CPU **only after safe completion**; save one unmodified 8192-byte output frame, record its stats, then reuse the buffers for the next frame.
6. After any failure, stop immediately, retain completed frames and failure evidence, use bounded safe cleanup, and never automatically retry, reconfigure FFT, or assume clean frame boundaries. If DMA stop cannot be established, quarantine the buffer and do not CPU-sync or reuse it.

The current FFT design has no CPU-visible XFFT TLAST event counters. Do not claim direct TLAST observation on board; infer framing only from supported DMA evidence and numerical comparison. If necessary, request separate simulation/ILA evidence.

## 5. Timing and acceptance

Use a monotonic clock with meaningful microsecond resolution. Clearly separate DMA phase timing, per-frame software elapsed time, and whole Batch elapsed time (including setup/teardown if that is the defined boundary). Record frame index, offsets, output byte count, DMA states, elapsed times, status, failure index, and aggregate throughput. **Do not report DMA polling duration or file/synchronization time as FFT core-only latency.**

Host tests, cross-compilation, QEMU, and sanitizers prove software behavior only. Physical-board acceptance requires, in order:
- 2 distinct frames under a **single FFT configuration without inter-frame PL reset**;
- then 16 and 64 real-echo frames;
- each FPGA frame independently compared on Windows with its MATLAB reference using established single-frame error thresholds;
- no timeouts, DMA errors, missing/reordered/repeated frames, or silent cross-frame contamination.

Never mark `FFT_BATCH_BOARD_TEST_PASS=1` without genuine board logs and MATLAB per-frame comparisons. Likewise do not treat an offline `--validate` PASS as hardware execution. Preserve original impulse/RADARSAT-1 single-frame artifacts and their known results.

## 6. Agent working procedure

Before a change, inspect the relevant code and existing tests, identify the smallest set of edits, and check existing hardware contracts and documented assumptions. Separate facts actually established by source/board logs from hypotheses and planned behavior. Prefer one focused feature at a time.

After a change:
- Run existing regression tests and new focused tests; cross-compile the **AArch64 static ELF** and verify its target architecture.
- Exercise malformed manifest/length, stale IOC, duplicate frames, output errors, abort and bounded timeout paths with host-side mocks where available.
- Report exactly which files changed, CLI/contract changes, tests run and their **actual** results, remaining risks, and whether the real board was accessed.
- Do **not** modify the live board, deploy an ELF, run DMA, overwrite golden vectors or edit Vivado/Windows sources without explicit authorization.

Known unrelated issue: `eth0` PHY attach failure (`error: -22`) does not block local SD-card-based FFT testing; do not expand this milestone into network debugging.
