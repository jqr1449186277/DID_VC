# 发布流程

DID E2E 目前处于 `v0.1.x` 实验原型阶段。发布流程重点是保证仓库干净、文档准确、CI 通过、功能边界说明清楚。

## 版本策略

- `v0.1.x`：实验原型，接口和目录结构仍可能调整。
- `v0.2.x`：可在补充 resolver、DID Document 或更完整实验后考虑。
- `v1.0.0`：只有当 DID Method、resolver、VC 边界、测试和安全审计达到稳定要求后才应考虑。

## v0.1.0 发布检查

发布前检查：

- README 明确实验原型、非生产使用、当前支持功能和未支持 W3C DID/VC 内容。
- 中文和英文文档链接有效。
- `SECURITY.md`、`CONTRIBUTING.md`、`CHANGELOG.md` 存在。
- issue/PR 模板存在。
- `.gitignore`、`.dockerignore`、`.gitattributes`、`.editorconfig`、`.clang-format` 存在。
- 生成物未提交。
- CI 通过。
- 本地 smoke 或代表性 E2E 已运行。

## 建议命令

```bash
git status -sb
scripts/check_repo_format.sh
scripts/check_markdown_links.py
scripts/check_js_syntax.sh
scripts/build.sh all
scripts/run_cpp_tests.sh
docker compose config
```

如修改了服务或流程，额外运行：

```bash
scripts/dev.sh up
scripts/dev.sh smoke
scripts/dev.sh down
```

## 打标签

确认工作区干净且 CI 通过后：

```bash
git tag -a v0.1.0 -m "v0.1.0 experimental DID E2E prototype"
git push origin v0.1.0
```

## Release Notes

release notes 应包含：

- 项目是实验原型，非生产使用。
- 支持的核心流程。
- 不支持的 W3C DID/VC 能力。
- 已知风险和安全边界。
- 快速启动方式。
- 重要测试结果或 CI 链接。
