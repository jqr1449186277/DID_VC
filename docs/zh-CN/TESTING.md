# 测试说明

项目测试分为格式/语法检查、C++ 构建与单元测试、开发栈 smoke、Hardhat 检查和 GitHub Actions CI。

## 快速本地检查

```bash
scripts/check_repo_format.sh
scripts/check_markdown_links.py
scripts/check_js_syntax.sh
```

这些检查覆盖：

- 仓库格式和基础 hygiene。
- Markdown 本地链接有效性。
- JavaScript 文件语法。
- 常见脚本结构问题。

## C++ 检查

构建：

```bash
scripts/build.sh all
```

运行核心测试：

```bash
scripts/run_cpp_tests.sh
```

CI 还会使用 warning flags 和 sanitizer flags 进行构建与 smoke。新增 C++ 文件时，应确保 Makefile/build 脚本能覆盖到对应目标。

## Hardhat 检查

```bash
cd hardhat
npm ci
npx hardhat compile
```

如果环境没有完整 Hardhat compile 能力，CI 中可能回退到 `solcjs` 路径，但本地开发建议保持 Hardhat 依赖完整。

## 开发栈 Smoke

```bash
scripts/dev.sh up
scripts/dev.sh smoke
scripts/dev.sh down
```

smoke 会验证服务启动、合约部署、TTSS setup、leaf readiness 和 TTSS metadata readiness 等基础路径。

## 核心功能覆盖

项目历史上已验证过的关键 E2E 路径包括：

- ZK auth E2E。
- ZK recovery E2E。
- TTSS setup。
- TTSS recover。
- TTSS recover and rotate。
- TTSS trace。
- TTSS trace publish。
- 委员会直连恢复/追踪请求。

这些路径依赖本地服务、配置和生成物，通常不全部放入最短 CI，但应在发布前或大改动后手动跑代表性用例。

## CI 覆盖

GitHub Actions 当前覆盖：

- repo format。
- shell syntax。
- JS syntax。
- markdown link check。
- C++ warning build。
- C++ core tests。
- sanitizer build 和 sanitizer tests。
- Hardhat compile。
- Docker Compose config。

CI 通过只说明当前自动检查范围通过，不代表项目达到生产安全标准。
