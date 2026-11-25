# RADARSAT-1 2048点 FFT→M2 DDR串联测试

状态：虚拟机软件/数据验证；板级执行与MATLAB数值比较均为`NOT_RUN`。

## 数据与协议

`references/fft_m2_com/`包含8192字节`fft_input.bin`、8192字节`m2_coeff.bin`、两份MATLAB参考和
`radarsat1-fft-m2-chain-v1` Manifest。离线审计逐文件重算Manifest中的SHA-256；程序还会在任何硬件访问前
计算实际输入/系数SHA-256并与Manifest比较。同长度但内容不同的文件会被拒绝；程序不修改这些文件。

FFT固定2048点Forward、mode 01、配置字0x0D550B、SCALE_SCH 0x6AA、natural order；M2沿用Q1.15协议。
串联以DDR文件为边界，不假设FFT与M2存在PL内部直连：

```text
fft_input.bin
  → FFT Switch路由 → DMA–XFFT–DMA
  → 原样保存fft_actual.bin
  → 同一文件原始8192字节（无fftshift、无重排、无重新量化）
  → M2 Switch路由 → 系数MM2S装载 → 数据MM2S/S2MM处理
  → 原样保存fft_m2_actual.bin
```

FFT阶段和M2阶段分别保存/恢复进入时的Switch路由。FFT阶段完成后关闭并释放DMA/udmabuf/GPIO，M2阶段重新检查
独占状态、复位DMA控制器以清除跨阶段IOC，再使用已有M2实现完成系数装载和处理。任一阶段失败立即停止；不重试、
不自动重配FPGA。DMA停止或数据边界无法证明时保留现场并不强行恢复路由。

## 命令

```text
--validate-fft-m2   仅检查Manifest、两份契约、输入/系数长度/SHA和路径；不打开设备
--run-fft-m2        执行一次FFT和一次M2
--fft-output PATH   中间原始FFT输出，默认fft_actual.bin，必须不存在
--output PATH       最终原始M2输出，默认fft_m2_actual.bin，必须不存在
--m2-contract PATH  独立M2契约，默认m2_hardware_contract.json
```

`--contract`指定FFT契约，`--manifest`指定串联Manifest，`--coeff`指定Q1.15系数。
新版`--run-fft-m2`使用可重复done清零/置位握手，不再要求全局复位；`--confirm-fresh-reset`仍可作为旧脚本
兼容参数，但不会执行任何复位。
离线校验接受静态协议已核实但部署字段仍UNVERIFIED的候选契约；真实运行仍要求两份契约的部署身份、GPIO映射、
DMA地址身份和独占状态全部VERIFIED，并继续执行运行时driver/地址/映射检查。

## 板端人工顺序（本轮不执行）

使用SD卡或现有人工方式部署静态ELF、输入、系数、Manifest及两份已经核实的板端契约。先执行：

```sh
chmod +x ./ffttest-chain
sha256sum ./ffttest-chain fft_input.bin m2_coeff.bin fft_m2_manifest.json
sudo ./ffttest-chain --inspect --contract hardware_contract.json --log inspect-fft-before.log
sudo ./ffttest-chain --inspect-m2 --log inspect-m2-before.log
./ffttest-chain --validate-fft-m2 \
  --input fft_input.bin --coeff m2_coeff.bin \
  --fft-output fft_actual.bin --output fft_m2_actual.bin \
  --manifest fft_m2_manifest.json --contract hardware_contract.json \
  --m2-contract m2_hardware_contract.json
```

只有inspect和validate均通过、且确认当前bitstream为新版握手RTL后才运行：

```sh
sudo ./ffttest-chain --run-fft-m2 --confirm-fresh-reset \
  --input fft_input.bin --coeff m2_coeff.bin \
  --fft-output fft_actual.bin --output fft_m2_actual.bin \
  --manifest fft_m2_manifest.json --contract hardware_contract.json \
  --m2-contract m2_hardware_contract.json \
  --timeout-ms 2000 --log run-fft-m2-001.log
printf 'exit=%s\n' "$?"
wc -c fft_actual.bin fft_m2_actual.bin
sha256sum fft_actual.bin fft_m2_actual.bin run-fft-m2-001.log
```

两份输出都回传Windows。先按Manifest阈值比较FFT；M2位精确参考必须从实际`fft_actual.bin`重新计算，不能用
双精度`fft_reference`代替实际M2输入。Ubuntu软件Mock生成的临时模拟字节不是FPGA输出，不用于MATLAB验收。
