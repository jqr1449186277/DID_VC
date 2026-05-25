# 变更日志

[English](CHANGELOG.md) | 中文

本项目遵循“面向实验原型”的变更记录方式。`main` 分支可能继续重构接口和目录结构；在 `1.0` 之前，HTTP 字段、CLI 选项和实验输出格式都不应视为稳定第三方契约。

## 未发布

- 补充中文说明文档，包括 README、贡献指南、安全策略、架构、API、配置、实验、测试、威胁模型、依赖和发布流程。

## v0.1.0 准备阶段

- 整理开源项目结构，补充 LICENSE、README、SECURITY、CONTRIBUTING、issue/PR 模板和 CI。
- 清理未使用 C++ 文件和实验脚本。
- 拆分 C++ 主流程、HTTP、ZK、TTSS、trace 和工具层。
- 拆分 Hardhat bulletin-board 服务。
- 引入配置文件、Docker Compose、smoke 检查和核心测试。

## 初始原型

- 实现本地 DID-like 身份状态注册。
- 实现 Poseidon-Merkle root anchoring。
- 实现 Groth16 ZK authentication。
- 实现 TTSS setup/recovery/rotation。
- 实现 trace flow 和 trace anchor。
