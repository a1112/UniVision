# 分阶段实施计划

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## 26  分阶段落地与依赖顺序

| 阶段 | 交付 | 退出条件 |
| --- | --- | --- |
| M0 基线复核 | 读取真实 HEAD/目录/C ABI/CMake/许可，形成映射；冻结契约草案 | Core-only 基线可复建；待复核项有结论 |
| M1 离线数据基线 | contracts、合成样例、recording reader/writer、最小 CLI | 结构/语义校验、录制/读取与恢复测试通过 |
| M2 传输与回放 | UniStream TCP/TLS loopback→两机、VisionReplay 引擎 | 确认等级、幂等、压力、缺口和回放可验证 |
| M3 单项测量 | 截面圆拟合、标定模型、预算与报告 | 合成数学测试和无效输入拒绝；实测另列 |
| M4 离线流程/工作台 | 类型化 DAG、统一 UI、证据串联 | CLI/UI 同契约、取消/故障/版本冻结验证 |
| M5 集成与发布候选 | 核心兼容、安装消费、离线包、实机与数据许可核对 | 目标平台/场景逐项签收，不外推到生产 |
| 后续提案 | 实时节点、厂商认证、其他测量任务、QUIC | 单独设计/权限/现场验收，不自动纳入 v0.1 |

M2 的网络与回放可以在 M1 后并行；M3 可独立离线推进。M4 的编辑器原型可先做，但产品执行器必须消费已经稳定的契约。不要同时重构 Core 和四个模块。

### 每个阶段的 PR 原则

小范围、单职责、附正反例和回滚说明。新增默认关闭的扩展，不通过“整个仓库强制装新工具链”完成集成。文档和 Schema 变更要有版本差异说明；真实硬件验收不被 CI 绿灯替代。

仓库内文档使用“拟议/已实现/已验证/已签收”四种状态。本包所有功能均是拟议，附带脚本的通过只证明其明确检查的样例与规则。


## 关联文档

[文档索引](README.md) · [共享契约](01-contracts.md) · [验收矩阵](08-acceptance.md) · [参考来源](10-references.md)

[S1]: https://github.com/a1112/UniVision/blob/main/README.md
[S2]: https://www.emva.org/standards-technology/genicam/introduction-new/
[S3]: https://www.rfc-editor.org/rfc/rfc9293.html
[S4]: https://www.rfc-editor.org/rfc/rfc8446.html
[S5]: https://www.rfc-editor.org/info/rfc9000/
[S6]: https://mcap.dev/spec
[S7]: https://www.nist.gov/pml/nist-technical-note-1297
[S8]: https://sqlite.org/pragma.html
[S9]: https://json-schema.org/draft/2020-12
