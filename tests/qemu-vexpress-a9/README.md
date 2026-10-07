# VExpress A9 QEMU 测试夹具

本目录只用于单核 RT-Thread classic 开发测试，不是完整 Cortex-A9 产品端口。
`msprobe_test` 执行真实 ARM UDF；链接包装仅接管该命令已标记且 PC 匹配的异常。
其他异常仍走 RT-Thread 原 handler，不改写其源码。

异常后切换独立 8 KiB 静态栈，屏蔽 IRQ/FIQ 和 UART0 中断，运行平台无关协议核。
PL011 使用非阻塞 MMIO polling；每次 TX 故意只返回一个字节，覆盖 partial TX。
虚构白名单 `0x20000000..0x200000ff` 返回 `->\r\n` 加字节序列 `4..255`，
不直接读取该地址。另开放实际 RAM 中的 `ms_probe_qemu_sample` 测试结构体，白名单仅为
该对象的 16 字节，不开放整个 RAM。结构体字段为 `value=0x12345678`、`counter=7`、
`message="qemu"`，供 ELF/DWARF 与 typed print 比对。

架构字段为 `MS_PROBE_ARCH_ARMV7_A` (`0x11`)，不伪装为 Cortex-R52。
Microscope 主机插件中的 `qemu.arm.vexpress-a9-test` 描述符可进行握手、状态、内存读取、
ELF/DWARF、缓存和会话测试。未提供 ARMv7-A Context、Backtrace、Reset、Watchdog 或
独立时钟超时接口；不宣称完整产品端口或 WebUI 验收。主机插件需支持该架构标识和测试描述符。

## 独立构建

在 RT-Thread **测试副本** 中，将本目录复制到
`bsp/qemu-vexpress-a9/applications/ms-probe-test/`，将 `defconfig` 复制为 BSP 的 `.config`。
不要覆盖用户已有配置；BSP 的 rtconfig.py 在构建时也会生成 automac.h。

```sh
source ~/.env/.venv/bin/activate
cd <RT-Thread 测试副本>/bsp/qemu-vexpress-a9
export RTT_ROOT=<RT-Thread 测试副本>
export RTT_EXEC_PATH=<arm-none-eabi-gcc 的 bin 目录>
export MS_PROBE_ROOT=<ms-probe 仓库绝对路径>
scons --pyconfig-silent
scons -j4
qemu-system-arm -M vexpress-a9 -smp 1 -m 128M -kernel rtthread.bin \
  -nographic -monitor none -nic none -audio none
```

SCons 包含当前协议核，GNU `--wrap=rt_hw_trap_undef` 只在此测试工程生效。
在 MSH 输入 `msprobe_test` 后必须看到 `->` 后换行；再发送 CR / LF / CRLF。
自动验收应使用原始 TCP UART 字节，避免终端 CR/LF 转换影响观察。
退出需要终止 QEMU；结束时必须关闭本次进程及串口端口。
