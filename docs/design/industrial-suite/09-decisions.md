# 架构决策与待复核事项

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## 30  决策记录与待复核清单

| ADR | 本次拟议决策 | 重新评审的触发条件 |
| --- | --- | --- |
| 01 | 单仓库、Core 无反向依赖 | 扩展导致默认构建或 ABI 变重 |
| 02 | 唯一共享帧/制品契约 | 厂商多平面格式或新单位无法表达 |
| 03 | TCP/TLS 首发，QUIC 后续 | 明确实测瓶颈与维护能力支持切换 |
| 04 | MCAP + 会话清单 | 实测记录吞吐/恢复要求无法达到 |
| 05 | 首个测量是离线截面直径 | 独立需求证明另一任务更有价值 |
| 06 | Flow 首版无真实设备动作 | 新的权限/调度/现场评审全部具备 |
| 07 | 一个可选 Workbench | 独立用户任务与发行边界需要拆分 |
| 08 | 持久化确认与处理确认分离 | 任何调用方混淆状态造成错误回收 |

### M0 必须查清

真实仓库 HEAD、Core 目录与 ABI 版本、Frame 实际所有权、CMake 导出目标、现有依赖/许可、测试覆盖、支持平台、线程/回调模型，以及是否已有录制/回放实现。不能以本文的提议覆盖已存在的同类实现。

### 容量与业务待定

真实持续行频、像素打包/深度范围、四路触发关系、可接受丢帧策略、断网保留时长、网卡与磁盘能力、JPEG 是否允许进入算法、第一项测量任务的定义/限值，以及独立参考数据来源。

### 资料包的校验范围

附带脚本只检查自建草案的 Schema、部分跨字段规则、引用摘要、DAG 和合成圆数学回归；不连接网络、不访问相机、不运行任意节点。Schema 是设计原型，不能直接当作已认证 wire parser。

任何“未取得最新仓库内容”的限制都不会被包装成已完成源码审计。导入文档可先完成；实现 PR 必须以复核后的真实代码为准。


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
