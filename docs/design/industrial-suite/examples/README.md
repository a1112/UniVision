# 自建合成契约样例

本目录不包含客户数据或硬件标定证据。

- `data/frame-mono8.raw`：8×8、64 字节递增灰度数据。
- `data/profile-circle.csv`：72 个半径 10 mm 的理想圆点，单位仅为合成几何定义。
- 8 个 JSON：对应 8 个契约草案；哈希引用均可在本目录解析。

`recording-manifest.json` 明确使用 `synthetic-fixture-v1`，不是 MCAP 文件。`run-manifest.json` 是手工组织的结果清单样例，不说明产品执行器已经实现或运行。`stream-receipt.json` 也不是实际网络确认。

标定状态是 draft，测量状态是 synthetic，生产判定为 not-evaluated。即使样例数学校验通过，也不得据此宣称 0.03 mm 精度、相机认证或四路网络吞吐已通过。

运行 `python ../tools/validate_design_examples.py` 可检查结构、部分语义、文件摘要及合成几何；脚本不访问硬件和网络。
