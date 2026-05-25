# 贡献指南

[English](CONTRIBUTING.md) | 中文

DID E2E 目前是实验原型。贡献时请优先保持项目可复现、可测试、结构清晰，并避免把生产级承诺写进文档或接口。

## 开发环境

推荐先用 Docker Compose 验证完整栈：

```bash
docker compose up --build
docker compose exec did-dev scripts/dev.sh smoke
docker compose down
```

native 开发请参考 [docs/zh-CN/CONFIGURATION.md](docs/zh-CN/CONFIGURATION.md) 和 [docs/zh-CN/DEPENDENCIES.md](docs/zh-CN/DEPENDENCIES.md)。

## 提交前检查

在提交 PR 或发布前，建议至少运行：

```bash
scripts/check_repo_format.sh
scripts/check_markdown_links.py
scripts/check_js_syntax.sh
scripts/build.sh all
scripts/run_cpp_tests.sh
```

如果改动涉及服务启动、ZK、TTSS 或 trace flow，还应运行：

```bash
scripts/dev.sh up
scripts/dev.sh smoke
scripts/dev.sh down
```

## 代码规范

- C++ 使用 C++17，保持函数职责单一，优先复用 `cpp/*_utils.*` 中已有工具。
- JSON 处理优先使用 `nlohmann::json`，不要继续添加手写 JSON 字符串拼接。
- 新增进程执行逻辑应使用 `process_utils`，不要新增 `std::system` 或 `popen`。
- HTTP 访问优先走 `http_transport`/`http_client`，不要在业务流里再实现 raw socket。
- shell 脚本读取 `config/dev.env` 或 `DID_E2E_CONFIG`，不要把端口、路径、timeout 散落成新的默认值。
- 文档需要明确实验边界，不要把当前实现描述成完整 W3C DID/VC 产品。

## 安全说明

请不要提交私钥、真实身份材料、真实凭证、生产配置或敏感日志。发现安全问题请参考 [SECURITY.zh-CN.md](SECURITY.zh-CN.md)。
