M2新增入口、独立契约、系数装载/处理协议和板端命令见[docs/m2_board_test.md](docs/m2_board_test.md)。
实际工作目录仍为FFTTest_UserSpace；M2仅完成软件实现与验证，板级测试NOT_RUN，FFT功能保留。
`--validate-m2`已兼容MATLAB `m2-phase-axis-test-v1`嵌套Manifest；7组`references/m2_phase_axis`
向量的长度、Manifest内SHA-256和离线CLI校验已通过。该结果不等于FPGA执行或数值回读通过。

RADARSAT-1 FFT→M2 DDR中转串联命令、失败边界和人工板测步骤见
[docs/fft_m2_chain.md](docs/fft_m2_chain.md)。串联板测仍为`NOT_RUN`。

# FFTTest 用户态 DMA–FFT–DMA 测试

2048点、每点32-bit复数、输入/输出各8192 Byte。程序只搬运原始文件，不执行FFT或变换I/Q。
用户已报告PCIE715 impulse及RADARSAT-1单帧实测、MATLAB比较通过。
本工作区新增最小Batch软件实现，**多帧板级测试尚未执行**。
根目录仍是UNVERIFIED候选契约，会阻止运行；部署时使用板端已核实的实际契约，不自动修改其确认状态。

- [Batch接口、统计、软件验证及首次2帧部署](docs/batch.md)
- [硬件契约、握手、TLAST与阻断项](hardware_contract.md)
- [只读参考包完整性与版本审计](docs/reference_audit.md)
- [板端部署、逐组测试和失败诊断](docs/board_test_plan.md)
- [工具链记录](docs/toolchain.md)、[软件测试记录](docs/validation.md)

## 构建

```sh
make CROSS_COMPILE=aarch64-linux-gnu-
make test
make elf-check
```

默认使用现有 `/usr/bin/aarch64-linux-gnu-gcc` GCC9.4.0，生成`build/ffttest`静态AArch64 ELF。
目标无JSON/libgpiod/crypto动态库依赖；宿主测试需要gcc、make、Python3。
未安装或修改SDK、内核、设备树和bitstream。`references/`严格只读，不参与C编译。

```sh
# 可显式覆盖工具链；切换编译器或链接选项时选独立TARGET或make -B
make CC=/usr/bin/aarch64-linux-gnu-gcc TARGET=build/ffttest-custom
make host
FFTTEST_COMMAND='qemu-aarch64-static build/ffttest' python3 tests/test_cli.py
```

`make test`运行核心测试、GPIO握手模型、41个CLI用例（含原35个）和参考包只读审计。
QEMU只验证软件，不模拟PL；模拟日志的DMA completed不是真实硬件结果。
`file`/`readelf -h/-l/-d/-n`确认ELF64/AArch64/静态、无PT_INTERP/DT_NEEDED、Linux ABI3.7.0。
与Linux5.4.52基础ABI相容，但板端设备权限、mmap/驱动和硬件行为仍须实测。

可选动态构建及审计：

```sh
make CC=aarch64-linux-gnu-gcc STATIC=0 TARGET=build/ffttest-dynamic
readelf -l build/ffttest-dynamic
readelf -d build/ffttest-dynamic
readelf --version-info build/ffttest-dynamic
```

交叉glibc为2.31。若采用动态版，必须核对板端`/lib/ld-linux-aarch64.so.1`、libc及所需GLIBC符号；
不要在宿主用ldd推断板端依赖。优先部署静态版。

## 已确认与未确认

参考包12项哈希及11个payload大小全部一致。三组输入均8192 Byte。
静态硬件参数已确认：mode_sel=1、config_word=0x0D550B、Forward、I-low/Q-high、little-endian、
signed16、Natural、SCALE_SCH0x6AA/增益1/2048、内部convergent_rounding。
DMA实际BTT是**15位**（1..32767），两方向DRE关闭、数据/地址32bit、非Micro、非SG。
AXIS直连并包含TLAST，实际波形尚未测量。HP0内存窗口为[0,1GiB)。
XCI最大4096与运行时2048不矛盾，本次不修改4096。

`hardware_contract.json`是已填静态参数的**候选契约**，仍有板端必要条件UNVERIFIED。
不能只改status来解锁：程序还检查deployment_identity/target_gpio_mapping、独占和地址域确认，
运行时再次检查live DT/driver、DMA绑定、udmabuf版本、对齐和实际地址窗口。
解除门禁所需证据见board_test_plan。
`fft_test_manifest.json`明确由MATLAB配置文档派生；参考包没有原始MATLAB JSON。
没有提供/伪造fft_expected.bin，也没有把双精度FFT当作位精确模型。

## 参数

```text
--inspect                  只读板端审计，不申请GPIO、不写DMA
--validate                 离线校验契约/manifest/输入，不访问设备
--run                      显式执行单帧，仅VERIFIED契约可用
--confirm-fresh-reset      run必需：操作者确认FFT/wrapper刚全局复位且没有旧请求
--input PATH               默认fft_input.bin，必须恰好8192 Byte普通文件
--output PATH              默认fft_actual.bin，必须不存在
--manifest PATH            默认fft_test_manifest.json
--contract PATH            默认hardware_contract.json
--mode 2048                run/validate必需；这是规模，不是GPIO编码
--timeout-ms N             每阶段1..600000 ms，默认2000
--log PATH                 新建日志，禁止覆盖已有文件
--help
```

GPIO实际接口是**上升沿req、保持done**。拉低req不会清done。
程序只在已确认fresh-reset前提下接受初始done0，然后发边沿等本次done1、拉低req；
初始done1直接拒绝，配置后不等待done0。
初始done0也可能是旧pending，所以单次低电平不能替代fresh-reset确认。
每组独立run之前都要重新建立该前提；DMA软件复位不能清wrapper。
不提供绕过配置、重复脉冲、忽略旧done或自动FPGA复位选项。

GPIO优先Linux5.4 chardev v1；不可访问时使用sysfs。
运行时通过41200000设备路径/label识别chip，通过driver+DT宽度+ngpio核对3+1紧凑映射，
mode/req/done预期偏移0/1/2/3；不固化gpiochip508，不对GPIO直接MMIO。

## JSON协议

硬件契约及单帧manifest保留顶层扁平JSON，只接受字符串、boolean、无符号十进制整数；
拒绝嵌套/数组/null/浮点/转义字符串/重复键，最多32768 Byte、64字段、键63 Byte、值511 Byte。
可以增加符合此格式的元数据字段。
契约与manifest必须逐项一致：contract_id、nfft、bytes_per_sample、byte_order、iq_order、encoding、
direction、scaling、quantization、output_order、config_word。

| 字段 | 语义 |
|---|---|
| nfft / bytes_per_sample | 2048 / 4 |
| byte_order | little 或 big，为32-bit字的文件端序 |
| iq_order | I-low或Q-low，为32-bit字的低16bit分量 |
| encoding | signed-int16二补码 |
| direction / output_order | forward或inverse；natural或bit-reversed |
| scaling / quantization | 完整缩放及量化说明 |
| config_word | wrapper实际写入XFFT的配置字字符串，仅审计/一致性检查，不直接驱动配置AXIS |
| dma_window_base / dma_window_size | 两方向共同可达的DMA总线地址窗口；本BD为0 / 1073741824 |

当前参考版使用little/I-low/forward/natural。字段支持范围不代表可以任意更改本硬件协议。
输入/输出本版使用同一格式，程序始终按字节原样搬运。
未确认模板仍保留在examples，不能用于解锁。

## 使用与日志

板端可先运行：

```sh
./ffttest --help
sudo ./ffttest --inspect --contract hardware_contract.json --log inspect-001.log
```

解除契约门禁后才可进行validate/run。下面仅为**人工命令示例**，本轮未执行：

```sh
./ffttest --validate --mode 2048 --input impulse/fft_input.bin \
  --manifest fft_test_manifest.json --contract hardware_contract.json
sudo ./ffttest --run --confirm-fresh-reset --mode 2048 \
  --input impulse/fft_input.bin --output impulse/fft_actual.bin \
  --manifest fft_test_manifest.json --contract hardware_contract.json \
  --timeout-ms 2000 --log run-impulse-001.log
sha256sum impulse/fft_input.bin impulse/fft_actual.bin fft_test_manifest.json hardware_contract.json run-impulse-001.log
```

DT控制器节点只按basename识别，避免其下dma-channel子节点被重复统计。
`--inspect`以O_RDONLY/PROT_READ访问DMA寄存器，仅在指定--log时创建日志；
不写同步sysfs、不改变GPIO方向。任何关键检查失败返回非零。
输入offset0，输出页对齐且间隔至少一页；所有物理地址从udmabuf sysfs计算。
显式进行CPU/device同步，mapped区使用对齐volatile32bit读写，文件I/O经普通内存中转。
先S2MM再MM2S，长度寄存器最后写；两方向IOC+Idle、无错误且接收8192才保存输出。
输出写入/fsync失败会保留已报告的部分文件，不能算成功。

失败先保存快照，进行一次有界DMA控制器清理，不重试传输。
若清理不能确认Halted，不做CPU同步，报告udmabuf隔离要求；其他进程不得复用，直到证明DMA停止。
协作flock不约束不合作的消费者。各等待阶段独立超时，总用时可能是多个阶段之和。
首轮impulse→tone→random的成功门槛、fresh-reset限制和回传清单详见board_test_plan。

Batch软件使用、manifest模板及数据/计时定义详见[docs/batch.md](docs/batch.md)。
原--run路径保留；新命令为--run-batch --frames 2|16|64 --stats PATH，仍须--confirm-fresh-reset。

Batch回归新增70个软件用例，QEMU与ASan/UBSan均通过；结果与限制见docs/batch.md，日志位于build/batch。

Batch manifest的数组/浮点兼容、MATLAB字段映射及硬件/数据集ID分离见[docs/manifest_compat.md](docs/manifest_compat.md)。
