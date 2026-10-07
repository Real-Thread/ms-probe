# ARM 栈回溯适配

ARM 适配由 `arch/arm/SConscript` 根据 CPU 类型选择 Cortex-M 或 Cortex-R52 实现。两个端口均
使用公共的 `rt_backtrace_frame` 描述当前线程或异常现场。

## Cortex-M

Cortex-M 端口扫描有效栈范围中的候选返回地址，并检查调用点是否为 Thumb `BL` 或 `BLX`
指令。端口优先使用 BSP 提供的弱函数 `is_in_code_range()` 检查代码地址，否则使用常见的弱链接
代码区边界符号。

BSP 应提供可靠的代码区判断函数或链接符号。内联、尾调用优化以及栈损坏会影响回溯结果。

## Cortex-R52

Cortex-R52 端口使用 ARM EHABI `.ARM.exidx` 展开表，并需要现场提供 PC、SP、FP、LR 和线程
栈范围。工具链应启用展开表生成，链接脚本应保留 `.ARM.exidx` 并定义其起止符号。

异常处理代码负责修正异常类型对应的 PC 偏移，再调用 `rt_backtrace_print_frame()`。端口本身不
依赖 Cortex-R52 的私有异常栈结构。
