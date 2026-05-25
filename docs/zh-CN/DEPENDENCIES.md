# 依赖说明

本文档说明本地构建、运行和测试 DID E2E 所需的主要依赖。

## Native 系统包

Ubuntu/Debian 常用安装命令：

```bash
sudo apt-get update
sudo apt-get install -y build-essential curl jq nodejs npm python3 libsodium-dev libgmp-dev nlohmann-json3-dev
```

不同发行版包名可能不同。核心要求是 C++17 编译器、Node.js/npm、Python 3、libsodium、GMP 和 nlohmann-json。

## C++ 库

C++ 侧主要依赖：

- C++17 标准库。
- `libsodium`：哈希、随机数或签名相关 helper。
- `GMP`：大整数/field 相关处理。
- `nlohmann::json`：JSON 序列化和解析。

项目已经尽量把通用逻辑收敛到工具层，新增代码应复用已有 C++ helper。

## Node.js 包

Hardhat 目录管理智能合约和本地服务依赖：

```bash
cd hardhat
npm install
```

CI 使用：

```bash
npm ci
```

主要用途：

- Hardhat 本地链和合约编译/部署。
- ethers 合约调用。
- Express HTTP 服务。
- snarkjs/Groth16 相关 proof verify 或 wrapper。

## Circuit 和 ZK 工具

ZK authentication 使用 Circom/Groth16 相关产物。运行 proof flow 需要：

- circuit 源文件。
- 编译后的 wasm/zkey/vkey 等产物。
- snarkjs 或对应 wrapper。
- C++ 侧能找到当前项目根、circuit 根和 Poseidon helper。

如果移动目录结构，需要同步更新 `zk_paths`、脚本和 CI。

## Docker

Docker Compose 提供最简单的开源试运行入口：

```bash
docker compose up --build
```

CI 会检查 compose 配置是否有效。远端或本地机器没有 Docker 时，可以使用 native 栈运行。

## License 备注

引入新依赖前请确认许可证适合开源发布，并在必要时更新文档。生产使用前还需要完整第三方依赖审计。
