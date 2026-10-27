# GitHub Actions 配额保护

此仓库默认**仅手动运行 Actions**。所有工作流的 `on:` 下只允许 `workflow_dispatch:`，禁止 `push`、`pull_request`、`schedule`、`workflow_run` 等自动触发器。

平时的源码提交、分支更新和 PR 不应触发自动构建。需要云端构建时，由仓库所有者在 GitHub > Actions > 选择工作流 > Run workflow 手动启动一次。开发阶段优先在本地测试。

不得把 APK 的发布签名私钥、手机原始设备树、GPU 专有固件、个人设备报告加入公开仓库。DeviceArchive 公开构建应当只生成调试签名 APK；调试签名不能覆盖已有的正式签名版本。

XNU 的主机测试通过不代表 iOS 已在一加 12 上启动，也不代表 GPU 已硬件加速。
