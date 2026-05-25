# 配置说明

项目配置目标是把端口、路径、TTSS 参数和 timeout 集中管理，避免散落在脚本默认值中。

## 配置文件

- `.env.example`：开源模板，列出可配置项和默认含义。
- `config/dev.env`：本地开发默认配置。
- 自定义 env 文件：通过 `DID_E2E_CONFIG=/path/to/file.env` 传给脚本。

`scripts/dev.sh` 和底层启动脚本会读取这些配置。直接运行实验脚本时，也建议先 `source run/ttss_phase5_env.sh` 或显式传入相同变量。

## 选择配置

使用默认配置：

```bash
scripts/dev.sh up
```

使用自定义配置：

```bash
cp .env.example config/my-dev.env
DID_E2E_CONFIG=config/my-dev.env scripts/dev.sh up
```

查看状态：

```bash
DID_E2E_CONFIG=config/my-dev.env scripts/dev.sh status
```

## 主要配置项

常见端口和 URL：

- `BASE_URL`：bulletin-board 服务地址，默认通常为 `http://127.0.0.1:3000`。
- `VERIFIER_URL`：ZK verifier 服务地址。
- `PIRATE_URL`：trace helper 服务地址。
- `COMMITTEE_URLS`：逗号分隔的委员会节点地址。
- Hardhat RPC 端口：本地链 JSON-RPC 地址。

TTSS 参数：

- `TTSS_N`：guardian 总数。
- `TTSS_T`：恢复阈值。
- `TOKEN`：本地委员会节点 demo token。

路径相关：

- `RUN_DIR`：运行时状态和 env 输出目录，通常为 `run/`。
- `RESULTS_DIR`：实验和示例输出目录，通常为 `results/`。
- ZK build/circuit 路径：由脚本和 `zk_paths` 统一发现。

## Timeout

项目中存在多类等待：

- 服务启动等待。
- Hardhat 交易确认等待。
- bulletin-board mirror readiness 等待。
- Merkle leaf/path/root 一致性等待。
- proof generation 和 verifier 请求等待。
- 委员会节点请求等待。

这些值应在 env 配置中集中设置，脚本读取配置后再传给 C++ 或 Node.js 服务。新增脚本不要私自写新的硬编码 timeout。

## TTSS 与委员会一致性

`TTSS_N` 应与 `COMMITTEE_URLS` 中可用委员会节点数量一致或小于等于它。`TTSS_T` 必须满足：

```text
1 <= TTSS_T <= TTSS_N
```

实验中常见配置是 6 个委员会节点、阈值 4。边界实验可以覆盖不同阈值，但主开发配置应保持稳定，便于 smoke 和 CI 验证。

## ZK Depth 一致性

Poseidon-Merkle tree depth 必须在以下位置保持一致：

- Circom circuit 参数。
- witness/proof 生成输入。
- bulletin-board Merkle mirror。
- C++ proof flow。

不同树深的实验脚本已不作为主流程维护。若重新引入，需要明确放到实验目录并标注不是默认 smoke 路径。

## 生成路径

常见生成物：

- `build/`：C++ 构建产物。
- `run/`：PID、env、smoke state 和运行时状态。
- `results/`：示例、实验日志和结果汇总。
- `hardhat/node_modules/`：npm 依赖。
- `zk_build/` 或 circuit build 目录：ZK 编译产物。

这些目录通常不应提交到 Git。

## Native 与 Docker

Docker Compose 是最接近开源读者的一键入口。native 模式适合本地开发、调试和实验批跑。

两种模式都应尽量使用同一套配置变量，避免“Docker 能跑、native 不一致”或“实验脚本绕过配置”的情况。
