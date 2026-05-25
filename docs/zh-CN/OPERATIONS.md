# 运维说明

本文档面向本地开发和实验运行，不是生产运维手册。

## Native 栈

启动：

```bash
scripts/dev.sh up
```

查看状态：

```bash
scripts/dev.sh status
```

运行 smoke：

```bash
scripts/dev.sh smoke
```

停止：

```bash
scripts/dev.sh down
```

## Docker 栈

启动：

```bash
docker compose up --build
```

查看状态：

```bash
docker compose exec did-dev scripts/dev.sh status
```

停止：

```bash
docker compose down
```

## 日志和状态

常见位置：

- `run/`：PID、env 文件、smoke 状态。
- `results/`：实验输出和示例输出。
- Hardhat 服务日志：由启动脚本写入运行目录。
- 委员会节点日志：用于定位 share 写入、恢复和 trace 请求问题。

`run/ttss_phase5_env.sh` 是本地流程常用环境入口，包含 `BASE_URL`、`COMMITTEE_URLS`、`TOKEN`、`TTSS_N`、`TTSS_T` 等。

## 常见故障

- 端口占用：先运行 `scripts/dev.sh down`，确认旧进程已停止。
- npm 依赖缺失：进入 `hardhat/` 后运行 `npm install` 或 `npm ci`。
- C++ 构建失败：确认 `libsodium-dev`、`libgmp-dev`、`nlohmann-json3-dev` 已安装。
- ZK proof 路径找不到：确认 circuits 和 Poseidon 相关构建产物存在，并使用当前脚本生成。
- TTSS 恢复失败：检查 `TTSS_T`、委员会节点数量、份额是否 active，以及 metadata version/epoch 是否匹配。
- trace 失败：检查 pirate box 是否有预期 leaked material，committee challenge shares 是否可用。

## 实验运行

实验前建议重新运行 smoke，确保基础栈健康：

```bash
scripts/dev.sh smoke
source run/ttss_phase5_env.sh
```

实验完成后，如果需要干净环境，可停止并重新启动开发栈。

## 相关文档

- [配置说明](CONFIGURATION.md)
- [测试说明](TESTING.md)
- [实验说明](EXPERIMENTS.md)
- [威胁模型](THREAT_MODEL.md)
