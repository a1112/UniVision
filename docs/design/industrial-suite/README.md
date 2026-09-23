# UniVision 工业视觉扩展设计文档包

**v0.1 / 2026-09-23 / 待评审设计基线**

一个仓库、四个可选模块、一套共享契约。Core 保持轻量，默认构建不依赖这些扩展。

## 文档目录

- [总体架构与仓库落地](00-overview.md)
- [共享数据与服务契约](01-contracts.md)
- [UniStream 传输模块设计](02-unistream.md)
- [VisionReplay 回放与对比设计](03-vision-replay.md)
- [UniMeasure 测量验证设计](04-unimeasure.md)
- [InspectFlow 离线流程设计](05-inspect-flow.md)
- [UniVision Workbench 界面与交互](06-workbench.md)
- [分阶段实施计划](07-delivery-plan.md)
- [验收矩阵与性能口径](08-acceptance.md)
- [架构决策与待复核事项](09-decisions.md)
- [参考资料与证据口径](10-references.md)

## 工程附录

- [建议工作项与依赖](implementation-backlog.md)
- [8 个 Schema 草案](contracts/README.md)
- [合成契约样例](examples/README.md)
- [本地验证脚本](tools/validate_design_examples.py)
- [本次验证结果](validation-report.json)

## 核查边界

此前会话读取的 README 描述 0.3 GenApi Feature Milestone。文件 blob SHA 为 `898303b541ad7466370957a1582a4b0492d40144`，不是仓库提交 SHA。本轮未成功获取最新 HEAD，目录和接口名称均为新增提案；合并实现前必须完成 M0。

本包没有向 GitHub 创建分支、提交、PR 或修改文件。它也没有运行 UniVision Core、网络传输、MCAP 录制、真实相机或物理测量验收。

## 可复核检查

在已准备 Python 3.10+、`jsonschema` 4.x 与 `referencing` 的环境中执行：

```bash
python tools/validate_design_examples.py --report validation-report.json
```

脚本只读本地草案和小型合成数据；可选写入所指定的 JSON 报告，不安装依赖、不访问网络、不调用相机。验证覆盖 8 个 Schema、8 个正例、16 个预期拒绝反例和一个合成几何一致性探针。不能把这份结果当作产品功能测试。

## 入库建议

将本目录作为新增文档目录审查后放入 `docs/design/industrial-suite/`；若目标路径已有内容，先逐文件比较，不直接覆盖。不要把 `modules/` 目录提案当作已存在路径。项目许可证及新增依赖许可需由维护者在 M0 核对；本包不替源仓库设置许可。
