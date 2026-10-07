# Simulator ROI

Simulator 通过现有 `FeatureAccess` 和 C ABI 暴露 ROI，无须新增虚函数、C 函数或公共结构字段。
本功能仅验证合成相机，不代表 Generic GenTL 或任何厂商设备的 ROI/触发认证。

## 参数与边界

虚拟传感器固定为 16384 × 16384 像素。`SimulatorConfiguration::width/height`
仍表示初始输出尺寸，默认 640 × 480；初始偏移为零。

| 节点 | 类型 | 范围与访问 |
| --- | --- | --- |
| `SensorWidth`、`SensorHeight` | integer，px | 只读，16384 |
| `Width` | integer，px | 16 到 `SensorWidth - OffsetX`，步进 1 |
| `Height` | integer，px | 1 到 `SensorHeight - OffsetY`，步进 1 |
| `OffsetX` | integer，px | 0 到 `SensorWidth - Width`，步进 1 |
| `OffsetY` | integer，px | 0 到 `SensorHeight - Height`，步进 1 |

节点使用标准名称，`standard_feature` 为 true；`features()` 和 `feature_info()`
返回当前动态上限。采集时四个 ROI 节点报告 `read_only`，写入返回 `invalid_state`。
传感器尺寸始终只读，写入返回 `access_denied`。类型不匹配或超出边界返回
`invalid_argument`，失败写入不改变任何 ROI 参数。

每次单节点写入都检查完整矩形。参数不是批量事务：放大图像前先减小相应偏移；
增加偏移前先减小相应尺寸。调用方必须先停止采集，再按所需顺序更新四个节点。

```cpp
camera->write_feature("OffsetX", std::int64_t{0});
camera->write_feature("OffsetY", std::int64_t{0});
camera->write_feature("Width", std::int64_t{320});
camera->write_feature("Height", std::int64_t{240});
camera->write_feature("OffsetX", std::int64_t{100});
camera->write_feature("OffsetY", std::int64_t{50});
```

实际应用须检查每次返回的 `Status`。C 调用对应 `uv_camera_set_integer` 和
`uv_camera_get_integer`，现有 `uv_camera_get_feature_info` 可读取动态范围。

## 采集与线程约定

- 每次 `Stream::start()` 在共享锁内快照当前 ROI 和帧率，并将相机转为 `streaming`。
  在 `create_stream()` 之后修改参数，或者停止后重新启动同一流，都使用最新参数。
- ROI 写入、相机状态变化和流启动使用同一相机锁。竞争的写入要么在启动之前完整生效，
  要么以 `invalid_state` 失败，不会产生半更新的单节点状态。
- 同一相机至多允许一个流采集；未启动流的 `stop()` 不改变其他运行流的相机状态。
- 流内帧计数、时间和统计受锁保护；停止会唤醒等待中的读取，返回 `invalid_state`。
  停止并立即重启也不会把旧的等待请求带入新采集周期。
- 输出是紧密排列的 Host Mono8 图像。像素值为
  `(OffsetX + x + OffsetY + y + frame_id) & 255`，保持零偏移时原有测试图案。
  帧 metadata 包含实际 `OffsetX` 和 `OffsetY`；帧共享 ownership 不随停止或参数编辑失效。
- 流重启继续原有 `frame_id` 与累计统计。`TriggerMode/TriggerSoftware` 行为未在本切片扩展；
  本功能不声称软件/硬件触发已实现。

创建相机时会校验初始尺寸，以及有限且位于 0.1 到 1000 Hz 的初始帧率。
非法配置通过 `Adapter::create_camera()` 返回 `invalid_argument`，不建立相机。
浮点 feature 拒绝 NaN 和无穷，避免无效帧周期进入等待路径。C ABI 保留其零值表示默认值的已有配置转换。

## 离线回归

```bash
cmake -S . -B build -DUNIVISION_BUILD_TESTS=ON -DUNIVISION_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`univision_simulator_roi_tests` 覆盖动态范围、准确边界、错误类型/极值/NaN、
逐像素裁剪、保留帧、启动/重启快照、并发写入和竞争流；`univision_c_api_tests`
覆盖 C ABI 的 ROI 节点、动态上限、运行中写保护和裁剪首像素。
这些测试仅使用 Simulator 和现有 Fake CTI，不连接真实工业设备。
