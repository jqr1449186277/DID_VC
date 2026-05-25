# 架构说明

DID E2E 是一个本地端到端实验原型，覆盖 DID-like 身份状态、ZK 认证、门限恢复和 trace-flow 实验。它的目标是验证设计和流程，而不是发布生产级 DID Method。

## 系统边界

项目刻意分成三层：

- 链上锚定层：`DIDBulletinBoardZK.sol` 保存身份记录、活跃 Merkle root、TTSS 元数据哈希和 trace anchor。
- 链下服务层：Node.js 服务维护本地 Merkle 镜像，提供 HTTP API，封装 proof verification，并模拟 trace evidence。
- C++ 客户端和委员会层：C++ CLI 驱动各类流程；委员会节点保存和返回 TTSS share envelope。

链是身份记录和锚定状态的来源。链下镜像是实验和性能层，它把链上记录转换成 Poseidon leaf、Merkle path 和 readiness snapshot。

## 组件关系

```text
did_demo_zk CLI
  |-- HTTP --> bb_service_zk.js ---- JSON-RPC ---- DIDBulletinBoardZK.sol
  |-- HTTP --> zk_verify_service.js
  |-- HTTP --> committee_node :8001..8006
  |-- HTTP --> pirate_box.js

bb_service_zk.js
  |-- state_store.js       本地持久化镜像状态
  |-- merkle_tree.js       Poseidon-Merkle 镜像和路径
  |-- chain_client.js      ethers 合约封装
  |-- routes_register.js   注册、更新、恢复接口
  |-- routes_ttss.js       TTSS 元数据接口
  |-- routes_trace.js      trace anchor 接口
```

## 主要组件

- `did_demo_zk`：C++ CLI，负责 ZK auth、ZK recovery、TTSS setup/recover/rotate、trace 和 trace publish。
- `committee_node`：C++ HTTP 服务，保存签名 share envelope，并按恢复或追踪请求返回活跃份额。
- `bb_service_zk.js`：bulletin-board API，本地 Merkle mirror，以及链上交易提交入口。
- `zk_verify_service.js`：本地 Groth16 verifier wrapper。
- `pirate_box.js`：trace-flow helper，保存实验中的泄露份额材料。
- Hardhat chain：本地以太坊兼容链，用于部署合约和提交交易。

## 链上状态

合约以 `bytes32 idHash` 保存身份记录：

- `cid`：用于 ZK leaf 的 commitment 或 DID pointer。
- `owner`：普通更新控制器。
- `recovery`：恢复控制器。
- `version`：单调递增身份版本。
- `active`：该记录是否参与当前活跃 root。
- `pkNormHash`：普通密钥哈希的 field 表示。
- `pkRecHash`：恢复密钥哈希的 field 表示。

额外锚定状态：

- `activeRoot` 和 `rootEpoch`：当前 Poseidon-Merkle root 和 epoch。
- TTSS metadata：`vkSetHash`、`metaHash`、version 和 epoch。
- Trace anchor：accused-set hash、proof hash、trace digest、version 和 epoch。

## 链下镜像

`bb_service_zk.js` 在 Hardhat 目录维护本地状态：

- `mirror`：`idHash` 到活跃身份记录。
- `idByHash`：demo ID 与 `idHash` 的本地可逆映射。
- `ops` 和 `requestToOp`：异步操作状态和幂等请求跟踪。
- `ttssMeta`：TTSS metadata anchor 的本地视图。
- `traceAnchors`：trace anchor 的本地视图。
- `rootCache`、`pathCache`、`leafCache`：Merkle readiness cache。

镜像可从链上事件和直接 record 读取重建。C++ 客户端通过 readiness endpoint 等待交易确认、镜像重建、路径生成和 root 观察达到一致。

## C++ 布局

- `main_cli.*`：CLI 解析和模式选择。
- `did_app_types.hpp`：客户端流程使用的共享 DTO。
- `app_crypto.*`、`url_utils.*`、`csv_utils.*`、`app_paths.*`、`identity_state.*`：应用层 helper。
- `text_utils.*`、`json_utils.*`、`hex_utils.*`、`normalize_utils.*`、`process_utils.*`：共享工具层。
- `http_transport.*`、`http_client.*`：HTTP 传输、bulletin-board client 和 readiness polling。
- `committee_client.*`：委员会节点客户端。
- `zk_auth_flow.*`：ZK auth 和 ZK recovery 编排。
- `ttss_flow.*`、`ttss_setup_flow.*`、`ttss_rotate_flow.*`：TTSS 初始化、恢复和轮换。
- `ttss_artifacts.*`、`ttss_meta_registrar.*`：TTSS 输出和 metadata anchor helper。
- `trace_flow.*`、`ttss_trace.*`：trace、trace verification 和 trace publish。
- `zk_backend.*`、`zk_paths.*`、`zk_runner.*`、`zk_public_signals.*`、`verifier_wrap.*`：witness/proof runner 和 verifier 集成。
- `merkle_poseidon.*`：Poseidon 和 Merkle helper。

## 核心流程

### 注册

1. C++ 客户端派生本地 demo 身份材料：`sid`、`rho`、普通密钥 seed、恢复密钥 seed、`cid`、`pkNormHash` 和 `pkRecHash`。
2. `POST /registerZk` 提交 `idHash`、`cid`、密钥哈希和 demo controller seed。
3. bulletin-board 服务调用合约 `registerZK`，镜像记录，重建活跃 root，并更新 `rootEpoch`。
4. 客户端等待 `/readySnapshot`、`/leaf`、`/path` 和 `/root` 达到一致。

### ZK 认证

1. 客户端获取当前 root、leaf 和 Merkle path。
2. 生成 `auth_membership.circom` 所需 witness input。
3. 调用 Groth16 proof backend，得到 `proof.json` 和 public signals。
4. 把 proof bundle 发送给 `zk_verify_service.js`。
5. verifier 检查 public signal 与 root、epoch、context、session 的绑定，并验证 Groth16 proof。

### ZK 恢复

1. 客户端先验证旧身份状态在恢复后不再通过认证。
2. 使用恢复控制器调用 `applyRecoveryRotateZk`。
3. 恢复路径同时轮换普通控制器和恢复控制器。
4. 服务重建 root，客户端等待新 path，并验证新 proof 成功。

### TTSS 初始化和恢复

1. `--ttss_setup` 注册身份状态，并为 `TTSS_N` 个 guardian 生成门限为 `TTSS_T` 的 share envelope。
2. 客户端通过 `/setShareEnvelope` 把签名份额分发到委员会节点。
3. 通过 `/registerTTSSMeta` 锚定 TTSS metadata。
4. `--ttss_recover` 从委员会节点获取活跃份额，只要获得至少 `TTSS_T` 个不同活跃份额就能重构恢复 seed。

### TTSS 恢复并轮换

1. 客户端从活跃委员会份额恢复旧 recovery seed。
2. 提交 recovery rotate，更新身份状态和两个 controller。
3. 失效旧委员会份额。
4. 生成并分发新活跃份额。
5. 为新身份版本和 root epoch 锚定新的 TTSS metadata。

### Trace Flow

1. trace 实验在 `pirate_box.js` 中加载或模拟泄露份额。
2. `--ttss_trace` 收集 honest challenge shares 和 leaked material。
3. trace algorithm 构造 accused set 和 verification result。
4. `--ttss_trace_publish` 通过 `/publishTrace` 锚定 trace hash。

## 运行生命周期

1. `scripts/dev.sh up` 加载 `config/dev.env`。
2. `scripts/start_stop.sh` 启动本地 Hardhat node。
3. 部署脚本部署 `DIDBulletinBoardZK`。
4. 启动 verifier、bulletin-board、pirate box 和委员会节点。
5. `scripts/dev.sh smoke` 运行 TTSS setup，并检查 leaf 与 TTSS metadata readiness。
6. 运行时 URL 和默认值写入 `run/ttss_phase5_env.sh`。

## 实验目录

研究和批量脚本放在 `experiments/`，让稳定的 `scripts/` 目录专注构建和开发栈操作。`experiments/runs/` 保存保留的场景驱动，`experiments/lib/` 保存采集 helper，`experiments/tools/` 保存结果汇总工具。

## 配置入口

native 栈默认读取 `config/dev.env`。设置 `DID_E2E_CONFIG=/path/to/file.env` 可切换配置文件。`config/dev.env` 使用 shell default expansion，因此仍支持环境变量覆盖。
