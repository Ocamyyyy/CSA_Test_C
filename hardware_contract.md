# FFTTest 硬件契约：第二阶段审计

2026-11-11。`VERIFIED`在本文件表示参考源码/配置的静态事实；不表示板端实测PASS。
`hardware_contract.json`已填写这些静态事实，但整体status仍为`UNVERIFIED`，
`FFTTEST_USERSPACE_READY_FOR_BOARD_TEST=0`：板端GPIO映射、DMA地址域/独占及部署身份仍需证据。
程序继续拒绝当前契约的真实运行，没有为解锁把未知值改成true。

## 参考包完整性和版本

- 实际文件树、完整清单及补充缺失项见 [docs/reference_audit.md](docs/reference_audit.md)。
- `references/SHA256SUMS.txt`的12项全部通过；MANIFEST还列出11个payload的大小/哈希，逐项相符。
- BD `design_info.tool_version=2018.3`，四个XCI `RUNTIME_PARAM.SWVERSION=2018.3`。
  XCI Component_Name与BD xci_name一致，BD中所有显式IP参数均与XCI一致。
- MANIFEST原始路径均指向 `D:\Workspace\SARTest\FFTTest`（硬件）和`FFTValidation`（MATLAB）。
  包内未发现互相冲突的版本；这不能独立证明板上当前bitstream就是这一版。
- 设计器件标识为`xc7z100ffg900-2`、`processing_system7:5.5`，与四个XCI的zynq标签一致。
  不能把这个标签当作AArch64证明；JFMQL兼容实现与板载镜像的对应关系需板方确认，
  不据此把已知AArch64 ELF改成ARM32。

## 已核实的FFT数据契约

依据 `references/matlab/fft_config.md`、`generate_test_data.m`、`xfft.xci`及`fft_config_ctrl.v`：

| 参数 | VERIFIED值 | 直接证据 |
|---|---|---|
| 运行点数、方向 | 2048 Forward | RTL nfft=11、fwd_inv=active_mode[0] |
| mode_sel | `2'b01`（数值1） | RTL active_mode[1]=0、[0]=1 |
| config_tdata | `24'h0D550B` | `(0x6AA<<9) | (1<<8) | 11` |
| XFFT架构 | Pipelined Streaming I/O、Non-Realtime | XCI implementation_options、throttle_scheme |
| 最大配置长度 | 4096，允许运行时2048 | XCI transform_length=4096、HAS_NFFT=1；不是当前帧4096 |
| 分量 | signed int16 I/Q | XCI INPUT_WIDTH=16、OUTPUT_WIDTH=16；MATLAB打包 |
| 32-bit布局 | [31:16]=Q，[15:0]=I | MATLAB write_complex_words，PG109复数通道布局 |
| 文件端序 | little，I低字节/I高字节/Q低字节/Q高字节 | MATLAB `ieee-le`、uint32写入 |
| 输出顺序 | Natural Order | XCI output_ordering=natural_order |
| 缩放 | 0x6AA，总右移11位，理想增益1/2048 | RTL及MATLAB；两位分组由低到高2,2,2,2,2,1 |
| 量化 | XFFT内部convergent_rounding，scaled signed16 | XCI rounding_modes、HAS_SCALING=1、OUTPUT_WIDTH=16 |
| 外部变换 | 无用户RTL量化/截断/饱和/缩放模块 | 数据AXIS两条直连；C只搬运原始字节 |
| 溢出遥测 | 未启用OVFLO/BFP；无饱和保证 | XCI C_HAS_OVFLO=0、C_HAS_BFP=0；不能由DMA完成推断数值正确 |
| 每帧 | 输入/输出均8192 Byte，2048个32-bit有效采样 | 原始文件大小+上述协议 |

未发现与用户给定MATLAB表的冲突。双精度`fft(x)/2048`不是位精确XFFT模型，
不能凭内部舍入方式就伪造expected.bin或宣称误差为零。
三个输入保持原文件，不在用户态执行FFT、缩放、重排或I/Q转换。

## GPIO连接和Linux映射

`axi_gpio.xci`：C_IS_DUAL=1，C_GPIO_WIDTH=3、C_ALL_OUTPUTS=1，
C_GPIO2_WIDTH=1、C_ALL_INPUTS_2=1；输出复位默认0。
BD `axi_gpio_0_gpio_io_o`同时连接两个slice：slice_mode取[1:0]，slice_req取[2]；
分别接RTL mode_sel、config_req，RTL done直接接gpio2_io_i。

| 信号 | 硬件位 | 上游Linux5.4 gpio-xilinx预期chip-relative offset |
|---|---|---|
| mode_sel[0] | ch1[0] | 0 |
| mode_sel[1] | ch1[1] | 1 |
| config_req | ch1[2] | 2 |
| config_done | ch2[0] | 3（不是32） |

上游驱动按实际通道宽度拼接：`xgpio_offset()`对第二通道减gpio_width[0]，
ngpio=width1+width2。依据：[Linux v5.4 gpio-xilinx.c](https://raw.githubusercontent.com/torvalds/linux/v5.4/drivers/gpio/gpio-xilinx.c)。
**目标5.4.52厂商树/实际DT尚未提供，实际映射为UNVERIFIED。**
程序保留动态chip发现，新增运行时检查绑定driver为gpio-xilinx、DT宽度3+1/is-dual=1、ngpio=4；
不符即拒绝配置，inspect只报告，不请求线路。实际gpiochip编号及sysfs全局base从运行系统发现，
不会固定gpiochip508，也不会直接MMIO访问GPIO。

## RTL握手：上升沿请求、sticky done

直接依据 `fft_config_ctrl.v:36–104`：

1. `req_rising=config_req & ~req_d`，不是持续高电平反复触发。
2. reset清req_d/pending/done为0，active_mode为00；上电**复位之后**done为0。
   若bootloader或其他进程已请求过，进入本程序时done可以为1。
3. pending=0且检测到上升沿时锁存mode、pending=1、done=0。
4. pending=1时只等待config_tready；pending就是config_tvalid，握手成立后done=1、pending=0。
   pending期间的新请求边沿被忽略。
5. 拉低config_req不会清done；高电平一直保持到新请求边沿或全局复位。
6. 若tready一直为1，重新请求时done=0可能只持续一个20ns时钟，用户态轮询不保证看见。

旧“req低→等done低→req高→等done高→req低→等done低”已删除。
安全实现限定**fresh global reset后、没有此前配置请求**的单帧配置：

- `--run --confirm-fresh-reset`是操作员对该前提的明确声明，不执行任何全局复位。
- 设置mode=01、req=0，保持setup_us（软件保守余量默认契约1µs=50个50MHz周期）。
- 读取初始done；为1立即拒绝，不发新边沿，不把旧完成当新完成。
- 初始0且不存在旧pending（由fresh-reset前提保证）时置req=1，有界等待done=1。
- 成功或等待失败均尝试置req=0；成功后不等待done清零，额外等待setup_us再允许DMA。
- PG109要求配置接受至少领先首拍数据一个周期；GPIO读取/显式余量及后续同步/寄存器写入满足。
  TREADY仍控制数据拍握手，不用猜测FFT内部固定延迟。

限制：初始done=0本身也不能排除旧pending；软件无法读取pending，故不能用一次采样证明新请求归属。
同一上电周期连续三次独立`--run`没有可靠可复用握手；旧done高也不等于模式仍是本程序所需模式。
不新增跳过配置/忽略done/自动重试选项，不靠延长timeout掩盖。
若需要不复位连续运行，请硬件负责人增加可明确清除且可保持的ack或请求/完成toggle/计数器；
这里只报告建议，不修改RTL。当前保守路径要求每组数据前人工建立fresh-reset状态。
DMA软件Reset只复位DMA，**不能清XFFT或wrapper done**。

## AXIS与TLAST

- BD interface_nets：`axi_dma_0/M_AXIS_MM2S ↔ xfft_0/S_AXIS_DATA`，
  `xfft_0/M_AXIS_DATA ↔ axi_dma_0/S_AXIS_S2MM`，无额外AXIS FIFO/Adapter。
  axi_smc及AXI interconnect位于MM内存/控制路径，不是数据AXIS中间模块。
- 两个IP四个数据接口XCI均HAS_TLAST=1、HAS_TREADY=1、TDATA_NUM_BYTES=4。
  因为是完整接口连接，TLAST包含在该连接中，不需要另一个标量net。
- MM2S LENGTH=8192应在第2048次有效数据握手置TLAST；不是按无TREADY的时钟计数。
- XFFT输出TLAST由当前NFFT的帧计数产生，不是组合透传输入TLAST。
  接受NFFT=11且完整输入一帧时，应在第2048个有效输出采样产生TLAST并送S2MM。
  输入TLAST用于检查帧界，与内部配置长度必须一致。
- config_tdata/tvalid/tready三条标量net完整接通wrapper与XFFT。
  wrapper等待ready期间done不置位，软件不会提前发数据；无额外配置流master或固定ready常量。
- 50MHz时钟共域；proc_sys_reset peripheral_aresetn同时驱动DMA/XFFT/GPIO/wrapper，
  FCLK_RESET0_N→ext_reset_in且XCI C_EXT_RESET_HIGH=0，未发现复位极性矛盾。
- XK_INDEX在TUSER启用但用户态不采集；状态/事件未导出为软件可读寄存器。
  无OVFLO/BFP故不依赖另一个状态包完成本次传输。

静态连接与IP协议预期为VERIFIED；**第2048拍的实际波形和板载bitstream为UNVERIFIED_RUNTIME**。
未发现“接收不到TLAST”的静态断路证据，不能反过来声称已测得正确TLAST。
S2MM超时须检查NFFT/配置、TVALID/TREADY、复位和TLAST，不能只增加超时。

## AXI DMA Simple Mode

`axi_dma.xci`与BD一致：MM2S=1、S2MM=1、SG=0、Micro=0、两方向DRE=0，
AXI MM/Stream数据均32位、地址32位、burst size=16，Store-and-Forward启用。
**实际C_SG_LENGTH_WIDTH/c_sg_length_width=15**，不是此前提到的14；合法BTT为1..32767，8192合法。
本轮只支持2048，不据此修改4096实现。
无DRE要求至少4-byte地址对齐，8192是4的倍数；DMA映射通过HP0覆盖[0,0x40000000)，
因此新增1GiB窗口校验；仅检查32-bit地址宽度不够，0x40000000以上会越过该BD映射。
`0x30000000`只是已报告板端地址，程序始终从sysfs读取再加offset。

| 寄存器 | MM2S偏移 | S2MM偏移 |
|---|---|---|
| DMACR | 0x00 | 0x30 |
| DMASR | 0x04 | 0x34 |
| 地址低32位 | 0x18 | 0x48 |
| 地址高位（本实现32位地址不写） | 0x1c | 0x4c |
| LENGTH/BTT | 0x28 | 0x58 |

基址0x40400000。CR RS=bit0、Reset=bit2；SR Halted=bit0、Idle=bit1、SGIncld=bit3，
DMAInt/Slv/DecErr=bit4/5/6，SGInt/Slv/DecErr=bit8/9/10，IOC/Delay/ErrIRQ=bit12/13/14。
已报告初值CR=0x00010002、SR=1意味着RS=0、Reset=0、Halted=1、Idle=0、无错误；
CR其余保留/实现相关位不用于推断配置正确，SG等实际参数以XCI为准。

执行先确认无内核绑定且两通道静止，DMA软件复位并等Reset清零，W1C旧IRQ；
再配置FFT、同步数据，S2MM RS=1→等Halted=0→写DA→最后写8192 LENGTH，
然后MM2S RS=1→等Halted=0→写SA→最后写8192 LENGTH。
不使能中断；仍轮询IOC（不依赖IRQ使能），要求两通道IOC+Idle、无错误/意外Halted，
并检查S2MM实际接收长度=8192。先接收后发送不能代替TLAST验证。
所有等待有界，失败打印复位前后完整快照，只做一次DMA控制器清理复位，不重试传输。

## u-dma-buf审计

继续复用已保存的上游4.5.3源码 `docs/u-dma-buf-v4.5.3.c`：
307–344行CPU/device sync调用，380–417行sysfs ABI，668–690及765行mmap属性选择。
O_RDWR|O_SYNC + MAP_SHARED、offset0正确；sync_mode=1选noncached页属性，
不是mmap读写自动完成全部CPU/device所有权同步。
输入offset0，输出向上页对齐且至少间隔一页；目标4KiB页/4byte对齐时输出offset12288，
范围[0,8192)和[12288,20480)互不重叠，整体页对齐映射5页。
输入TO_DEVICE，输出先清零故BIDIRECTIONAL，再分别交给device；完成后分别归还CPU。
映射区只做对齐volatile32位访问，文件I/O经普通内存中转，不会变换I/Q字节。
无硬编码物理地址或在用户态私自改页属性，要求实际driver_version=4.5.3、sync_owner=CPU。

失败只有确认DMA已停止后才归还CPU；清理复位超时则保留device owner、报告隔离要求，
不对可能仍被DMA写的缓存做CPU同步。munmap/close不释放driver分配的物理缓冲区，
flock在close后释放，其他进程仍需遵守隔离告警；该程序不能强制隔离其他进程。
共享sysfs同步属性需要独占，flock只是协作锁；真正的地址域一致性/无IOMMU错配仍需板端确认。

## 尚未闭合的执行阻断项

| 条目 | 状态/需收集证据 |
|---|---|
| 目标实际GPIO映射 | UNVERIFIED；inspect的driver/DT widths/ngpio/line info，厂商驱动若不同须给源码 |
| udmabuf总线地址域一致性 | UNVERIFIED；运行时phys_addr/size、DT保留区/映射/IOMMU配置，确认同一DMA地址域 |
| 独占DMA/整个udmabuf/配置wrapper | UNVERIFIED；板端确认无其他消费者；内核DMA未绑定还会再检查 |
| 参考版与已装载bitstream一致、厂商器件/PS兼容关系 | UNVERIFIED；板方部署版本/生成记录；不能仅以fpga_manager operating证明 |
| fresh-reset且无旧pending | 每次run的前置条件；人工建立并声明，旧done高时软件仍拒绝 |
| 不复位连续重配 | 当前RTL协议的限制；当前软件不支持，不承诺三次run可在同一周期完成 |

原包没有`fft_test_manifest.json`，根目录提供**据MATLAB文档派生**的协议JSON，明确provenance，
不是伪称Windows原文件；若Windows后续提供JSON须逐项核对。
引用的`tb_fft_2048_radarsat1.v`、仿真/ILA结果、matlab_reference.mat也未包含；
它们不是哈希包漏传（payload已齐），但不能声称已经核查其内容/实测行为。
板端步骤和解除门禁所需证据见 [docs/board_test_plan.md](docs/board_test_plan.md)。

## 规范来源（仅用于IP协议，不替代2018.3 XCI）

- [AXI DMA PG021](https://www.amd.com/content/dam/xilinx/support/documents/ip_documentation/axi_dma/v7_1/pg021_axi_dma.pdf)
- [PG021 Simple Mode顺序](https://docs.amd.com/r/en-US/pg021_axi_dma/Direct-Register-Mode-Simple-DMA)
- [XFFT PG109 v9.1（2022版）](https://www.amd.com/content/dam/xilinx/support/documents/ip_documentation/xfft/v9_1/pg109-xfft.pdf)
- [配置先于数据](https://docs.amd.com/r/en-US/pg109-xfft/Applying-a-New-Configuration-While-Idle)
- [XFFT输出TLAST定义](https://docs.amd.com/r/en-US/pg109-xfft/Pinout?contentId=Oo0GV0CRIzVYlj~SvCrJTg)
- [u-dma-buf 4.5.3原始源码](https://raw.githubusercontent.com/ikwzm/udmabuf/v4.5.3/u-dma-buf.c)
# 新版配置握手覆盖说明（2026-11-25）

本文件后文记录的是仓库中旧`references/fft`快照的审计历史。当前软件目标采用后续已提供并核实的新版
`fft_config_ctrl.v`协议：CPU保持`config_req=1`直至观察`config_done=0`，再释放请求并等待新的
`config_done=1`。因此后文“旧done高必须全局复位”的结论不再适用于新版部署。软件不会据此自动认定板端
bitstream身份；仍须在`hardware_contract.json`/`fft_modes_hardware_contract.json`中记录当前部署证据并通过门禁。

