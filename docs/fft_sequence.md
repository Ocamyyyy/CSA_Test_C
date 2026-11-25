# FFT连续模式切换测试（板测NOT_RUN）

目标顺序固定为`S2→S5→S7→S10→S2`，对应2048 FFT、4096 FFT、4096 IFFT、2048 IFFT、
再次2048 FFT。五帧互相独立，不传递前一帧输出。

## 新版配置握手

每次配置均执行：稳定`mode_sel`且请求为低→拉高请求→保持高并有界等待`config_done=0`→拉低请求→
有界等待新的`config_done=1`。同一进程内第二至第五次配置还要求前一次done基线为高，避免在上一次配置
尚未完成时切换。TREADY延迟包含在第二阶段超时内，任何阶段失败均停止序列，不重试、不重载bitstream。

## 数据与文件

根目录`fft_sequence_plan.json`明确记录实际输入路径。S2复用
`references/fft_m2_com/fft_input.bin`及其含SHA-256的真实FFT→M2 Manifest；S5/S7/S10使用
`references/fft_modes`中的原始数据。S2首尾读取同一输入但分别保存：

```text
S2-first-actual.bin
S5-actual.bin
S7-actual.bin
S10-actual.bin
S2-return-actual.bin
```

离线校验不会创建输出：

```sh
./build/ffttest --validate-fft-sequence --sequence-plan fft_sequence_plan.json \
  --output-dir fft_sequence_output --contract fft_modes_hardware_contract.json
```

## 板端人工运行

保持计划中的相对目录部署输入和Manifest，创建空输出目录。先确认当前bitstream确实包含新版握手RTL，并以
证据将模式契约的deployment identity、GPIO映射、DMA地址域和独占状态闭合为`VERIFIED`。随后人工执行：

```sh
mkdir -p fft_sequence_output
./ffttest --validate-fft-sequence --sequence-plan fft_sequence_plan.json \
  --output-dir fft_sequence_output --contract fft_modes_hardware_contract.json
sudo ./ffttest --run-fft-sequence --sequence-plan fft_sequence_plan.json \
  --output-dir fft_sequence_output --contract fft_modes_hardware_contract.json \
  --timeout-ms 2000 --log fft_sequence_output/run-001.log
```

程序只在序列开始时执行一次DMA控制器复位、一次FFT Switch路由配置；这不是PL/XFFT全局复位。每帧完成
双DMA IOC+Idle、精确接收长度和缓存同步后才请求下一模式。任一失败保留已完整写出的输出，执行有界DMA
清理并停止。五个输出、日志、退出码、大小及SHA-256必须回传Windows逐帧与MATLAB参考比较。

本文件和软件Mock不构成板级通过证据。
