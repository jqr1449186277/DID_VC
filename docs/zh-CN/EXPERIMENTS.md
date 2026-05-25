# 实验说明

实验脚本位于 `experiments/`，用于批量运行 ZK auth、ZK recovery、TTSS recovery、TTSS trace、规模变化和链上交互等场景。项目已经删除或归档过期脚本，保留的 runner 应适配当前目录结构和配置方式。

## 目录结构

- `experiments/runs/`：保留的实验 runner。
- `experiments/lib/`：通用实验 helper、计时、日志、配置读取。
- `experiments/tools/`：结果汇总和分析工具。
- `results/`：实验输出目录，通常不提交。

## 常用前置步骤

先启动开发栈：

```bash
scripts/dev.sh up
scripts/dev.sh smoke
source run/ttss_phase5_env.sh
```

运行单个实验：

```bash
bash experiments/runs/run_G_zk_auth.sh
```

结束后停止服务：

```bash
scripts/dev.sh down
```

## 保留的实验类别

当前保留的实验主要覆盖：

- ZK authentication E2E。
- ZK recovery E2E。
- TTSS recovery batch。
- TTSS trace batch。
- TTSS threshold boundary。
- TTSS scale。
- chain/committee smoke suite。

不同树深的旧实验不作为当前维护对象，因为它们和当前 circuit/build 路径容易偏离。

## 结果解释

实验输出通常包含：

- 每轮是否成功。
- latency 或分阶段耗时。
- root/epoch/version。
- TTSS 份额数量、阈值、恢复是否成功。
- trace accused set 是否匹配预期。
- 错误信息和服务日志路径。

分析实验结果时要区分：

- 功能失败：流程未完成、proof 未通过、份额不足、anchor 不一致。
- 环境失败：服务未启动、端口占用、依赖缺失、Hardhat 进程异常。
- 参数失败：`TTSS_N/T`、committee URLs、tree depth 或 timeout 配置不一致。

## 新增实验要求

新增实验应满足：

- 使用 `scripts/dev.sh` 和 `config/dev.env` 约定，不重复硬编码端口和路径。
- 输出到 `results/`，不要写入源码目录。
- 明确实验目标、输入参数和结果字段。
- 能在当前默认配置下运行，或者在文件头说明额外依赖。
- 不把过期路径、旧 build 目录或旧二进制名称重新带回项目。
