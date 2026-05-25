# 威胁模型

DID E2E 是本地实验原型。威胁模型用于说明当前实验考虑哪些风险、不考虑哪些风险，以及为什么不能用于生产。

## 资产

实验中需要保护或正确绑定的对象包括：

- 身份 secret：`sid`、`rho`、普通和恢复 key seed。
- 链上身份记录：`idHash`、`cid`、controller、version、active 状态。
- Poseidon-Merkle root、leaf、path 和 epoch。
- ZK witness、proof 和 public signals。
- TTSS share envelope、metadata hash 和 active/inactive 状态。
- trace leaked material、accused set 和 trace anchor。
- 本地配置、token、日志和生成物。

## 信任组件

本地实验默认信任：

- 本机文件系统和开发者账户。
- 本地 Hardhat chain。
- 本地 Node.js 服务。
- 本地 C++ CLI 和委员会节点进程。
- 本地 ZK proving/verifying toolchain。

这些组件没有隔离成生产多租户服务，也没有完整访问控制和审计。

## 基本假设

- 攻击者不能完全控制运行实验的本机。
- 本地 demo token 不用于生产认证。
- 委员会节点和 bulletin-board 服务运行在本地开发网络。
- 生成的 proof、witness、share 和日志不承载真实身份材料。
- 实验关注流程正确性，而不是公网环境对抗。

## 各流程信任边界

注册流程依赖 bulletin-board 服务正确提交链上交易并重建 mirror。

ZK authentication 依赖 circuit、witness input、public signals 绑定和 verifier 服务正确执行。

ZK recovery 依赖 recovery controller 未被错误复用，并且恢复后 root/path 与新版本一致。

TTSS recovery 依赖至少 `TTSS_T` 个有效、不同、活跃的委员会份额，以及旧份额在轮换后失效。

Trace flow 依赖 leaked material 与 challenge shares 的实验模型正确，并且 anchor 绑定身份版本和 root epoch。

## 实验范围内威胁

项目适合研究：

- 身份状态是否能通过 root/epoch 正确锚定。
- ZK proof 是否绑定当前 root、context 和 session。
- 恢复轮换后旧身份状态是否失效。
- TTSS 份额是否只在 active metadata 下可用于恢复。
- 轮换后旧份额是否失效。
- trace result 是否能锚定到指定身份版本和 epoch。

## 范围外威胁

当前不覆盖：

- 公网 DoS。
- 多租户隔离。
- 生产私钥保护。
- 硬件钱包或 HSM。
- 链上治理、争议和惩罚。
- DID resolver 的可用性和缓存一致性。
- VC 签发、撤销、状态检查和钱包保护。
- 法规合规、PII 数据治理和审计。

## 隐私与匿名边界

ZK auth 目标是在认证时证明成员资格，而不直接暴露私有身份 secret。但当前系统仍可能在本地服务、日志、demo ID、生成路径、trace 实验材料中泄露关联信息。

因此当前匿名性只应理解为“电路和流程层面的实验属性”，不是完整隐私系统承诺。

## 已知风险集中点

- 本地服务保存 demo seed，便于实验但不适合生产。
- TTSS share 存储在本地委员会节点，没有生产级密钥保护。
- trace helper 故意保存泄露材料，用于实验。
- 部分 API 是 pre-1.0，本身不是稳定外部协议。
- 本地链没有真实网络安全假设。

## 生产警告

不要把本项目用于真实身份、真实凭证、真实资金、生产密钥或受监管个人数据。若要走向生产，需要补 resolver、DID Document、VC、访问控制、密钥管理、安全审计、监控、部署加固和隐私设计。
