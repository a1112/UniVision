# 参考资料与证据口径

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## 31  参考来源与追溯

在线技术来源于 2026-09-23 核对；[S1] 是本会话此前获得的仓库文件快照，本轮未重新抓取。引用只支持相关事实；本册方案、数值配置、案例与接口是设计建议。

| 编号 | 来源与用途 |
| --- | --- |
| S1 | a1112/UniVision README；已知 0.3 GenApi 基线。<br>https://github.com/a1112/UniVision/blob/main/README.md<br>README Git blob SHA: 898303b541ad7466370957a1582a4b0492d40144；不是 commit。 |
| S2 | EMVA GenICam：GenTL、SFNC、PFNC 与 GenDC 的职责/资料。<br>https://www.emva.org/standards-technology/genicam/introduction-new/<br>https://www.emva.org/standards-technology/genicam/genicam-downloads/ |
| S3 | IETF RFC 9293：TCP 提供有序字节流。<br>https://www.rfc-editor.org/rfc/rfc9293.html |
| S4 | IETF RFC 8446：TLS 1.3 与认证/重放相关安全边界。<br>https://www.rfc-editor.org/rfc/rfc8446.html |
| S5 | IETF RFC 9000：QUIC 传输与流控制。<br>https://www.rfc-editor.org/info/rfc9000/ |
| S6 | MCAP Format Specification：通道、消息、时间、块和索引。<br>https://mcap.dev/spec |
| S7 | NIST：测量可追溯性及 TN 1297 的不确定度表达。<br>https://www.nist.gov/calibrations/traceability<br>https://www.nist.gov/pml/nist-technical-note-1297 |
| S8 | SQLite PRAGMA：synchronous/WAL 的持久化差异。<br>https://sqlite.org/pragma.html |
| S9 | JSON Schema Draft 2020-12：草案结构验证规范。<br>https://json-schema.org/draft/2020-12 |

文档优先保留完整来源路径和适用边界，不复制长篇标准原文。未进行厂商兼容认证、软件安全认证或计量认证。


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
