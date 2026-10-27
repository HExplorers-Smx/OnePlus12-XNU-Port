# CI 额度保护政策

本仓库严禁因日常提交而自动运行 GitHub Actions。

- 工作流仅允许 `workflow_dispatch` 手动触发，不得加入 `push`、`pull_request`、`pull_request_target`、`schedule`、`workflow_run`、`repository_dispatch` 或其他自动事件。
- 任何新增工作流都必须先检查 `on:` 事件列表，再合入 `main`。
- 本地可以直接运行的编译、测试、校验优先在本地完成；提交源码不意味着授权执行 Actions。
- 单次手动构建设置 `timeout-minutes`；禁止自动重试、矩阵扩容和循环触发。
- 用户只有在明确要求或自行点击 **Actions → Run workflow** 时才运行；助手不会主动发起工作流运行。
- 私人 APK 签名密钥、原厂固件二进制、含设备标识的 Root 报告不得提交或作为公开构建工件。
- 即使余额恢复，本政策持续有效，除非用户明确批准变更。

提示：此政策约束本仓库配置；若账号中其他仓库有自动工作流，需分别检查。
