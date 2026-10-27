# 下一步升级方向

1. 维护匹配版本固件 manifest（下载链接、构建标识、SHA256、设备型号、boot/dtbo/vendor_boot 的头和 dtbo 索引）。原始镜像存放于维护者私有环境，不纳入 Git。
2. 让 DeviceArchive v1.1 可显式导出 DTB/设备树**非敏感结构化元数据**和面板/触控驱动的 `compatible`，不碰分区写入。
3. 增加 `waffle` 专用 UEFI 源码/补丁目录、锁定上游 commit，并为 UEFI 编译创建单独手动 Actions；**在完成引导协议审计前只编译不刷写**。
4. 确认 Bootloader 是否支持无持久写入的临时启动与可恢复调试，准备日志、断电及 Android 回退流程。不可默认支持 `fastboot boot`。
5. XNU CPU0/内存映射、UFS、显示触控顺序补齐，每一步分别记录主机测试和真机测试。
