# 现有工具链调查

日期：2026-11-11。未安装、替换或修改任何工具链/SDK。

- 使用 `/usr/bin/aarch64-linux-gnu-gcc`，版本 `Ubuntu 9.4.0-1ubuntu1~20.04.2`。
- target triplet：`aarch64-linux-gnu`；`-print-sysroot` 返回 `/`，这是发行版cross布局，
  目标库实际在 `/usr/aarch64-linux-gnu/lib`，不能据此链接宿主x86库。
- 静态libc：`/usr/aarch64-linux-gnu/lib/libc.a`；目标动态glibc为2.31。
- 动态loader：`/usr/aarch64-linux-gnu/lib/ld-linux-aarch64.so.1 -> ld-2.31.so`。
- `arm64-gcc` 未在PATH中找到。只读搜索了/usr、/usr/local、/home/fmsh可访问目录的有限深度；
  未发现可识别的另一个SDK gcc；/opt不可读，故未声称完整调查其中SDK。
- 现有 `/usr/bin/qemu-aarch64-static` 用于纯软件CLI验证。
- 使用默认GCC目标，不启用高于基础ARMv8-A的 `-march=native` 等指令扩展。

实际构建：

```sh
make CROSS_COMPILE=aarch64-linux-gnu-
```

编译/链接展开：

```sh
aarch64-linux-gnu-gcc -Iinclude -D_FILE_OFFSET_BITS=64 -O2 -g \
  -std=c11 -Wall -Wextra -Werror -Wformat=2 \
  src/main.c src/common.c src/dma_mmio.c src/udmabuf.c src/gpio_ctrl.c \
  -static -o build/ffttest
```

`build/elf-check.txt` 保存file/readelf检查：ELF64、little-endian、AArch64、EXEC、静态链接，
没有INTERP段、没有dynamic section。`.note.ABI-tag`声明Linux 3.7.0最低ABI。
与目标Linux5.4.52 AArch64的基础用户态ABI相容；尚未在真实板端运行。
涉及/dev/mem、GPIO ioctl和udmabuf mmap的功能仍须目标权限及驱动实际行为验证。

现有工具链此次经主机检查、交叉编译和QEMU验证，不声称已有该板部署成功历史。
完整命令调查结果见 `build/toolchain-report.txt`。

第二阶段重编译仍使用上述工具链；当前ELF和测试以`build/stage2/`为准，
其中toolchain-report.txt、elf-check.txt、ELF-SHA256.txt记录最新交付；build根目录旧日志为第一阶段历史记录。
