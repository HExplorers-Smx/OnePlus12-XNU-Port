# 参考项目与上游边界

- [UniXNU-OnePlus-Ace5](https://github.com/tycykp/UniXNU-OnePlus-Ace5)：SM8650 一加 Ace 5 的 UEFI/XNU 实验移植参考。设备是 PKG110/giulia，**不是** PJD110/waffle。所有设备地址、显示/触控和存储表必须重新验证。
- [HakiOS](https://github.com/oosexgang/hackios-xnu-kernel)：另一套 Qualcomm 设备 XNU 实验工程，展示 `devices/<name>` 分离硬件的方法。
- [OnePlus-12-Development 设备树](https://github.com/OnePlus-12-Development/android_device_oneplus_waffle)：用于确认 waffle 固件与 Android 源码关系，不是 XNU 驱动。
- [OnePlus 官方开源中心](https://service.oneplus.com/global/search/search-detail?id=2096329)：内核源码应以与真机系统版本匹配、可核验来源的版本为准。

这个仓库只记录依赖和待验证差异，不将上游代码假称为本项目成果。以后引用时按上游 License 履行归属、版权和修改告知。XNU 和 UEFI 的构建、GPU 驱动尚未在一加 12 上验证。
