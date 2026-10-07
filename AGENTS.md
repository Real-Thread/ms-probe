# ms-probe Repository Guidelines

## 范围与许可

本仓库是独立 RT-Thread 软件包，也是 Microscope 主机工程的 Git submodule。
采用 GPL-2.0 / 商业双许可：保留 LICENSE 全文、商业许可说明和原版权；源码使用
`GPL-2.0-only OR LicenseRef-Commercial`。不恢复禁止再发布声明或改成 Apache 单许可。

## 实现约束

- 通用协议核位于 include/ 与 src/，不含芯片寄存器地址或 RTOS 依赖。
- BSP 适配只放在 ports/，架构回溯放在 arch/，公共 Backtrace 和 CoreDump 独立维护。
- Fault 路径采用 C89，不调用设备驱动、锁、堆、日志、阻塞等待或系统 Tick。
- Probe 不恢复业务调度，退出需要复位；生产配置必须关闭所有诊断和 Fault 注入。
- 未经明确验证，不声明实体板、多核、Watchdog、Reset 或新的交叉工具链支持。
- 不手工修改 rtconfig.h 或工程生成文件，不修改父仓库中的主机业务实现。

## 验证与提交

```sh
make -C tests test
python3 -m unittest discover -s tests -p 'test_package.py' -v
make -C tests clean
```

配置检查使用 tests/requirements.txt 中的依赖，也可激活 Env venv。
文档的本地链接必须在本仓库内有效；测试仅使用虚构数据，不提交 ELF、内存转储或凭据。
提交前检查分支和暂存内容，只提交本软件包。推送后核对远端提交，再更新父仓库 gitlink。
测试结束关闭本次启动的服务并释放端口，不结束用户原有服务；本包主机验证不需要预览服务。
