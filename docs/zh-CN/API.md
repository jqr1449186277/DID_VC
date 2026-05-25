# API 说明

DID E2E 暴露的 HTTP API 面向本地开发、实验和 smoke 测试，不面向公网生产部署。主要客户端是 C++ CLI；API 文档的作用是帮助开发者理解设计、调试流程或增加实验。

## 响应格式

bulletin-board 服务当前存在两类响应：

- JSON：读取接口、TTSS metadata、trace publish 和调试接口。
- 分号分隔的 key/value 文本：早期注册、更新、恢复接口，仍被 C++ 客户端使用。

文本响应示例：

```text
ok=1;accepted=1;opId=op_abc;status=ACCEPTED;ready=0;txHash=0x...
```

API 仍处于 pre-1.0 阶段。字段名对仓库内脚本足够稳定，但还不是第三方兼容性契约。

## Bulletin Board Service

默认地址：`http://127.0.0.1:3000`

职责：

- 向本地链提交身份交易。
- 维护本地 Poseidon-Merkle mirror。
- 提供 active root、leaf、path、record 和 readiness 视图。
- 锚定 TTSS metadata 和 trace result。
- 跟踪异步操作 readiness。

### `GET /health`

返回服务 readiness 和基础运行时信息。

```bash
curl -fsS http://127.0.0.1:3000/health
```

### `GET /root`

返回服务当前知道的活跃 Merkle root 和 epoch。

重要字段：

- `root`：`0x` 前缀的 32 字节 hex。
- `epoch`：root epoch。
- `depth`：配置的 Merkle depth。

### `GET /record`

按 demo ID 或 `idHash` 查询身份记录。

```bash
curl 'http://127.0.0.1:3000/record?id=demo'
curl 'http://127.0.0.1:3000/record?idHash=0x...'
```

返回记录镜像合约状态：`cid`、`owner`、`recovery`、`version`、`active`、`pkNormHash`、`pkRecHash`。

### `GET /leaf`

返回某个 ID 的活跃 leaf 材料。leaf 计算：

```text
leaf = Poseidon(cid, pkNormHash, pkRecHash, ver, active)
```

C++ 客户端用该接口比较本地 leaf 计算结果和服务端 mirror。

### `GET /path`

返回某个 ID 在当前活跃 root 下的 Merkle inclusion path。

典型字段：

- `root`
- `epoch`
- `leaf`
- `pathElements`
- `pathIndex`

### `GET /readySnapshot`

一次返回 record、leaf、path 和 root readiness。

常用参数：

- `id`：demo ID。
- `idHash`：32 字节身份哈希。
- `minVersion`：更新或恢复流程等待的目标版本。

C++ 客户端用该接口避免交易结算、镜像重建、路径生成和 root 观察之间的竞态。

### `GET /registerStatus`

按 `opId` 查询异步操作状态。注册和恢复接口接受异步工作时会返回 `opId`。

重要字段：

- `status`
- `accepted`
- `ready`
- `currentVersion`
- `currentRoot`
- `currentEpoch`
- `lastError`

### `POST /registerZk`

注册一个新的 DID-like 身份状态。

代表性请求体：

```json
{
  "id": "demo",
  "cidHex": "0x...",
  "pkNormHash": "0x...",
  "pkRecHash": "0x...",
  "ownerSeedHex": "0x...",
  "recoverySeedHex": "0x...",
  "wait": 1,
  "confirmations": 1,
  "includeSnapshot": 1
}
```

实现备注：

- 服务从 seed hex 派生 demo owner/recovery wallet。
- 合约存储 controller address，不存储 seed。
- 服务把 seed 保存在本地 demo state 中，方便后续流程无需外部钱包即可运行。
- TTSS setup 需要挂接 metadata 时，`ttssSetupMode` 可为 `deferred`、`inline` 或 `off`。

### `POST /applyUpdateZk`

使用普通 controller 执行身份更新。通常更新 `cid`、`pkNormHash`、`pkRecHash` 和 `version`，但不更换 owner/recovery controller。

适用于普通身份状态变更，不适用于 controller 泄露后的恢复。

### `POST /applyRecoveryRotateZk`

使用 recovery controller 执行恢复并轮换。它会同时轮换普通 controller 和 recovery controller，避免泄露过的 recovery controller 继续拥有未来接管能力。

### `POST /registerTTSSMeta`

为当前身份版本注册 TTSS metadata anchor。链上保存 metadata hash，不保存原始份额。

典型字段：

- `id`
- `idHash`
- `version`
- `rootEpoch`
- `vkSetHash`
- `metaHash`

### `POST /applyRecoveryRotateTTSS`

TTSS recovery/rotate 流程使用的恢复入口。实际恢复 seed 来自委员会份额重构，链上仍锚定新身份状态。

### `GET /ttssMeta`

查询某个身份的 TTSS metadata 本地视图和链上锚定状态。

### `POST /publishTrace`

发布 trace result anchor。

典型字段：

- `id`
- `idHash`
- `version`
- `rootEpoch`
- `accusedSetHash`
- `proofHash`
- `traceDigest`

该接口只锚定 trace 结果摘要，不提供完整治理、争议处理或惩罚机制。

### `POST /recomputeRoot`

强制服务重建本地 Merkle root。主要用于调试和实验，不是普通客户端路径。

## Committee Node

默认端口：`8001..8006`

委员会节点保存签名后的 TTSS share envelope，并根据恢复或 trace 请求返回活跃份额。

### `GET /health`

返回节点健康状态和基础信息。

### `POST /setShareEnvelope`

写入一个 share envelope。客户端在 TTSS setup 或 rotate 后调用该接口分发新份额。

### `GET /shareMeta`

查询节点上某个身份的份额元数据，包括版本、epoch、是否活跃等。

### `POST /shareForRecover`

返回恢复用份额。节点应只返回当前活跃 metadata 对应的有效份额。

### `POST /shareForTrace`

返回 trace challenge 使用的份额材料。

### `POST /invalidateShares`

恢复轮换成功后失效旧份额。

### `GET /debugListShares`

调试接口，列出节点保存的份额摘要。只应用于本地开发。

## Verifier Service

`zk_verify_service.js` 封装 Groth16 proof verification。它检查 proof、本地 verification key、public signals，以及 root、epoch、context、session 等绑定关系。

该服务不等同于 DID resolver，也不负责身份生命周期管理。

## Trace Helper

`pirate_box.js` 为实验保存或模拟泄露份额材料。trace flow 会结合 honest challenge share 和 leaked material 生成 accused set，并可把结果哈希发布到 bulletin-board。

## C++ 入口

常用 CLI 入口：

```bash
./build/did_demo_zk --zk_auth_e2e --id demo_auth --runs 1 --workdir results/examples
./build/did_demo_zk --ttss_setup --id demo_ttss --bb "$BASE_URL" --committee_urls "$COMMITTEE_URLS" --committee_token "$TOKEN" --ttss_n "$TTSS_N" --ttss_t "$TTSS_T" --workdir results/examples
./build/did_demo_zk --ttss_recover --id demo_ttss --bb "$BASE_URL" --committee_urls "$COMMITTEE_URLS" --committee_token "$TOKEN" --ttss_t "$TTSS_T" --workdir results/examples
```
