# DID E2E 中文说明

[English](README.md) | 中文

DID E2E 是一个端到端的去中心化身份实验原型，用来演示 DID-like 身份状态、零知识匿名认证、TTSS 门限恢复、恢复后密钥轮换、追踪锚定、委员会节点存储，以及本地 Hardhat 链上的完整开发流程。

## 项目状态

本项目是研究和实验用途的原型，不是生产级 DID 方法实现。

它当前使用本地链账户、演示 token、开发服务、实验性密码流程和本地生成物。项目尚未经过安全审计，不应保护真实身份、真实凭证、真实资金、生产私钥或受监管的个人数据。

## 当前功能范围

已支持：

- 在本地 Hardhat 链上注册 DID-like 身份状态。
- 普通控制器更新和恢复控制器轮换。
- Poseidon-Merkle 根锚定、叶子生成和包含路径生成。
- 基于 Groth16 的 ZK 匿名认证，证明身份属于当前活跃身份根。
- TTSS 份额初始化、恢复、轮换和委员会节点存储。
- 模拟泄露份额的 trace flow，并把追踪结果哈希锚定到链上。
- Docker 和 native 两种开发栈。
- smoke 检查、核心 C++ 测试、格式检查、语法检查和 GitHub Actions CI。

尚未支持：

- 已注册的 W3C DID Method。
- W3C DID Document Resolver。
- DID URL dereferencing。
- DID deactivation 操作。
- W3C Verifiable Credential 的签发、钱包存储、presentation、验证和 credential status。
- 生产级密钥管理、访问控制、监控、部署加固和隐私保护策略。

更多边界说明见 [docs/zh-CN/DID_METHOD.md](docs/zh-CN/DID_METHOD.md) 和 [docs/zh-CN/THREAT_MODEL.md](docs/zh-CN/THREAT_MODEL.md)。

## 设计概览

系统围绕一个本地身份状态模型组织：

- Hardhat 合约以 `idHash` 为 key 保存身份记录，并锚定当前 Poseidon-Merkle root 和 root epoch。
- bulletin-board 服务在链下维护活跃身份镜像，生成 Merkle leaf/path，提交 root 更新，并提供给 C++ 客户端使用的 readiness API。
- C++ CLI 驱动注册、ZK 认证、ZK 恢复、TTSS 初始化/恢复/轮换和 trace 发布。
- 委员会节点保存签名后的 TTSS share envelope，只在份额元数据仍然活跃时返回恢复或追踪所需份额。
- trace helper 模拟泄露份额证据，实验脚本据此验证 trace flow 是否能识别预期的 guardian 集合。

设计上有意区分链上锚定和链下实验服务。链上保存身份状态、TTSS 元数据哈希、trace anchor 和活跃 root；本地生成的私钥、Merkle cache、witness、proof、委员会份额和实验日志都保留在本地服务或输出目录中。

## 中文文档地图

- [docs/zh-CN/ARCHITECTURE.md](docs/zh-CN/ARCHITECTURE.md)：系统边界、组件关系、链上/链下状态和核心流程。
- [docs/zh-CN/API.md](docs/zh-CN/API.md)：本地 HTTP API、endpoint 语义和请求示例。
- [docs/zh-CN/DID_METHOD.md](docs/zh-CN/DID_METHOD.md)：当前实现如何映射到未来 DID Method，以及缺少哪些 W3C DID/VC 能力。
- [docs/zh-CN/CONFIGURATION.md](docs/zh-CN/CONFIGURATION.md)：端口、路径、TTSS 参数和 timeout 配置。
- [docs/zh-CN/EXPERIMENTS.md](docs/zh-CN/EXPERIMENTS.md)：保留的实验脚本、运行方式和结果解释。
- [docs/zh-CN/TESTING.md](docs/zh-CN/TESTING.md)：本地检查和 CI 覆盖。
- [docs/zh-CN/THREAT_MODEL.md](docs/zh-CN/THREAT_MODEL.md)：信任假设、隐私边界和安全风险。
- [docs/zh-CN/DEPENDENCIES.md](docs/zh-CN/DEPENDENCIES.md)：native、npm、C++ 和 ZK 依赖。
- [docs/zh-CN/OPERATIONS.md](docs/zh-CN/OPERATIONS.md)：日常启动、停止、日志和常见故障。
- [docs/zh-CN/RELEASE.md](docs/zh-CN/RELEASE.md)：版本发布流程。
- [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)：中文贡献指南。
- [SECURITY.zh-CN.md](SECURITY.zh-CN.md)：中文安全策略。
- [CHANGELOG.zh-CN.md](CHANGELOG.zh-CN.md)：中文变更日志。

## 快速启动

推荐优先使用 Docker Compose：

```bash
docker compose up --build
```

compose 服务会在首次运行时安装 Hardhat 依赖，构建 C++ 二进制，启动本地链，部署 DID bulletin-board 合约，启动 verifier、bulletin-board、pirate box、委员会节点，并运行内置 smoke setup。

查看状态：

```bash
docker compose exec did-dev scripts/dev.sh status
```

再次运行 smoke：

```bash
docker compose exec did-dev scripts/dev.sh smoke
```

停止：

```bash
docker compose down
```

## Native 开发

native 栈默认读取 `config/dev.env`。可以从模板复制一份自己的配置：

```bash
cp .env.example config/my-dev.env
DID_E2E_CONFIG=config/my-dev.env scripts/dev.sh status
```

安装依赖：

```bash
sudo apt-get update
sudo apt-get install -y build-essential curl jq nodejs npm python3 libsodium-dev libgmp-dev nlohmann-json3-dev
cd hardhat
npm install
cd ..
```

构建：

```bash
scripts/build.sh all
```

启动：

```bash
scripts/dev.sh up
```

smoke：

```bash
scripts/dev.sh smoke
```

状态：

```bash
scripts/dev.sh status
```

停止：

```bash
scripts/dev.sh down
```

## 示例流程

开发栈健康后：

```bash
source run/ttss_phase5_env.sh
./build/did_demo_zk --zk_auth_e2e --id demo_auth --runs 1 --workdir results/examples
./build/did_demo_zk --ttss_setup --id demo_ttss --bb "$BASE_URL" --committee_urls "$COMMITTEE_URLS" --committee_token "$TOKEN" --ttss_n "$TTSS_N" --ttss_t "$TTSS_T" --workdir results/examples
```

smoke 结果会写入：

```bash
run/ttss_phase5_smoke_last.json
```

## 项目结构

- `cpp/`：C++ CLI、委员会节点、ZK/TTSS/trace 流程和共享工具。
- `hardhat/`：智能合约、部署脚本和 Node.js 服务。
- `scripts/`：构建、开发栈、ZK setup 和 smoke 入口。
- `experiments/`：保留的实验 runner、采集工具和结果汇总脚本。
- `docs/`：英文 API、架构、配置、测试、运维和实验文档。
- `docs/zh-CN/`：中文说明文档。
- `results/`：生成的实验结果和日志。
- `run/`：本地进程状态、环境文件和 smoke 状态。

## 开发备注

- C++ 共享工具主要位于 `cpp/text_utils.*`、`cpp/json_utils.*`、`cpp/hex_utils.*`、`cpp/normalize_utils.*` 和 `cpp/process_utils.*`。
- HTTP 层由 `cpp/http_transport.*` 和 `cpp/http_client.*` 负责。
- TTSS 输出和元数据注册由 `cpp/ttss_artifacts.*`、`cpp/ttss_meta_registrar.*` 负责。
- `scripts/dev_common.sh` 放置开发栈脚本共用函数。
- `scripts/dev.sh` 是公开入口，`scripts/start_stop.sh` 是 native 栈编排器。
- CI 定义在 `.github/workflows/ci.yml`。
