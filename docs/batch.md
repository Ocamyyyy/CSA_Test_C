# FFTTest 最小 Batch 软件实现

2026-11-12。用户已报告PCIE715上的2048点impulse、RADARSAT-1单帧及MATLAB数值比较通过。
当前改动只完成软件Batch实现与软件验证；**多帧板级验证NOT_RUN**。
本地references是此前复制的快照，不代表Windows最新工程。本任务未操作开发板、FPGA、MATLAB或eth0 PHY。

## 接口与门禁

```sh
./ffttest --run-batch --confirm-fresh-reset --mode 2048 --frames 2 \
  --input fft_batch_input.bin --output fft_batch_output.bin \
  --contract hardware_contract.json --manifest batch_manifest.json \
  --stats batch_stats.jsonl --timeout-ms 2000 --log batch-run-001.log
```

支持帧数2、16、64，每帧8192 Byte，原始BIN按帧连续存放，输入/输出无帧头、填充或用户态FFT变换。
现有--run/--inspect/--validate保留原有路径；--frames和--stats只允许用于--run-batch。
--validate仍是单帧校验，不会被改为隐式执行Batch。
Batch开始前执行完整参数、契约、manifest、输入文件长度校验，失败不会打开硬件设备。
输出、统计、日志都要求新文件；已有输入/单帧结果不会被覆盖。
启动阶段设备检查失败可能留下空输出和FAIL汇总，不属于成功结果。

**使用板端已核实并实际通过单帧的契约，不得将本地候选JSON仅改status冒充核实。**
Batch复用validate_contract全部VERIFIED/地址域/独占/GPIO/部署确认检查；
`--confirm-fresh-reset`为旧命令兼容参数。新版GPIO函数在Batch开始时执行一次可重复握手：请求保持高等待
done清零，释放请求后等待新done置位；Batch帧间仍不重复配置FFT。
新CLI保留生产AArch64执行限制；没有环境变量或测试模式可在生产ELF绕过门禁。

## batch_manifest.json

示例位于`examples/batch_manifest.json`，它是模板，不是Windows当前导出结果。
Batch manifest使用独立的有界标准JSON解析器，支持数组、布尔值、浮点/科学计数法及转义字符串；
硬件契约和单帧manifest仍使用原严格扁平解析器。schema必须`ffttest-batch-v1`。
MATLAB原生字段及身份分离规则见[manifest_compat.md](manifest_compat.md)。
必需字段：

- `frame_count`：与--frames一致，2/16/64之一。
- `frame_bytes`：整数8192。
- 原单帧共同字段：contract_id、nfft=2048、bytes_per_sample=4、byte_order=little、
  iq_order=I-low、encoding=signed-int16、direction=forward、output_order=natural、
  config_word="0x0D550B"、scaling、quantization。

共同字段须与**同一份板端硬件契约**完全一致，实际GPIO编码必须mode_sel=1。
旧格式只在内存中适配schema；MATLAB格式在核对显式FFT参数后映射到相同校验器，
不改文件、不放宽任何契约条件。数据集quantization与XFFT内部量化说明分开处理。
输入必须是普通文件，总长严格为：2帧16384、16帧131072、64帧524288 Byte。
校验后保持同一个只读文件描述符，逐帧完整读取；遇短读/错误即停，最终还检查EOF，拒绝尾部新增数据。
运行期间不要从其他进程修改输入；长度校验不能证明文件内容没有被外部等长替换，部署时需配合SHA-256。
Windows的fft_batch_reference.mat保留在Windows，不由Linux解析或当作BIN输入。

## 控制流程

一次设备检查/锁定/映射，一次DMA初始化复位，一次GPIO配置；始终复用同一对udmabuf区域。

```text
初始化 DMA、配置 XFFT 一次
for 每帧:
    读8192 Byte → 写输入/清输出
    输入TO_DEVICE、输出BIDIRECTIONAL交给device
    校验通道状态 → 仅W1C清IOC并读回
    S2MM地址+LENGTH8192 → MM2S地址+LENGTH8192
    双方新的IOC+Idle、无错误、实际接收8192
    两区域归还CPU → 原样读取输出 → 完整写入8192并fsync
    写入并同步本帧JSONL统计
最终EOF检查 → 资源清理 → Batch汇总
```

首帧刚复位，允许Halted=1/Idle=0，且要求RS=0；不能错误地要求复位后立刻Idle=1。
后续帧必须双方IOC+Idle、非Halted、RS=1且无错误/SG。
错误在任何W1C之前检查；仅向偏移0x04、0x34各写0x1000，不同时清ErrIRQ。
读回确认IOC清零及无错误，再复用原dma_transfer()，要求本帧重新出现IOC+Idle。
该函数仍幂等写RS=1，正常帧间不翻转RS、不Reset；地址及两个LENGTH每帧重写。
S2MM的LENGTH总是先于MM2S的LENGTH提交。BTT限制按每次8192计算，不将整个Batch写入LENGTH。

config_req在首次配置完成后保持0；done为高不会阻挡数据通道，不逐帧重配。下一次启动新Batch时，新版握手
会明确观察done清零和重新置位，无需PL全局复位。
任意一帧失败都停止提交，保留复位前快照，做一次有界DMA控制器清理，不重试、不重配FPGA。
只有证明DMA已停止才归还CPU所有权；否则报告QUARANTINE，不读取可能仍被写入的输出。

## 输出和错误语义

输出按0基frame_index顺序连续写入。每帧完整写入并fsync后才可计成功，短写和EINTR受处理。
失败帧若已有部分输出，尝试ftruncate回已完成帧前缀，保留之前完整结果；不补零、不标整批成功。
如果截断/文件同步也失败，返回非零，日志明确要求检查实际长度；summary记录actual_output_bytes。
已完成帧数只表示输出和该帧统计都已成功处理；统计写入失败也会停止并回退当前输出帧。
JSONL行也尝试回退部分写入；持续磁盘错误可能使FAIL行或summary无法保存，此时退出码/stdout/run-log为依据。
全局初始化/收尾失败没有特定帧时failed_frame_index=null；帧内失败记录实际0基帧号。

## 时间统计

每帧JSONL：type=frame，frame_index，input_offset/output_offset，rx_bytes/rx_bytes_valid，
dma_us，frame_us，mm2s_sr/s2mm_sr（数值寄存器），status=PASS/FAIL，phase。
未确认DMA完成时rx_bytes=0且rx_bytes_valid=false，不能把仍为8192的接收容量寄存器误当实际收包长度。

- dma_us：从本帧状态检查/IOC清除前，到DMA提交与双通道完成检查返回；不含缓存同步/文件I/O。
- frame_us：从读取输入前到输出写入/fsync或失败结束；包含缓存同步和DMA，不含随后统计记录的写入。
- batch_total_us：从Batch执行初始化前到资源/输出收尾后，计算summary前；包含逐帧统计I/O，
  不包含之前的参数/契约文件校验，也不包含summary自身写入/关闭。
- average_frame_us：成功帧frame_us的算术平均，无成功帧为0。
- output_bytes_per_second：成功帧数×8192÷batch_total_us，换算为Byte/s；只算输出有效负载，
  不把输入/输出双向字节合并，也不表示纯DMA带宽。

所有阶段用CLOCK_MONOTONIC微秒计时。每帧fsync和详细日志会影响吞吐量；
这些是软件处理/提交轮询耗时，**不是FPGA FFT核计算延迟**。
summary还包含requested_frames、successful_frames、failed_frame_index、actual_output_bytes、status及phase。
最终退出码非零始终表示失败，即使已有部分帧的PASS记录。

## 部署与首个2帧测试

1. 在Ubuntu执行make、make test、make elf-check，核对`build/batch/ELF-SHA256.txt`。
2. 人工复制`build/ffttest`到板端新文件名`ffttest-batch`，保留此前已通过的单帧ELF及结果。
   同时复制Windows当前导出的fft_batch_input.bin、batch_manifest.json，使用板端已核实的契约。
   不用本地旧候选契约替换已核实版本。
3. 使用--help、--inspect确认平台/绑定/缓冲区/GPIO，检查输入16384 Byte和SHA。
4. 人工建立并确认XFFT/wrapper fresh-reset且无旧pending。以下命令仅供操作员执行，本任务未执行：

```sh
sudo ./ffttest-batch --run-batch --confirm-fresh-reset --mode 2048 --frames 2 \
  --input fft_batch_input.bin --output fft_batch_output-2-001.bin \
  --contract hardware_contract.json --manifest batch_manifest.json \
  --stats batch_stats-2-001.jsonl --timeout-ms 2000 --log batch-run-2-001.log
printf 'exit=%s\n' "$?"
wc -c fft_batch_input.bin fft_batch_output-2-001.bin
sha256sum ffttest-batch fft_batch_input.bin fft_batch_output-2-001.bin \
  hardware_contract.json batch_manifest.json batch_stats-2-001.jsonl batch-run-2-001.log
```

5. 首测两帧采用明显不同的数据；确认仅一次配置、第二帧新IOC、两帧各8192、summary完成2帧、输出16384。
   回传Windows逐帧MATLAB比较，排除重复首帧、交换顺序、旧缓存和TLAST跨帧污染。
6. 两帧实测通过后再测试16、64；不靠增加timeout掩盖错误。收集stdout/退出码/JSONL/前后inspect和所有SHA。

同一次配置多帧尚未上板；软件mock的输出是echo输入，仅检验控制/文件/缓存交接顺序，不检验FFT数值。
既有eth0 PHY问题保留，本任务不排查网络或接入调度器。

## 本轮软件验证结果

以下均为软件验证，不是板级FFT结果：

| 检查 | 结果 | 日志 |
|---|---|---|
| GCC9.4 AArch64静态构建 | PASS | build/batch/build.txt |
| 原核心/GPIO/访问隔离、41个CLI回归 | PASS | build/batch/host-tests.txt |
| Batch主机自动测试 | 70个用例PASS | 同上 |
| QEMU生产ELF的原CLI回归 | 41个PASS | build/batch/qemu-single-cli.txt |
| QEMU AArch64 Batch echo模型 | 70个PASS | build/batch/qemu-batch-tests.txt |
| Batch ASan+UBSan | 70个用例PASS | build/batch/sanitizer-batch-tests.txt |
| 原核心/GPIO ASan+UBSan | PASS | build/batch/sanitizer-core.txt、sanitizer-gpio.txt |
| file/readelf | ELF64 LSB AArch64静态EXEC，无动态解释器/动态段 | build/batch/elf-check.txt |
| 单帧路径源码保留核对 | PASS | build/batch/single-path-preservation.txt |
| 原参考包校验 | 清单内全部通过；新增实板文件单独列出为未由旧清单覆盖 | build/batch/reference-check.txt |
| Batch真实板级测试 | NOT_RUN | 无板端运行记录 |

测试覆盖2/16/64总长度、一次配置/一次初始复位/多次提交、每帧地址和长度、RX先于TX、
旧IOC清除读回失败、错误状态在清IOC前拒绝、非Idle拒绝、超时、短帧、信号取消、同步失败、
首帧/第二帧失败立即停止、不同帧输出顺序、短写/部分写入/fsync/统计失败及完整帧前缀保留、
已有结果/输入保护、manifest参数/契约门禁、缺少fresh-reset和非法CLI参数。
模型时钟用于DMA超时测试，避免宿主调度导致偶发时间测试失败；性能数值不用于评价硬件。
ASan使用ASAN_OPTIONS=detect_leaks=0，原因是当前环境LeakSanitizer与ptrace不兼容；不宣称完成泄漏检测。

对变更前快照的逐段比较确认：单帧run/inspect/validate主体、原DMA实现、GPIO、udmabuf、
原契约/单帧输入校验器保持原文；DMA文件仅追加Batch辅助函数。
源码及文档差异见build/batch/changes.diff；ELF校验值见build/batch/ELF-SHA256.txt。

FFT_BATCH_SOFTWARE_TEST_PASS=1

FFT_BATCH_BOARD_TEST_PASS=NOT_RUN
