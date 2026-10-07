# ms-probe

ms-probe 是 Microscope 的独立嵌入式诊断 Probe 仓库和 RT-Thread 软件包，
可独立克隆、配置和测试，不依赖 Microscope 主机仓库。`src/ms_probe.c` 和 `include/ms_probe.h` 是平台无关的
Fault 协议核；`ports/ms_scope_baremetal.*` 提供 polling I/O 公共封装，BSP 专用端口
负责异常入口、Fault Context 编码、串口、计时、白名单和可选 Watchdog / Reset。

Probe 只固化并导出 Context 和白名单内存；Microscope 主机 `ms` 根据这些数据和固件 ELF 完成系列的系统诊断功能。

## 许可协议

本软件包采用 **GPL-2.0 / 商业双许可** 。开源许可全文见 [LICENSE](LICENSE)；商业许可可联系 `business@rt-thread.com` 获取，
具体授权以独立书面协议为准，见 [商业许可说明](LICENSES/LicenseRef-Commercial.txt)。
源码保留原版权和变更记录，使用 `GPL-2.0-only OR LicenseRef-Commercial` 标识。
`package.json` 按 RT-Thread 软件包索引惯例以 `GPL-2.0` 标注开源选项，商业选项由本文和
商业许可说明明确。RT-Thread、芯片 SDK 和工程中的其他依赖仍适用各自的许可协议。

## 目录

| 目录 / 文件 | 职责 |
| --- | --- |
| `include/`、`src/` | 平台无关的 Fault 协议核 |
| `ports/` | 裸机 polling 封装及 BSP 专用适配 |
| `Kconfig`、`Kconfig.options`、`SConscript` | 独立配置入口、共享诊断选项和 RT-Thread 构建入口 |
| `package.json` | 软件包元数据，latest 指向 main 分支 |
| `tests/` | C89 主机测试、独立包检查及开发 QEMU 夹具 |
| `docs/limitations.md` | 支持边界与验收要求 |

## RT-Thread 接入

### 软件包索引

索引提交到 `RT-Thread/packages` 的 `tools/ms-probe`。该条目合并并更新本机索引后，
在 BSP 中执行 `menuconfig`，选择 `RT-Thread online packages -> tools packages -> ms-probe`，
版本选择 `latest`，保存后执行：

```sh
pkgs --update
menuconfig
```

下载目录为 `packages/ms-probe-latest`。第二次 menuconfig 从包内 `Kconfig.options` 加载
Microscope、Scope 和 BSP 专用选项，不必手工修改上级 Kconfig。
索引未合并或本机索引尚未更新时使用下面的直接克隆方式。

### 直接克隆

开发时可在 BSP 工程根目录直接获取软件包：

```sh
git clone https://github.com/Real-Thread/ms-probe.git packages/ms-probe
```

在上级 Kconfig 中按工程实际路径引入本包，例如 BSP 根目录的 Kconfig：

```kconfig
rsource "packages/ms-probe/Kconfig"
```

上级 SConscript 加载本包的 `SConscript`。如果工程的 packages 构建脚本已自动扫描软件包，
不重复手工加载。包内使用相对路径和 `rsource`，不依赖原 RT-Thread utilities 路径。

通过 menuconfig 打开 `PKG_USING_MS_PROBE`，它会选择原组件开关 `RT_USING_MICROSCOPE`；
按需启用 `RT_USING_SCOPE`。
既有工程也可继续直接启用 `RT_USING_MICROSCOPE`。Scope Probe 和 Fault 注入默认关闭。
用 `scons --pyconfig-silent` 生成配置，不手工修改 `rtconfig.h`。

`package.json` 提供 RT-Thread 软件包下载元数据，当前只声明 latest/main，不创建虚构的
发布标签。直接克隆方式不依赖索引注册；索引与源码共享同一份包内诊断配置。

在 Microscope 主机工程中，本包作为 Git submodule 固定版本：

```sh
git submodule update --init --recursive ms-probe
```

## BSP 适配

`ports/` 下保留四个 BSP 的 Microscope 专用源文件、汇编入口和链接工作区：

| 目录 | BSP 标识 | 工具链 | 初始化入口 |
| --- | --- | --- | --- |
| `stm32f407-rt-spark/` | `SOC_STM32F407ZG` | `gcc` | `ms_scope_stm32_probe_init()` |
| `qemu-mps3-an536/` | `SOC_QEMU_MPS3_AN536` | `gcc` | `ms_scope_rtthread_init()` |
| `kf32a158-evb/` | `SOC_KF32A158` | `kf32-gcc` | `ms_scope_kf32_probe_init()` |
| `s32k3-core/` | `SOC_FAMILY_S32K3` | `gcc` | `ms_scope_s32k3_probe_init()` |

Kconfig 根据 BSP 标识加载对应默认配置；SCons 仅构建已启用且工具链匹配的端口。
没有匹配 BSP 时只构建通用协议核与裸机 I/O 封装，调用方通过 `ms_probe_config`
提供 Fault ops、静态白名单、Context 和独立工作区。

BSP 仍提供 RT-Thread/芯片头文件、串口初始化、时钟、异常向量接管和正常启动阶段的
初始化调用；本组件不包含通用 BSP 驱动、RTOS 或厂商 SDK。

Fault 路径不得调用 RTOS、设备驱动、锁、堆、日志、阻塞等待或系统 Tick。
Probe 不恢复业务调度，诊断结束后需要复位。生产构建必须关闭组件和 Fault 注入。
本次初始化不代表新的实体板、多核或工具链验收；独立使用前的边界和开放项见
[支持边界](docs/limitations.md)。主机 `ms` / `ms-cli` 和 Env WebUI 不包含在本包中。
