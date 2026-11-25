> 2026-11-25协议更新：本文原有fresh-reset/sticky-done步骤是旧RTL的历史测试计划，不得用于新版部署。
> 新版软件采用`req=1`等待done清零、`req=0`等待新done置位的可重复握手；连续模式板测见
> `docs/fft_sequence.md`。用户此前报告的单帧结果仍保留，但不证明新版连续切换已通过。
> 下文为第二阶段留存的准备计划；本地候选契约不代表板端实际已核实版本。
> 新增Batch的软件接口和首次2帧计划见[batch.md](batch.md)，Batch板级测试仍为NOT_RUN。

# FFTTest板端测试计划（尚未执行）

当前 `FFTTEST_USERSPACE_READY_FOR_BOARD_TEST=0`。
已完成静态审计及软件修正，下面是**人工部署/验证步骤**，不是执行成功记录。
当前hardware_contract.json仍会拒绝--validate/--run；先闭合本节阻断项。
没有自动SSH、SCP、DMA或FPGA重配置命令。

## 部署文件及目录

在板端可写工作目录`FFTTest`中放置：

```text
FFTTest/
├── ffttest                         # 来自build/ffttest，AArch64静态ELF
├── hardware_contract.json          # 当前候选契约，待板端证据确认
├── fft_test_manifest.json          # 明示由MATLAB配置文档派生
├── hardware_contract.md
├── impulse/fft_input.bin           # 来自references/vectors/impulse，8192字节
├── tone/fft_input.bin              # 同理，8192字节
├── random/fft_input.bin            # 同理，8192字节
└── logs/
```

只复制原始数据，保留三组不同目录；不覆盖输入，输出与日志必须选用不存在的新文件名。
参考包的SHA校验在Ubuntu完成，板端用hardware_contract.md中的三组输入哈希复核。
另保留`build/stage2/ELF-SHA256.txt`，核对部署的ffttest；不要部署ffttest-host。

## 解除契约门禁所需证据

允许先执行help和inspect；这些不要求VERIFIED契约。

1. 确认Windows参考版本与当前已加载bitstream对应，记录生成/部署版本，
   说明xc7z100/PS7标记与JFMQL AArch64平台的兼容对应关系。
2. inspect确认gpio-xilinx绑定41200000、live DT widths3+1/is-dual1、ngpio4，
   line info及厂商GPIO实现对应offset0/1/2/3。程序打印mapping VERIFIED才接受配置。
3. 记录udmabuf driver_version=4.5.3、phys_addr、size、sync_mode及所有同步属性。
   确认该地址与AXI DMA处于相同总线地址域，无IOMMU错配，输入输出在BD的[0,1GiB)内。
4. 确认DMA无内核driver且DT节点disabled、无其他用户态DMA/udmabuf/wrapper消费者。
5. 建立一次已知的**整个XFFT和fft_config_ctrl全局复位**，且尚无旧请求。
   不能以DMA的Reset位、Linux重启（可能未复位PL）或done=0单次采样替代证明。

只在有记录后更新候选JSON：`target_gpio_mapping`、`deployment_identity`→`VERIFIED`，
`dma_address_identity`、`exclusive`→true，`status`→`VERIFIED`，evidence追加记录位置。
不要修改mode/BTT等已确认值来绕过错误。
若Windows另给manifest，先核对与派生版本的所有FFT字段；差异必须调查。

## 板端顺序

### 1. ELF与非破坏性检查

从FFTTest工作目录人工执行：

```sh
uname -a
file ./ffttest                         # 若板端没有file，在Ubuntu检查并核对ELF SHA
sha256sum ./ffttest
chmod +x ./ffttest
./ffttest --help
mkdir -p logs
sudo ./ffttest --inspect --contract hardware_contract.json --log logs/inspect-before-001.log
```

保留退出码；inspect只读DMA、DT、udmabuf、GPIO line info，不改变GPIO方向，不写DMA寄存器。
任何UNVERIFIED映射必须调查，禁止直接进行run。

### 2. 离线校验三组输入

在解除上面的契约门禁之后：

```sh
sudo ./ffttest --validate --mode 2048 --input impulse/fft_input.bin \
  --contract hardware_contract.json --manifest fft_test_manifest.json --log logs/validate-impulse-001.log
sudo ./ffttest --validate --mode 2048 --input tone/fft_input.bin \
  --contract hardware_contract.json --manifest fft_test_manifest.json --log logs/validate-tone-001.log
sudo ./ffttest --validate --mode 2048 --input random/fft_input.bin \
  --contract hardware_contract.json --manifest fft_test_manifest.json --log logs/validate-random-001.log
```

validate不启动DMA、不打开设备。若契约还为UNVERIFIED，拒绝是正确门禁行为，不是硬件失败。

### 3. impulse首帧

确认仍是fresh-reset且无请求的初始状态后，人工执行：

```sh
sudo ./ffttest --run --confirm-fresh-reset --mode 2048 \
  --input impulse/fft_input.bin --output impulse/fft_actual.bin \
  --contract hardware_contract.json --manifest fft_test_manifest.json \
  --timeout-ms 2000 --log logs/run-impulse-001.log
# 紧接程序结束记录退出码，或由终端会话录制保存
printf 'exit=%s\n' "$?"
wc -c impulse/fft_input.bin impulse/fft_actual.bin
sha256sum impulse/fft_input.bin impulse/fft_actual.bin hardware_contract.json fft_test_manifest.json \
  > logs/impulse-sha256-001.txt
sudo ./ffttest --inspect --contract hardware_contract.json --log logs/inspect-after-impulse-001.log
```

必须看到：本次基线done0、req上升沿、本次done1、req回0；两DMA无错误、IOC+Idle、接收长度8192，
输出长度8192。保持done高是当前RTL的正常结果，不要求它回0。
先回传impulse输出和日志给Windows MATLAB按正确整数单位/缩放比较；
理想impulse频谱每bin约8，只是数值检查线索，不把它当作位精确expected.bin。
记录“软件传输完成”和“MATLAB数值比较通过”为不同判据。

### 4. impulse确认成功后才允许tone

**本RTL不能在旧done高的状态可靠重配。不得直接再次run，也不得伪造fresh-reset声明。**
由板端操作员通过已确认的既有操作建立新global reset（例如确认会复位PL的整板冷启动），
不由本程序重配置FPGA，不修改bitstream。保存上一轮文件，确认仍装载同一已审计版本，
重新做ELF/inspect/契约及输入校验，再人工执行：

```sh
sudo ./ffttest --run --confirm-fresh-reset --mode 2048 \
  --input tone/fft_input.bin --output tone/fft_actual.bin \
  --contract hardware_contract.json --manifest fft_test_manifest.json \
  --timeout-ms 2000 --log logs/run-tone-001.log
wc -c tone/fft_actual.bin
sha256sum tone/fft_input.bin tone/fft_actual.bin > logs/tone-sha256-001.txt
sudo ./ffttest --inspect --contract hardware_contract.json --log logs/inspect-after-tone-001.log
```

回传MATLAB检查正向复数tone峰在0基bin37。不能只凭峰位置就宣布所有样本位精确。
若无法安全建立fresh-reset，则在这里停止；这是真实阻断，不能增加timeout解决。

### 5. tone确认成功后才允许random

再次建立并确认同样fresh-reset前提，重复inspect/validate后人工执行：

```sh
sudo ./ffttest --run --confirm-fresh-reset --mode 2048 \
  --input random/fft_input.bin --output random/fft_actual.bin \
  --contract hardware_contract.json --manifest fft_test_manifest.json \
  --timeout-ms 2000 --log logs/run-random-001.log
wc -c random/fft_actual.bin
sha256sum random/fft_input.bin random/fft_actual.bin > logs/random-sha256-001.txt
sudo ./ffttest --inspect --contract hardware_contract.json --log logs/inspect-after-random-001.log
```

全部实际输出原样回传Windows；不在Ubuntu执行FFT补偿或修改输入/输出以匹配参考。
若长期需求是同一上电周期连续运行，应先由硬件负责人设计可观察的ack机制并重新审计。

## 失败诊断

| 现象 | 收集/排查 | 停止条件 |
|---|---|---|
| GPIO请求失败 | chip label/设备关联路径、权限、line consumer、是否已sysfs export；live DT宽度及driver | 不抢占线路，不绕过内核MMIO |
| 初始done高 | 前轮配置/bootloader/其他进程；req低不会清高 | 不发新边沿；要求fresh global reset |
| 初始done低但配置超时 | 旧pending、FCLK/aresetn、config_tvalid/tready、实际加载版本；RTL pending期间忽略新边沿 | 不无限脉冲重试，不用DMA Reset冒充FFT复位 |
| DMA一直Halted | RS是否写入、Reset是否清除、50MHz/aresetn、AXI Lite路径；复位前后CR/SR | 不重复启停覆盖现场 |
| Internal Error bit4 | S2MM包长超过8192、帧/TLAST/NFFT不符；保存双方LENGTH及状态 | 不扩大缓冲/BTT掩盖错误 |
| Slave Error bit5 | DDR/AXI slave response、权限/互连、实际内存窗口 | 核对地址域后再运行 |
| Decode Error bit6 | phys_addr、offset、32-bit与1GiB窗口、HP0地址可达性、IOMMU | 不硬写0x30000000绕过sysfs |
| S2MM超时 | MM2S是否IOC；XFFT配置/数据TREADY；输出TVALID/TLAST；第2048个有效采样 | 只能报告TLAST可能缺失，不能无波形认定根因或用长timeout掩盖 |
| udmabuf同步失败 | driver_version、sync_offset/size/direction、权限、write错误、sync_owner及其他消费者 | 不接受输出；DMA未停时不可CPU同步/复用缓冲 |
| 输出不足/超过8192 | S2MM actual LENGTH、文件write/fsync错误、部分文件、磁盘空间 | 保留部分文件并标FAIL，不做填零补齐 |
| 清理Reset也超时 | 完整快照与QUARANTINE告警，立即隔离udmabuf0 | 禁止再次用缓冲区，直到人工证明DMA停止 |

CPU/device同步失败、部分输出或任何错误都不算通过。每次失败只做有界DMA控制器清理，
不会全局复位XFFT/wrapper、重配FPGA或重试传输。

## 必须回传的记录

ELF SHA、输入/输出/两份JSON SHA、每轮完整stdout/日志/退出码、inspect前后、文件长度、
udmabuf属性、GPIO/DT/绑定证据、fresh-reset建立方式、bitstream身份、MATLAB比较报告。
有TLAST问题则另需Windows仿真/ILA：config valid-ready、NFFT、两侧data valid-ready-last、reset。
所有硬件步骤当前状态均为NOT RUN，不标PASS。
