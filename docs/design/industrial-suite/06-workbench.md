# UniVision Workbench 界面与交互

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## 25  一个工作台，四个模块视图

建议采用统一 UniVision Workbench，而不是首版发布四个几乎相同的桌面程序。模块库与 CLI 仍独立可用；工作台只聚合它们的能力和证据。此页是交互规范，不是已经生成或实现的界面截图。

| 全局元素 | 设计要求 |
| --- | --- |
| 顶部工作区栏 | 当前本机工作区、模式（离线/模拟）、服务状态、活动 run；模式不能只靠颜色区分 |
| 左侧导航 | 设备（能力允许时）、传输、回放、测量、流程、证据、设置 |
| 中央主视图 | 当前模块画布/图像/流程；默认保留足够操作面积 |
| 右侧证据栏 | 选中对象的 ID、版本、来源、单位、有效性、错误与限制 |
| 底部任务栏 | 队列、进度、取消、磁盘预算与诊断入口；保留明确结果状态 |

### 模块之间的跳转

传输接收帧 → 查看录制会话；回放选择截面 → 新建测量 run；测量方法 → 作为固定版本节点插入流程；流程输出 → 打开对应回放位置。跳转携带对象 ID 和版本，不携带不受限绝对路径。

### 状态与交互规范

空态指导导入合成样例；服务不可用时只读浏览已提交制品；完整/部分/无效/模拟状态常驻。不得把预览缩略图当成原始深度，也不得把 raw-device-code 自动贴上 mm 标尺。

建议中性色界面、一种主强调色、文本状态标签与可访问键盘导航；图像伪彩仅用于显示，并保留色标/单位/无效区域说明。窗口在紧凑宽度下将证据栏变为抽屉，不压缩关键结果到不可读。

> UI 的“运行成功”“接收成功”“测量有效”“判定通过”四个状态必须各自命名，不能共用一个绿色勾号。


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
