# references只读审计

2026-11-11。本轮未向references写入任何文件，也未调用Vivado/MATLAB或运行硬件。

## 实际目录

```text
references/
├── MANIFEST.md
├── SHA256SUMS.txt
├── hardware/
│   ├── axi_dma.xci
│   ├── axi_gpio.xci
│   ├── design_1.bd
│   ├── fft_config_ctrl.v
│   ├── proc_sys_reset.xci
│   └── xfft.xci
├── matlab/
│   ├── fft_config.md
│   └── generate_test_data.m
└── vectors/
    ├── impulse/fft_input.bin
    ├── tone/fft_input.bin
    └── random/fft_input.bin
```

清单中11项payload齐全，另有MANIFEST和SHA256SUMS共13文件。
`cd references && sha256sum -c SHA256SUMS.txt` 的12项全部OK；
MANIFEST逐项记录的字节数及SHA-256也全部相符，没有未被校验表覆盖的额外payload。

| 输入 | Byte | SHA-256 |
|---|---:|---|
| impulse | 8192 | 49e81a27be2fd1344ef7cea91044ada40db62d1a843e06251aedadb890f331b4 |
| tone | 8192 | 83db91178f2f3585292e0e70b70d19e36e7abd18df1cfaff3c4818600502f11b |
| random | 8192 | 3e27b5d49b908c8859faf29051151aafa92aec2f5d6594aab5a93fa857e725ab |

## 来源与版本一致性

MANIFEST所列原始路径为FFTTest.srcs中的BD/RTL/XCI及FFTValidation中的MATLAB文件。
不能在Ubuntu独立访问原Windows路径，但可验证包内哈希与路径声明、组件名和参数的相互一致性。
四个XCI instanceName/Component_Name与BD引用的design_1组件完全一致，RTL模块名fft_config_ctrl
与BD module_ref一致。全部XCI的工具版本2018.3、器件xc7z100/ffg900/-2，与BD一致。
BD显式参数全部与对应XCI相符，复位拓扑与MANIFEST说明一致，未发现包内版本冲突。

需要明确区分的事实：

- 实际BTT=15；此前14只是待核实说法，与当前包不符，应采用包内15，并记录差异。
- XFFT transform_length=4096是最大长度，HAS_NFFT及运行时可配置长度均启用，
  RTL mode01写入NFFT11，所以不与本轮2048冲突；不修改4096。
- MANIFEST中的“pulse req, poll done”省略了sticky done的歧义；RTL才是握手的直接依据。
  拉低req不会清done，复用上一轮高电平不安全。
- BD/XCI中的zynq/PS7器件标记不能独立证明厂商AArch64芯片的兼容关系或板载bitstream版本，
  该对应关系仍需板方资料，不能无证据改成ARM32或断言必然不兼容。

## 补充资料缺失（不是清单payload校验失败）

1. fft_config.md引用的`tb_fft_2048_radarsat1.v`未包含；无法审查其中188–199行或运行该仿真。
2. 无综合网表、生成wrapper HDL、implementation结果、bitstream身份记录、ILA或仿真波形。
   BD整接口连线+XCI HAS_TLAST可确认静态TLAST连接，不能代替实际第2048拍波形。
3. 无原始`fft_test_manifest.json`；MATLAB生成器也不输出这个JSON。
   根目录JSON明确标注由配置说明派生，不假冒Windows导出文件。
4. 无matlab_reference.mat或MATLAB比对脚本；generator中虽生成.mat，但它们不在本包。
   比对资料应从Windows已有工程获得；expected.bin有意不提供，不应伪造。
5. 无目标live DT、厂商gpio-xilinx源代码、IOMMU/总线地址域或独占消费者清单。

## 可复现审计

```sh
(cd references && sha256sum -c SHA256SUMS.txt)
python3 tests/test_references.py
```

测试只读这些文件，检查版本/参数、复位和AXIS网络、配置字、输入长度及int16打包范围。
没有运行FFT、生成输入文件或修改MATLAB数据。
完整执行输出在`build/stage2/reference-audit.txt`及`reference-sha256-before.txt`/`after.txt`。
