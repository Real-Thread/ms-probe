# 支持边界与验收要求

ms-probe 仅用于开发、测试与故障定位。生产配置必须关闭软件包、Microscope、Scope 和
Fault 注入，禁止把本包作为生产故障恢复或 AUTOSAR 合规能力交付。

## Fault Runtime

- Fault 路径采用 C89，不调用 RTOS、设备驱动、线程、锁、堆、日志、阻塞等待或系统 Tick。
- UART、单调计时、内存白名单、Context、Watchdog 和 Reset 由 BSP 提供。
- Probe 使用独立静态工作区和 Emergency Stack，进入后不恢复业务调度，退出必须复位。
- 软件复位请求不等同于板级复位成功；无效时应使用板卡复位按钮或断电重启。
- 当前仅以单核 Fault Runtime 为完整支持范围；多核 Owner、停核/ACK、每核资源和跨核一致性未闭环。
- `MEMORY_STABLE` 不能代替 DMA、Cache、其他核和共享总线主设备的真实停止与一致性验证。

## BSP 与架构

- 仓库保留 STM32F407、QEMU MPS3 AN536、KF32A158、S32K3 的已有端口，不代表本次重新完成板测。
- BSP 负责向量接管、启动阶段初始化和保留 RAM。Scope 工作区不能与堆栈、数据、MPU 或其他应用重叠。
- QEMU 的完整链接脚本只作为参考；实际 BSP 必须独立确认 SCOPE_RAM、工作区和容量断言。
- 实体板必须验证 UART partial I/O、真实 Watchdog 时序、Fault Context、嵌套 Fault 和复位生命周期。
- 白名单读取本身可能触发嵌套 Fault；只有目标端口实现并验证了受保护读取，才能按其能力使用。
- Cortex-M、Cortex-R52 和 KungFu32 栈回溯端口及约束见 [架构说明](../arch/README.md)。
- CoreDump 的 Kconfig 架构选项不代表每种架构均有完整实现；必须检查目标架构的实际源码和链接。

## 本次初始化验证

只验证通用 Probe 的 C89 主机用例、独立软件包元数据、许可声明、相对文档链接、Kconfig
配置和 SConscript 端口选择。不新声明真实 QEMU、实体板、多核或交叉工具链的验证结论。
运行主机端时只使用本机或可信隔离调试网络，不将 Probe 协议暴露到不可信网络。
