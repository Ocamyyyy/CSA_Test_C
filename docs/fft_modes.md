# S5、S7、S10独立FFT模式测试

当前状态：虚拟机软件与参考数据验证完成；三种模式的板级执行及MATLAB比较均为`NOT_RUN`。

## 固定模式表

| 阶段 | 操作 | `mode_sel` | 配置字 | 点数 | 帧字节 | 缩放计划 |
|---|---|---:|---|---:|---:|---|
| S5 | FFT | 3 (`11`) | `0x15550C` | 4096 | 16384 | `0xAAA`，总右移12位 |
| S7 | IFFT | 2 (`10`) | `0x15540C` | 4096 | 16384 | `0xAAA`，总右移12位 |
| S10 | IFFT | 0 (`00`) | `0x0D540B` | 2048 | 8192 | `0x6AA`，总右移11位 |

数据均为little-endian 32-bit字、低16位I、高16位Q、signed int16、natural order、convergent rounding。
软件驱动两位`mode_sel`并执行新版可重复GPIO握手：请求保持高时确认`done=0`，释放请求后确认新的
`done=1`。配置字由已核实RTL模式映射产生。

## 软件边界

`--validate-fft-mode --fft-stage S5|S7|S10`解析实际`radarsat1-csa-fft-mode-v1` Manifest，核对模式、
点数、配置字、方向、缩放、字布局、输入长度和SHA-256，不访问设备。`reference.mat`仅做只读完整性审计，C程序不解析MAT。

`--run-fft-mode`复用已有DMA、GPIO、u-dma-buf、缓存同步、Switch和有界清理：

1. 所有Manifest、输入和部署契约门禁在设备访问前完成；
2. 初始DMA reset一次，提交并读回FFT Switch路由；
3. 配置一次FFT，准备输入/输出缓存并逐区域同步给设备；
4. 一次提交完整帧：S5/S7为16384字节，S10为8192字节；S2MM先于MM2S；
5. 等待新的双向IOC+Idle、无错误、S2MM实际长度精确等于帧长；
6. 同步回CPU，原样保存`actual.bin`，安全时恢复原Switch路由。

4096点绝不拆成两个2048帧。MM2S LENGTH=16384使DMA只在第4096个32-bit样本产生输入TLAST；程序通过
双DMA完成和S2MM实际长度检查帧结果，但没有CPU可见XFFT TLAST计数器，真实TLAST仍只能由DMA证据、ILA/仿真和数值比较间接确认。

DMA BTT为15位，16384小于32767。输入/输出区域按实际页大小布局并至少隔一页，长度、地址位宽、低1GiB窗口、
u-dma-buf范围和Cache所有权均在运行时检查。错误后停止且不重试；若DMA停止或FFT边界不明则不强行恢复Switch或复用缓冲。

根目录`fft_modes_hardware_contract.json`记录三组已提供RTL模式映射，`protocol_status=VERIFIED`，但整体部署状态仍为
`UNVERIFIED`。真实运行要求当前加载bitstream与本模式表一致，并在该独立契约中补充板端deployment identity证据；
仅修改`status`字段不能替代该确认。
新版控制器会在请求保持高时清除旧`config_done`，因此模式运行不再要求全局复位；软件仍要求完整观察
本次`done 1→0→1`（首次允许从0开始）并保留部署身份和DMA独占门禁。

## Ubuntu离线校验

```sh
./build/ffttest --validate-fft-mode --fft-stage S5 \
  --input references/fft_modes/S5/input.bin --output S5-actual.bin \
  --manifest references/fft_modes/S5/manifest.json --contract fft_modes_hardware_contract.json
./build/ffttest --validate-fft-mode --fft-stage S7 \
  --input references/fft_modes/S7/input.bin --output S7-actual.bin \
  --manifest references/fft_modes/S7/manifest.json --contract fft_modes_hardware_contract.json
./build/ffttest --validate-fft-mode --fft-stage S10 \
  --input references/fft_modes/S10/input.bin --output S10-actual.bin \
  --manifest references/fft_modes/S10/manifest.json --contract fft_modes_hardware_contract.json
```

离线校验不创建输出。根目录候选契约可用于静态协议检查；它仍会阻止`--run-fft-mode`。

## 板端人工命令（本轮不执行）

部署静态ELF、三组input/manifest及有当前bitstream证据的VERIFIED契约。先执行只读inspect和三组validate。
每一组输出名必须不存在；`--confirm-fresh-reset`仅为旧命令兼容，可省略：

```sh
sudo ./ffttest --run-fft-mode --fft-stage S5 \
  --input S5/input.bin --output S5/actual.bin --manifest S5/manifest.json \
  --contract fft_modes_hardware_contract.json --timeout-ms 2000 --log S5/run-001.log

sudo ./ffttest --run-fft-mode --fft-stage S7 \
  --input S7/input.bin --output S7/actual.bin --manifest S7/manifest.json \
  --contract fft_modes_hardware_contract.json --timeout-ms 2000 --log S7/run-001.log

sudo ./ffttest --run-fft-mode --fft-stage S10 \
  --input S10/input.bin --output S10/actual.bin --manifest S10/manifest.json \
  --contract fft_modes_hardware_contract.json --timeout-ms 2000 --log S10/run-001.log
```

严格按S5→MATLAB确认→S7→确认→S10的顺序推进。回传每组`actual.bin`、完整日志、
退出码、文件大小与SHA-256。软件PASS不代表三种模式已经在板上通过。
