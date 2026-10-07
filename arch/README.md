# Microscope 架构适配

该目录保存 `microscope` 组件的架构相关实现。子目录名称与 `rtconfig.ARCH` 保持一致，
`arch/SConscript` 会在相应组件启用时自动加载匹配的端口构建脚本。

## 栈回溯适配

| 架构 | 端口位置 | 状态 | 说明 |
| --- | --- | --- | --- |
| KungFu32 | `kungfu32` | 已支持 | [端口约束](kungfu32/README.md) |
| ARM Cortex-M | `arm/cortex-m` | 已支持 | [端口约束](arm/README.md) |
| ARM Cortex-R52 | `arm/cortex-r` | 已支持 | [端口约束](arm/README.md) |

只有符合 microscope 公共接口的实现才能选择 `RT_USING_MICROSCOPE`。`libcpu` 负责异常现场和
线程上下文等 CPU 端口逻辑，回溯算法及其构建入口统一放在本目录，避免重复实现。
