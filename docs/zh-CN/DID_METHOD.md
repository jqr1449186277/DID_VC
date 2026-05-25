# DID Method 说明

本仓库当前没有定义已注册的 W3C DID Method。它实现的是一组身份状态、认证、恢复和追踪机制，这些机制可以作为未来 DID Method 的设计基础，但还缺少 resolver、method specification 和 VC 生命周期。

因此当前项目应描述为 DID-like 原型，而不是完整 DID 生态。

## 实验性 Method 形态

未来可能的 DID 形式：

```text
did:didzk:<idHash>
```

其中 `<idHash>` 是 bulletin-board 合约跟踪的 32 字节身份哈希。

当前实现允许本地 API 使用人类可读 demo ID，并在本地服务状态中映射到 `idHash`。真实 DID Method 需要定义确定性的 identifier 生成、编码、冲突处理和 resolver 行为。

## 当前状态模型

合约以 `idHash` 为 key 保存身份记录：

- `cid`：ZK leaf 使用的 commitment 或 DID pointer。
- `owner`：普通更新 controller address。
- `recovery`：恢复 controller address。
- `version`：单调递增身份版本。
- `active`：该记录是否进入当前活跃 root。
- `pkNormHash`：普通公钥哈希的 field 表示。
- `pkRecHash`：恢复公钥哈希的 field 表示。

bulletin-board 服务维护链下 Poseidon-Merkle mirror，并把 active root 和 epoch 锚定到链上。

leaf 公式：

```text
leaf = Poseidon(cid, pkNormHash, pkRecHash, ver, active)
```

## 操作映射

当前实现可以映射到未来 DID Method 的概念：

| DID 概念 | 当前实现 |
| --- | --- |
| Create | `registerZK`、`POST /registerZk` |
| Read | 合约 `records`、`GET /record`、`GET /leaf`、`GET /path`、`GET /root` |
| Update | `applyUpdateZK`、`POST /applyUpdateZk` |
| Recover | `applyRecoveryRotateZK`、`POST /applyRecoveryRotateZk` |
| Attach recovery metadata | `setTTSSMeta`、`POST /registerTTSSMeta` |
| Publish trace anchor | `publishTraceResult`、`POST /publishTrace` |
| Deactivate | 未实现 |
| Resolve DID Document | 未实现 |

## Create

Create 写入版本为 `0` 的新身份记录。

输入包括：

- `idHash`
- `cid`
- 普通和恢复密钥哈希
- 普通和恢复 controller address

本地服务从本地 seed 派生 demo controller address。生产级 DID Method 应接入钱包或 controller 管理系统，而不是在本地服务中保存 demo seed。

## Read

当前 read 包括：

- 链上 `records(idHash)`
- bulletin-board `/record`
- bulletin-board `/leaf`
- bulletin-board `/path`
- bulletin-board `/root`

这些能力足够支撑实验和 proof 生成，但不是 W3C resolver。当前没有 DID URL dereferencing，也不输出 DID Document。

## Update

普通更新需要 normal controller。它改变：

- `cid`
- `pkNormHash`
- `pkRecHash`
- `version`

它不改变 `owner` 或 `recovery`。

## Recover

恢复需要 recovery controller，并同时轮换普通和恢复 controller。它改变：

- `cid`
- `pkNormHash`
- `pkRecHash`
- `owner`
- `recovery`
- `version`

同时轮换两个 controller 是有意设计。如果只轮换普通 controller，泄露过的 recovery controller 仍可继续接管未来身份状态。

## TTSS Recovery Metadata

TTSS 层是实验性的恢复机制。它把签名 share envelope 分散存储在委员会节点，并只把 metadata hash 锚定到链上：

- `vkSetHash`
- `metaHash`
- identity version
- root epoch

链上不保存原始 share。恢复轮换成功后，委员会节点会把旧份额标记为 inactive。

## Trace Anchor

trace 实验可以发布：

- `accusedSetHash`
- `proofHash`
- `traceDigest`
- identity version
- root epoch

anchor 表示某个身份状态和 epoch 下产生了一个 trace result。它不包含完整 dispute、governance 或惩罚流程。

## DID Document 投影

未来 resolver 可以把当前 record 投影为类似 DID Document：

```json
{
  "@context": ["https://www.w3.org/ns/did/v1"],
  "id": "did:didzk:0x...",
  "controller": "did:didzk:0x...",
  "verificationMethod": [
    {
      "id": "did:didzk:0x...#normal",
      "type": "Multikey",
      "controller": "did:didzk:0x...",
      "publicKeyMultibase": "<derived-or-published-key-material>"
    },
    {
      "id": "did:didzk:0x...#recovery",
      "type": "Multikey",
      "controller": "did:didzk:0x...",
      "publicKeyMultibase": "<derived-or-published-key-material>"
    }
  ],
  "authentication": ["did:didzk:0x...#normal"],
  "assertionMethod": ["did:didzk:0x...#normal"]
}
```

当前实现无法输出该文档，因为它只保存内部 key hash 和 controller address，没有保存 W3C verification method 所需材料。

## 与 ZK Authentication 的关系

ZK 电路证明身份属于当前活跃身份集合，但不暴露私有身份 secret。

public input 包括：

- active root
- context hash
- session public-key hash
- epoch

private witness 包括：

- `sid`
- `rho`
- normal/recovery key hash
- version
- Merkle path

public output 当前包括：

- `nullifier`
- `bindHash`
- `leaf`

这是一种可支撑 DID Method 的认证机制，但它本身不是 DID Method。

## W3C DID 和 VC 缺口

尚未实现：

- 正式 DID Method specification。
- Method registration。
- DID resolver。
- DID URL dereferencing。
- W3C-conformant DID Document output。
- DID deactivation operation 和 metadata。
- service endpoint privacy policy。
- internal key hash 之外的 verification method material。
- Verifiable Credential issuance。
- Verifiable Presentation verification。
- credential status。
- wallet storage。
- 生产密钥管理和恢复策略。

## 设计状态

当前项目最准确的描述是：

- 本地 identity-state 和 proof 原型。
- DID Method 设计基础。
- ZK auth、TTSS recovery 和 trace anchoring 的实验 harness。

在补齐 resolver、DID Document、deactivation、VC 和治理流程之前，不应把它描述成完整 W3C DID Method 或 VC 实现。
