> 2026-11-25：握手已升级为req高等待done低、req低等待新done高；新增连续模式测试见
> [fft_sequence.md](fft_sequence.md)。下文仍含旧阶段的历史措辞。
>
> 2026-11-12：新增Batch实现及软件验证结果见[batch.md](batch.md)。
> 原41个CLI场景保留，新增70个Batch软件用例通过；Batch板级测试NOT_RUN。
> 下文为第二阶段历史记录。

# 软件验证记录：第二阶段

日期2026-11-11；没有连接开发板，没有执行真实DMA。`FFTTEST_USERSPACE_READY_FOR_BOARD_TEST=0`。

## 结果

| 检查 | 结果 | 证据 |
|---|---|---|
| references SHA-256/大小/版本/参数/拓扑 | 12项哈希及11项payload全部一致，测试通过 | build/stage2/reference-audit.txt、reference-sha256-after.txt |
| AArch64 GCC9.4构建 | PASS，-Wall -Wextra -Werror -Wformat=2 | build/stage2/build.txt |
| ELF | ELF64 LSB AArch64静态EXEC，无PT_INTERP/DT_NEEDED，ABI3.7.0 | build/stage2/elf-check.txt、toolchain-report.txt |
| 主机核心C测试 | PASS | build/stage2/host-tests.txt |
| GPIO协议及sysfs路径模型 | PASS | 同上 |
| 主机CLI | 原35个继续通过，新增6个，共41个 | 同上 |
| inspect/validate隔离测试 | 测试provider下inspect只请求只读映射；41个CLI再次通过，validate不调用硬件provider | 同上 |
| QEMU AArch64 CLI | 41个通过，仅软件模式 | build/stage2/qemu-tests.txt |
| ASan+UBSan核心和GPIO模型 | PASS；关闭环境不支持的LeakSanitizer | build/stage2/sanitizer-core.txt、sanitizer-gpio.txt |
| 真实GPIO/DMA/缓存可见性/XFFT数值 | NOT RUN | 无板端数据，不标PASS |

核心测试继续覆盖JSON、输入打包、字节保真、大小/对齐/边界、BTT、32/64位地址、状态位、
模拟复位、先收后发、双通道IOC+Idle、错误、超时、短帧、Halted、取消及复位失败。
新增1GiB DMA窗口边界及DT控制器名称匹配，保证不把控制器下的dma-channel子目录重复计数。

GPIO模型依据新版可重复请求/done行为，测试首次低基线、旧高被本次请求清除、连续请求、
延迟ready、超时拉低req、读写错误、等待/请求前中断。
使用模拟时钟，避免调度延迟使几毫秒的单元测试偶发失败；不据此验证真实总线时序。
假sysfs目录验证了chardev ID与sysfs全局base不同、symlink/平台父目录发现、driver或宽度不符拒绝。

新增CLI门禁覆盖：无fresh-reset声明、非法DMA窗口、旧握手profile、未确认目标GPIO映射、
未确认部署身份，以及候选契约即便附fresh-reset声明也仍拒绝run。
所有可通过的VERIFIED测试契约均为临时合成fixture，不是板端证据。

## 本轮源码最小修改

- `gpio_ctrl.c`：请求保持高等待done回零，释放后等待新done置位；覆盖TREADY延迟并在失败时拉低请求。
  动态发现沿sysfs真实父路径定位平台，验证gpio-xilinx+DT3/1/dual+4条线，不混淆两类chip编号。
- `common.c`、`ffttest.h`：新握手profile、目标映射/部署确认门禁、DMA可达窗口；
  为握手测试增加I/O及时钟注入接口。原FFT数据通路和打包协议保持原样。
- `main.c`：`--confirm-fresh-reset`、live GPIO映射与DMA窗口校验，inspect遇未确认映射返回失败。
- `dma_mmio.c`：取消信号在启动前及准备RX后检查；只按节点basename识别DT控制器，
  避免完整路径substring重复统计子节点。Simple Mode启动/完成流程保持原有规范顺序。
- `udmabuf.c`：本轮未改变；4.5.3同步与失败清理审计通过静态检查，未替代板端实测。
- Makefile/测试：新增GPIO、模式访问隔离、只读参考包测试；原35个CLI场景仍保留。
- 新候选JSON、派生协议manifest及审计/部署文档；references内容完全未修改。

变更前相关文件存于`build/stage2/before.tar.gz`，文本差异为`build/stage2/changes.diff`。

## 复现

```sh
make CROSS_COMPILE=aarch64-linux-gnu-
make test
make elf-check
FFTTEST_COMMAND='qemu-aarch64-static build/ffttest' python3 tests/test_cli.py
```

```sh
gcc -Iinclude -D_FILE_OFFSET_BITS=64 -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined tests/test_core.c src/common.c src/dma_mmio.c src/udmabuf.c \
  -o build/stage2/test_core-sanitized
ASAN_OPTIONS=detect_leaks=0 build/stage2/test_core-sanitized
gcc -Iinclude -D_FILE_OFFSET_BITS=64 -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined tests/test_gpio.c src/common.c src/gpio_ctrl.c \
  -o build/stage2/test_gpio-sanitized
ASAN_OPTIONS=detect_leaks=0 build/stage2/test_gpio-sanitized
```

此环境LeakSanitizer曾报告ptrace不兼容，不宣称完成泄漏检测。
测试日志中的模拟DMA完成/错误和test-access的inspect OK只表示模型/调度路径，不能作为真实板端PASS。
第一阶段记录仍保留在build根目录，当前交付证据以build/stage2为准。
