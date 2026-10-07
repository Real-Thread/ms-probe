# 栈回溯组件

栈回溯是 `microscope` 下的通用诊断组件。公共层负责参数校验、当前线程信息获取、
结果输出和稳定 API；架构层负责读取 SP，并按目标架构的 ABI、指令集和栈布局完成回溯。
公共目录不依赖具体芯片、BSP、工具链或链接脚本。

## 目录与职责

```text
microscope/
├── backtrace/                 公共 API、公共逻辑、配置和测试
└── arch/<rtconfig.ARCH>/      可选的架构适配实现
```

架构适配实现 `backtrace_internal.h` 声明的端口接口，并由对应架构在 `libcpu/Kconfig` 中选择
`RT_USING_MICROSCOPE`。只有已声明该能力的架构才能配置 `RT_USING_STACK_BACKTRACE`。

支持情况和各端口的约束见[架构适配说明](../arch/README.md)。`libcpu` 中已有但尚未实现本组件
端口接口的回溯代码仍保持独立，不能仅通过声明架构能力接入。

## 配置

在 Kconfig 的 `Utilities -> Microscope` 下启用：

```text
RT_USING_STACK_BACKTRACE=y
RT_STACK_BACKTRACE_DEPTH_MAX=8
```

`RT_STACK_BACKTRACE_DEPTH_MAX` 的有效范围是 1 至 64，用于限制公共打印接口和异常处理代码的
本地缓冲区大小。

## 公共接口

- `rt_backtrace()`：打印当前线程的调用地址并返回实际深度；
- `rt_backtrace_capture()`：将当前线程的调用地址写入调用者缓冲区；
- `rt_backtrace_from_frame()`：从规范化的 PC、SP、FP、LR 和栈边界现场采集调用地址；
- `rt_backtrace_print_frame()`：打印规范化现场的调用地址；
- `rt_backtrace_from_stack()`：从指定 SP 和栈内存范围采集调用地址，适用于异常现场。

`stack_addr` 表示栈内存范围的最低地址，`stack_size` 表示该范围的字节数。公共层会拒绝空缓冲区、
零容量、地址溢出以及不在栈范围内的 SP。返回值不会超过调用者传入的缓冲区容量。

`rt_backtrace()` 输出一行由空格分隔的地址，地址宽度随 `rt_ubase_t` 自动调整：

```text
0x00133b04 0x00133b24 0x00112760
```

应使用产生回溯的同一版本 ELF 和对应工具链的 `addr2line` 解析地址：

```sh
<toolchain-prefix>-addr2line -e <firmware.elf> -a -f <address...>
```

启用 MSH 后可执行 `backtrace_test`，该命令通过三级非内联调用检查基本回溯能力。

## 架构适配接口

架构端口需要实现：

```c
rt_err_t rt_backtrace_arch_get_current_frame(struct rt_backtrace_frame *frame);
rt_size_t rt_backtrace_arch_unwind(rt_ubase_t *buffer,
                                   rt_size_t size,
                                   const struct rt_backtrace_frame *frame);
```

公共层保证传给 `rt_backtrace_arch_unwind()` 的缓冲区、容量以及已标记有效的 SP 和栈范围通过
基础校验。架构层根据有效字段选择展开算法，并负责地址对齐、栈增长方向、返回地址识别、调用点
修正和代码区合法性检查。

新增适配时应完成以下事项：

1. 在 `microscope/arch/<rtconfig.ARCH>` 增加 `SConscript` 和端口实现；
2. 在架构 Kconfig 中 `select RT_USING_MICROSCOPE`；
3. 记录 ABI、编译选项、链接符号和地址解析约束；
4. 在目标板执行 `backtrace_test`，并验证线程现场和异常现场；
5. 使用固件 ELF 检查输出地址能够解析到预期函数和源码位置。

内联、尾调用优化、缺失的展开信息或已损坏的栈都可能减少回溯深度。具体保证由架构端口说明，
公共 API 不假定端口使用帧指针、展开表或栈扫描中的任一种算法。
