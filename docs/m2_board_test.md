# M2复数相位乘法：软件与板端测试

日期：2026-11-23。本次实际工作目录是`/home/fmsh/FFTTest_UserSpace`；当前环境未发现另一个
`/home/fmsh/CSA_Test_C`目录。这里在现有FFTTest工作区实现CSA_Test_C所请求的M2功能，
没有移动仓库、访问板卡或修改Windows/Vivado工程。M2板级测试为NOT_RUN。
用户提供的实际RTL接口作为M2协议依据，旧references仅为FFT历史快照，不能证明M2已部署。

## 命令和文件

```text
--inspect-m2       读DMA/Switch寄存器、发现M2 GPIO及64条线、读取ch2状态
--validate-m2      离线验证独立M2契约/manifest、输入与系数文件、N与N×4长度
--run-m2           一次系数装载后一次数据处理
--samples N       必需，1..4096
--coeff PATH      系数BIN，默认m2_coeff.bin
--input PATH      M2默认m2_input.bin
--output PATH     M2默认m2_actual.bin，必须不存在
--manifest PATH   M2默认m2_manifest.json
--contract PATH   M2默认m2_hardware_contract.json
--timeout-ms N    每个等待阶段超时，沿用1..600000ms限制，默认2000
--log PATH        新建日志，不覆盖已有日志
```

原--run、--run-batch、--inspect、--validate保留。--coeff/--samples不能用于FFT命令，
M2不使用--mode或--confirm-fresh-reset。M2不触碰FFT GPIO 0x41200000，也不假装执行全局复位。
--inspect-m2不写DMA、Switch或M2控制字；通过chardev临时申请ch2的32条输入线读取状态，
内核会将这些已定义输入设为输入方向，随后释放。不申请输出线，不产生start/clear边沿。

## 契约

根目录m2_hardware_contract.json是独立的候选契约，protocol_status=VERIFIED记录用户提供的RTL定义，
整体status仍为UNVERIFIED；不会因开发软件而擅自确认板端部署、地址域或独占状态。
运行时要求status=VERIFIED、target_gpio_mapping/deployment_identity=VERIFIED，
exclusive/dma_address_identity=true及有效evidence。操作员应使用已确认的板端版本，不能只改标志绕过调查。
BTT/地址位宽/DDR窗口来自现有DMA基线候选参数，需与当前M2硬件实际配置核对，不能把旧快照当新工程证据。
默认BTT15允许16384字节，若实际仅14位则4096点被拒绝；本软件不会更改DMA硬件参数。

内部M2 manifest schema=`ffttest-m2-manifest-v1`，至少包含：

- contract_id：与该M2硬件契约一致，与FFT或Batch数据集ID分开。
- samples=N、bytes_per_sample=4、input_bytes/coeff_bytes/output_bytes=N×4。
- encoding=signed-int16、byte_order=little、iq_order=I-low、coeff_format=Q1.15。
- arithmetic=complex-mul-s33-rshift15-rne-sat16、tkeep=15、tlast=last-sample。

离线校验和板端运行也接受MATLAB `m2-phase-axis-test-v1`：程序严格核对sample_count、两种vector_len、
packed I/Q格式、Q1.15、33位/RNE/饱和算术、验证布尔值以及input/coeff/expected的声明长度和SHA字段格式。
`test_id`是数据集身份，不冒充硬件contract_id；协议字段与独立M2硬件契约绑定。
外部只读审计会重算Manifest列出的实际文件SHA-256。

`--validate-m2`只要求`protocol_status=VERIFIED`及协议/地址参数一致，因此可用根目录候选契约做纯离线数据检查；
它不访问设备。`--run-m2`仍额外要求status、部署身份、GPIO映射、独占和DMA地址身份全部VERIFIED，
不能用离线PASS绕过板端门禁。

两份BIN均为恰好N个32-bit字，不带MAT头；低16位I，高16位Q。
系数由Windows/MATLAB按Q1.15提供，程序不自行产生系数、不重新量化输入、不在CPU修正输出。
复数乘法按所给RTL使用33位有符号累加、2^-15缩放、ties-to-even、signed16饱和；
饱和标志仅报告，不自动视为DMA错误，也不隐瞒或改变饱和输出。数值是否符合预期由MATLAB比较。

## Switch

Input=0x43C00000：MI0(+0x40)=0x80000000 disabled，MI1(+0x44)=0选择SI0。
Output=0x43C10000：MI0(+0x40)=1选择SI1。
每个Switch写CTRL(+0)=0x2提交，有限等待bit1自动清零，再对三个MUX做精确读回校验。
仅在DMA/M2静止时更改路由；提交会使Switch本身短暂soft-reset，不是M2/FFT的全局复位。
两个Switch和DMA共享已有/dev/mem独占协作锁，Switch复用该fd映射，避免多次flock把自己锁死。
检查Switch无内核驱动后才允许用户态写寄存器。

寄存器依据：[PG085 Control Register](https://docs.amd.com/r/en-US/pg085-axi4stream-infrastructure/Control-Register)、
[官方axis_switch寄存器定义](https://xilinx.github.io/embeddedsw.github.io/axis_switch/doc/html/api/xaxis__switch__hw_8h.html)。
契约显式绑定pg085-axi-lite profile；如果实际IP使用其他寄存器布局，应停止并提供证据，不能猜偏移。
MUX读回只能核实寄存器，不是直接观测AXIS数据、TKEEP或TLAST。

入口保存原三项路由，正常完成且M2无busy/error、DMA停止/完成后提交恢复并再次读回，
使下一次FFT仍使用原路由。异常时若M2状态不明、busy/error或DMA停止无法确认，**不强行提交恢复**，
报告routes NOT restored；不得直接运行FFT，需操作员建立安全状态并核对路由。
程序不会为了恢复路由复位整个FPGA、重试M2或在处理中修改Switch。

## GPIO和新完成确认

M2必须使用gpio-xilinx chardev，按41210000平台父路径定位，不固定任何gpiochip编号。
核对live DT宽度32/32、is-dual=1、driver及64条line info。
ch1输出offset0..31，ch2输入offset32..63；分别申请两个32-line handle，不用/dev/mem或sysfs绕过驱动。
输出SET后读取输出handle核对完整32位控制字；若厂商gpio-xilinx多线写不正确，则拒绝继续。
状态连续读取两次相同才接受，避免GPIO v1可能逐pin读取时混合busy/done/code过渡值。

控制：start bit0；load_mode bit1；vector_len bits14:2；clear_error bit15。
初始先确认busy=0，设置有效N及load_mode，再只发一次clear_error上升沿，确保系数状态失效后重新装载。
clear_error不要求清done；应用不依赖这个未声明的副作用。
start前低/高电平各保持至少1µs软件余量；保持N和mode稳定，高→低重置请求，供下一次上升沿使用。

每次新start后，在任何本次DMA数据发送之前等待两个旧done位均为0；
新完成必须在相应DMA完成后为本次期望的done、busy=0且无error。
不强制看见短暂busy边沿，也不会把旧sticky high当新完成。
M2无需逐次全局复位来清done，因为所给RTL明确声明新start清旧完成，而且清零检查发生在送数之前。

状态：busy bit0，load_done bit1，process_done bit2，error bit3，code bits6:4，sat_i bit7，sat_q bit8。
错误码1/2/3/4/7按用户提供协议输出名称，任何error均停止，不自动clear后重试。

## 执行与缓存

```text
校验两份文件和契约；输出O_EXCL预留
  → DMA/udmabuf独占、运行时地址/边界/非重叠检查，映射一次
  → GPIO输入检查busy；申请控制；DMA初始复位一次
  → 保存并提交M2路由；初始clear_error一次
  → 读系数，CPU写输入区，sync_for_device(TO_DEVICE)
  → load_mode=1、N、start边沿、新done清零检查
  → MM2S-only N×4；S2MM仍Halted、不写S2MM寄存器
  → MM2S IOC+Idle及load_done，CPU取回输入区所有权
  → 读数据，写输入/清输出；分别TO_DEVICE/BIDIRECTIONAL同步
  → load_mode=0、相同N、start边沿、新done清零检查
  → S2MM地址/LENGTH先提交，随后MM2S地址/LENGTH，均N×4
  → 双方新的IOC+Idle、无DMA错误、实际接收长度N×4和process_done
  → CPU同步，读取原始N×4输出，写新文件并fsync
  → 确认静止后恢复原路由、释放资源
```

从load_done到process_done之间没有M2 clear/reset、DMA Reset或Switch提交。
可变长度DMA及缓冲区复制是原模块新增辅助接口，旧FFT固定8192函数保持原文。
所有地址从udmabuf sysfs取得，未固化0x30000000；输入/输出页对齐并间隔至少一页，支持4096点16KiB。
Cache区域用对齐volatile32位读写，显式调用4.5.3同步接口，不能仅依赖sync_mode=1。

异常先保存DMA/M2快照，执行一次有界DMA-only reset；只有证明停止才归还CPU所有权。
如果停止无法证明则QUARANTINE、不CPU-sync、不恢复Switch、不复用缓冲。
M2可能还等待流数据；软件不会擅自清系数或假设下一帧边界干净。
输出I/O失败可能保留部分文件，始终返回非零并报告；不覆盖已有FFT黄金数据。

load_dma_us/process_dma_us包含各自提交及轮询，total_us包含初始化、GPIO等待、同步、文件和清理；
它们是软件阶段计时，不是M2核心纯计算延迟。等待阶段各自有timeout，清理总时长可为多个阶段之和。

## 人工部署和首帧命令（本轮不执行）

Ubuntu：

```sh
make test
make CROSS_COMPILE=aarch64-linux-gnu-
make elf-check
sha256sum build/ffttest
```

保留板端已通过的FFT ELF和黄金输出。经现有SD卡/人工文件传输将build/ffttest复制为新文件名
`ffttest-m2`，另放入已核实M2契约、匹配manifest、MATLAB原始输入和Q1.15系数。
不要直接用未核实候选契约冒充板端部署确认；不处理eth0 PHY已知问题。

```sh
chmod +x ./ffttest-m2
./ffttest-m2 --help
sha256sum ffttest-m2 m2_input.bin m2_coeff.bin m2_hardware_contract.json m2_manifest.json
sudo ./ffttest-m2 --inspect-m2 --log m2-inspect-001.log
./ffttest-m2 --validate-m2 --samples 2048 \
  --input m2_input.bin --coeff m2_coeff.bin \
  --contract m2_hardware_contract.json --manifest m2_manifest.json
sudo ./ffttest-m2 --run-m2 --samples 2048 \
  --input m2_input.bin --coeff m2_coeff.bin --output m2_actual-001.bin \
  --contract m2_hardware_contract.json --manifest m2_manifest.json \
  --timeout-ms 2000 --log m2-run-001.log
printf 'exit=%s\n' "$?"
wc -c m2_actual-001.bin
sha256sum m2_input.bin m2_coeff.bin m2_actual-001.bin
sudo ./ffttest-m2 --inspect-m2 --log m2-inspect-after-001.log
```

检查：64条线映射、两Switch路由提交/读回、load只MM2S、两次fresh start、load_done/process_done、
N×4字节实际接收、error/code、sat_i/q以及原路由恢复。输出原样回传MATLAB，按已量化输入与Q1.15系数比较。
先用简单可诊断系数/数据，再扩大长度；不要把Q1.15的32767误称可精确表示+1.0。
本机模型验证不等于实测TLAST或FPGA算术通过。

## 修改与软件验证结果

新增：include/m2.h、src/m2.c、src/m2_gpio.c、src/axis_switch.c、独立m2_hardware_contract.json/m2_manifest.json。
主CLI增加三个M2动作及coeff/samples参数；DMA/udmabuf模块仅追加可变长度辅助接口，
旧FFT/Batch DMA函数、固定8192缓冲区函数、FFT GPIO和FFT/Batch执行分支保留原文。
Switch复用DMA的有序MMIO读写、/dev/mem fd和有界清理，没有另建DMA驱动栈。

新增测试：tests/m2_mock.c、tests/test_m2.py、tests/test_m2_layout.c、tests/test_m2_gpio_api.c。
Makefile纳入测试；test_access只补链接桩。
现有test_manifest_compat已适配操作员导入的合法修订版Batch身份字段，并始终保留错误数据集/硬件ID绑定负例；
未修改任何导入的manifest、BIN或MAT。所有references哈希在本轮前后完全一致。

| 验证 | 实际结果 | 记录 |
|---|---|---|
| make test | PASS：原核心/GPIO、41个CLI、70个Batch、M2及参考数据回归；旧Batch外部快照因当前仓库未包含而明确SKIP | 本轮终端记录 |
| M2模型与CLI | 82个软件用例PASS | 同上 |
| MATLAB M2参考包 | 7组Manifest/文件大小/SHA-256及`--validate-m2` PASS | tests/test_m2_references.py |
| 可变长度、页、地址窗口/溢出、64线sysfs映射 | PASS | 同上 |
| 实际GPIO-v1 API适配层（仅替换系统调用） | PASS：32+32偏移、输入handle复用、SET/GET读回、申请失败及释放 | 同上 |
| QEMU AArch64 | 41个原CLI、82个M2软件用例、GPIO API测试PASS | build/m2/qemu-*.txt |
| ASan+UBSan | M2模型、GPIO API、布局/映射测试PASS | build/m2/asan-*.txt |
| 静态交叉编译 | PASS，现有/usr/bin/aarch64-linux-gnu-gcc，GCC9.4.0 | build/m2/build.txt、toolchain.txt |
| file/readelf -h/-l/-d/-n | AArch64 ELF64 LSB EXEC、静态、无PT_INTERP/动态依赖、Linux ABI3.7.0 | build/m2/elf-check.txt、ELF-notes.txt |
| 原FFT路径及参考文件保护 | PASS | build/m2/preservation.txt |
| M2实板运行/数值比较 | NOT_RUN | 无板端执行记录 |

ASan使用detect_leaks=0以避开本环境已知LSan/ptrace不兼容，不宣称完成泄漏检测。
软件模型检查：初始旧sticky位、初始error清理、初始busy拒绝、两阶段start及新完成、只有一次clear_error，
load仅MM2S、process先S2MM、每包N×4地址/长度、RNE正负ties/33位累加/饱和、
TLAST/TKEEP错误码、DMA错误/超时、M2完成超时、Switch提交/读回失败、取消、输出错误与异常隔离。
GPIO API专项测试覆盖了先申请输入、再申请控制时不可重复申请输入造成EBUSY的生命周期问题。
这些都是软件验证，不宣称真实TKEEP/TLAST波形或M2板级算术通过。

实际构建：`make CROSS_COMPILE=aarch64-linux-gnu-`，等效编译命令：

```sh
aarch64-linux-gnu-gcc -Iinclude -D_FILE_OFFSET_BITS=64 -O2 -g \
  -std=c11 -Wall -Wextra -Werror -Wformat=2 \
  src/m2.c src/m2_gpio.c src/axis_switch.c src/batch_json.c src/batch_manifest.c \
  src/batch.c src/main.c src/common.c src/dma_mmio.c src/udmabuf.c src/gpio_ctrl.c \
  -static -o build/ffttest
```

产物：build/ffttest；校验值：build/m2/ELF-SHA256.txt。
软件差异审阅文件：build/m2/changes.diff；本轮没有部署或运行实板DMA。
