# 中文文档索引

[English docs](../ARCHITECTURE.md) | 中文

本目录提供 DID E2E 的中文说明。英文文档仍是上游主文档，中文文档用于帮助中文读者理解项目边界、架构、API、配置、实验和安全假设。

## 推荐阅读顺序

1. [项目 README](../../README.zh-CN.md)：项目目标、快速启动和目录结构。
2. [架构说明](ARCHITECTURE.md)：链上、链下、C++ CLI 和委员会节点如何协作。
3. [DID Method 说明](DID_METHOD.md)：当前实现与 W3C DID/VC 的关系，以及尚未实现的部分。
4. [配置说明](CONFIGURATION.md)：端口、路径、TTSS_N/T 和 timeout 如何集中管理。
5. [API 说明](API.md)：bulletin board、committee node、verifier 和 trace helper 的本地接口。
6. [测试说明](TESTING.md)：本地检查、核心 C++ 测试和 CI 覆盖。
7. [实验说明](EXPERIMENTS.md)：保留的实验 runner 和结果解释。
8. [威胁模型](THREAT_MODEL.md)：信任假设、隐私边界和非生产警告。

## 文档边界

中文文档描述的是当前仓库实现，不是论文级 DID/VC 标准说明，也不是生产部署手册。若实际代码和文档存在差异，应以当前代码、脚本和 CI 为准。
