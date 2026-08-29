# Spec: 小猫咪倒计时抢占提醒与逐秒显示

## Objective

倒计时设置成功后，屏幕立即显示剩余秒数并逐秒递减；倒计时到点时，即使设备正在聆听或说话，也要终止当前语音会话，优先播放倒计时动画和声音。

## Behavior

- 倒计时创建成功后立即全屏显示 `HH:MM:SS`，例如 `00:00:10`。
- 屏幕秒数使用剩余毫秒向上取整，因此从设置值开始按 `N、N-1、...、1、0` 显示，不会提前跳秒。
- 倒计时图层独立于 AI 对话字幕；普通状态、聆听中和说话中均可见。
- 同时存在多个倒计时时，显示剩余时间最短的一个；相同剩余时间按较小提醒 ID 选择。
- 当前倒计时取消或结束后，切换到下一个倒计时；没有倒计时时隐藏图层。
- 倒计时到点后显示 `00:00:00`，并在四秒提醒动画结束后隐藏或切换到下一个倒计时。
- 到点时若正在聆听，停止本轮聆听和语音上传，再播放本地提醒。
- 到点时若正在说话，中止 AI 说话、清空原播放音频并切回待命，再播放本地提醒。
- 被倒计时中断的聆听或说话不会自动恢复；提醒后设备回到正常待命流程。

## Approved Visual Design

- 采用用户在 2026-08-29 确认的 B 方案“无耳橘猫脸边框”。
- 240×240 全屏白底，时间使用高对比度黑色大字并始终位于最上层。
- 顶部使用淡橘色额头和四条深橘色花纹，两侧使用橘色脸颊与胡须，底部使用粉色鼻子和深色嘴巴。
- 不绘制耳朵，机器人橘色外壳提供实体耳朵；不显示网络、电量图标或 AI 字幕。
- 所有装饰均为创建界面时一次生成的静态 LVGL 图元；逐秒刷新只更新倒计时文本。

## Existing Safety Rules Retained

- 低电量时仍可显示动画，但沿用现有规则不播放提醒声音。
- 过热等关键系统告警仍优先于倒计时，并沿用现有静音规则。
- 配网、升级、激活、音频测试等非对话忙状态仍不被倒计时强行中断。

## Tech Stack and Structure

- C++17 / ESP-IDF 6.0.2。
- 调度与视图选择：`main/boards/zhengchen/1.54tft-wifi-maomi/maomi_reminders.*`。
- 板级会话抢占和刷新：`main/boards/zhengchen/1.54tft-wifi-maomi/zhengchen-1.54tft-wifi-maomi.cc`。
- LVGL 独立图层：`main/boards/zhengchen/1.54tft-wifi-maomi/maomi_lcd_display.h`。
- 主机回归测试：`scripts/tests/maomi_reminders_clock_test.cc`、`scripts/tests/test_maomi_tools.py`。

## Commands

- 聚焦 Python 契约测试：`python -m unittest scripts.tests.test_maomi_tools -v`
- 聚焦 C++ 主机测试：`python -m unittest scripts.tests.test_maomi_host_cpp.MaomiHostCppTest.test_reminders_and_clock_contract -v`
- 全量主机测试：`python -m unittest discover -s scripts/tests -v`
- 固件构建：加载 ESP-IDF 6.0.2 PowerShell 环境后运行 `ninja -C build-m`

## Code Style

```cpp
const uint32_t remaining_seconds =
    static_cast<uint32_t>((remaining_ms + 999) / 1000);
```

- 遵循仓库现有 C++ 命名和 100 列限制。
- 仅在数值发生变化时刷新 LVGL 文本，避免 100 ms 轮询造成无效重绘。

## Testing Strategy

- 纯 C++ 测试锁定倒计时选择、毫秒向上取整、切换和隐藏规则。
- Python 板级契约测试锁定聆听/说话抢占、声音重试以及独立图层接线。
- 全量主机测试防止提醒、宠物状态、工具契约和既有互动回归。
- ESP-IDF 编译验证真实类型、LVGL API 和固件链接。

## Boundaries

- Always：先复现失败测试；保留全部未跟踪构建目录；完成后运行全量测试与固件构建。
- Ask first：改变低电量/过热规则、改变其他提醒类型、刷机、推送或合并。
- Never：使用 `reset`、`clean`、`stash`；删除或覆盖用户现有修改；自动恢复被中断的 AI 会话。

## Success Criteria

1. 设置 10 秒倒计时后，屏幕可观察到 `00:00:10、00:00:09、...、00:00:01、00:00:00` 的递减。
2. 聆听中到点会停止聆听，并立即显示提醒动画、播放提醒声音。
3. 说话中到点会停止原回复，并立即显示提醒动画、播放提醒声音。
4. 待命状态的既有倒计时提醒行为保持正常。
5. 多倒计时、取消、低电量、过热和其他官方忙状态遵守上述边界。

## Open Questions

无。以上行为由用户本轮要求和现有安全策略共同确定。
