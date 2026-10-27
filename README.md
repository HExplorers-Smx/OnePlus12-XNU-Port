> **Actions 配额保护：本仓库不自动运行任何工作流，仅在手动选择 Run workflow 时运行。** 规则见 [配额保护政策](docs/ACTIONS_QUOTA_POLICY.md)。

# OnePlus 12 (waffle) XNU / UEFI port

一加 12 PJD110（SM8650 / pineapple）的实验性 UEFI、XNU 板级移植工程。本阶段仅有硬件事实清单、验证工具和自动检查；目前**不存在可刷机 iOS 镜像**。

保护现有 Android 系统，不允许将其他型号的分区表和未经核实的寄存器地址视为可用资料；不在本仓库存放厂商私有固件、Apple iOS 镜像、设备唯一标识或签名密钥。

> 后续文件包含已验证板级配置、测试、文档及 Actions 工作流。