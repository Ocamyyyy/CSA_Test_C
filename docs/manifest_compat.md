# Batch MATLAB manifest JSON兼容修复

本次仅修复Ubuntu软件，不访问板卡、不运行DMA、不修改references/、MATLAB BIN/MAT或硬件。
本地导入数据是参考快照，不等于Windows当前最新工程。

## 已定位的原因

真实文件位于references/vectors/batch_2、batch_16、batch_64/batch_manifest.json。
三个文件均可由Python标准json解析，是合法JSON；实际mode_sel和config_word都有双引号。

旧common.c解析器只支持扁平字符串、boolean、无符号整数：

- 第一个失败字段selected_columns的`[`无法通过无符号整数解析。
- 删除数组后，scale_factor的小数点仍会被strtoull完整消费检查拒绝；fft_output_scale也同理。
- binary_readback_exact=true本来就支持，不是本次报错原因。
- 0b01/0x0D550B若写成**无引号字面量**并不是标准JSON；新解析器仍拒绝，不能把非标准语法自动修好。

单元测试隔离复现上述三种类型行为，另直接对三组原始manifest测试：旧解析器失败、新Batch解析器成功。
仅修复语法还不够：原文件contract_id是数据集身份；且MATLAB字段名/量化语义与旧Linux约定不同。

## Windows最小改动：只调整JSON身份字段

继续把contract_id定义为**硬件契约ID**，保持已有身份校验。原数据集ID移入显式dataset_id：

```json
"contract_id": "FFTTest-ref-3656ea4e7423-20261111-2048fwd",
"dataset_id": "radarsat1-fft2048-batch-common-scale-v1"
```

即把原contract_id的数据集值移到dataset_id，再填写板端实际硬件contract_id。
只需调整MATLAB导出JSON的这两个字段，或由Windows侧显式修订元数据JSON；
无需重新生成BIN/MAT、选择列、公共I/Q缩放或量化结果。
本修复没有修改任何原始manifest；测试仅在临时目录制作上述两字段修订副本。

**原文件仍会被明确拒绝为硬件ID不匹配。**程序不会自动替换ID、修改硬件契约或忽略身份。
MATLAB原生格式必须有非空dataset_id；旧Linux规范格式可继续不带dataset_id以保持兼容。
VERIFIED、部署、GPIO映射、地址域、独占和fresh-reset门禁全部保留。

## 类型支持与限制

只有Batch manifest改用batch_json.c。硬件契约/单帧仍用common.c原parse_json/load_json，
原有接受/拒绝规则和validate_contract完整保留。
Batch解析支持JSON字符串/转义/Unicode转义、boolean、整数、浮点/指数、数组、对象及null。
它会完整校验元数据语法，拒绝重复键（含转义后同名键）、非法数值、NaN/Infinity、尾随垃圾/逗号。
复合元数据内部空白仅在内存中去除，字符串内容和数组元素顺序不变；原文件不重写。

仍有资源边界：最大32768 Byte、每对象64键、键63 Byte、解码字符串或紧凑值511 Byte、深度16。
不能在C字符串接口中表示的嵌入NUL被拒绝，不声称支持无限大小JSON。
真实64帧的格式化selected_columns经内存紧凑保存后可容纳，无需Windows压缩或删去列号。

## 参数映射与严格校验

支持两种ffttest-batch-v1格式；旧Linux规范字段继续逐项匹配硬件契约。
检测到MATLAB的component_type/fft_direction/fft_output_order/fft_output_scale时，要求完整MATLAB字段集，
不会因缺失或冲突就退回宽松解析。

| MATLAB字段 | 校验/含义 |
|---|---|
| contract_id / dataset_id | 硬件身份严格匹配；数据集身份单独记录到日志 |
| frame_count/frame_bytes | 必须与CLI一致、8192；支持2/16/64 |
| total_bytes（提供时） | 必须等于frame_count×8192 |
| nfft | JSON整数2048，不能用2048.0或字符串代替 |
| mode_sel | 原生格式必需，字符串"0b01"/"01"或JSON整数1 |
| config_word | 字符串"0x0D550B" |
| component_type | "signed int16 I/Q" → signed-int16 |
| word_layout | "bits[31:16]=Q, bits[15:0]=I" → I-low |
| byte_order | "little-endian" → little |
| fft_direction/fft_output_order | forward/natural |
| fft_output_scale | 有限数值，必须精确表示1/2048，科学计数法可用 |
| frame_layout/sample_order | 与原生v1声明的逐帧连续、无头、自然输入样本顺序一致 |
| selected_columns（提供时） | 帧数个不同的正整数，保留原顺序；不接受浮点/boolean/重复列 |
| scale_factor（提供时） | 有限正JSON数，是数据集公共输入量化因子，不是FFT输出缩放 |
| binary_readback_exact（提供时） | 必须是boolean true，字符串"true"或false拒绝 |

MATLAB的quantization描述输入数据集共享I/Q量化，不等于XFFT内部舍入模式。
该说明仅作数据集元数据保留/记录，**不用于改变实际数据，也不拿它冒充硬件舍入声明**。
归一化请求中的硬件scaling/quantization取自经过完整VERIFIED校验的硬件契约；
显式config_word、mode、NFFT和fft_output_scale必须同时符合已知2048 Forward配置。
若提供额外fft_quantization或scaling硬件声明，必须与契约一致；
若同时提供direction/encoding/iq_order等旧字段，也要一致，冲突拒绝。

仍在任何硬件访问之前检查输入普通文件长度严格为frames×8192。
selected_columns/scale_factor不传入DMA数据变换路径；本轮未修改传输、缓存同步或GPIO执行代码。
input_sha256/reference_sha256作为元数据保留；程序未新增哈希库，部署仍用sha256sum外部核对。

## 回归和交付

- 原核心/GPIO/CLI/Batch/访问隔离/参考包测试全部继续执行。
- 新JSON单元测试覆盖数组、boolean、浮点/指数、转义、嵌套、重复键和有界随机畸形输入。
- 126个离线用例直接读取真实2/16/64帧manifest和BIN；原始数据集ID拒绝，
  只修订两身份字段的临时副本通过；覆盖FFT/帧/长度/列号/身份/VERIFIED负例。
- 新probe仅调用解析和batch_validate，不调用batch_execute，不访问硬件。
- 所有references文件的SHA-256在测试前后逐项比较，必须完全不变。

构建与测试：

```sh
make CROSS_COMPILE=aarch64-linux-gnu-
make test
make elf-check
```

详细结果位于build/manifest/。交付build/ffttest为AArch64静态ELF，部署前核对该目录ELF-SHA256.txt。
板端实际运行仍需操作者独立执行，本轮不作板级成功声明。

## 实际验证结果

| 检查 | 结果 | 记录 |
|---|---|---|
| 原核心/GPIO/访问隔离与41个CLI回归 | PASS | build/manifest/host-tests.txt |
| 原Batch软件用例 | 70个PASS | 同上 |
| 新JSON类型/语法/Unicode/边界测试 | PASS，另含20000组有界畸形输入 | 同上 |
| 真实2/16/64帧manifest离线回归 | 126个PASS | 同上 |
| QEMU AArch64 | 原41个CLI、70个Batch模型、126个离线metadata用例PASS | build/manifest/qemu-*.txt |
| ASan+UBSan | JSON单元、126个metadata和70个Batch模型用例PASS | build/manifest/*sanitizer* |
| AArch64静态ELF | PASS，无动态解释器/动态段 | build/manifest/elf-check.txt |
| 旧单帧及硬件代码/参考数据保护 | 原文及全部references哈希不变 | build/manifest/preservation.txt |

ASan按本环境限制设置detect_leaks=0，不宣称完成LeakSanitizer检测。

源码改动：新增src/batch_json.c和src/batch_manifest.c；src/batch.c仅修改入口元数据处理；
include/ffttest.h和Makefile增加接口/构建与测试目标。
新测试为tests/test_batch_json.c、tests/manifest_probe.c、tests/test_manifest_compat.py。
更新examples/batch_manifest.json和相关说明文档，未修改任何原始Windows参考或数据文件。

本轮未修改common.c原解析/校验器、main.c单帧CLI、DMA/GPIO/udmabuf实现和Batch逐帧执行路径。
所有测试为主机/模拟/QEMU或离线校验，未访问开发板，未运行真实DMA。

FFT_BATCH_MANIFEST_COMPAT_PASS=1
