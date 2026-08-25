# 小猫咪 1.54 TFT Wi-Fi 定制板型

该目录为“小猫咪”桌面宠物固件保留独立的板型和 OTA 身份：
`zhengchen-1.54tft-wifi-maomi`。它不会覆盖官方原板型
`zhengchen-1.54tft-wifi`。

## 硬件基线

本板型以 Zhengchen 1.54-inch TFT Wi-Fi（ESP32-S3）为硬件基线。
`config.h` 中的音频采样率、I2S、三个按键、显示屏和背光参数均与原板型保持一致。

## 当前实现

该板型已有独立的板级实现、资源清单和 OTA 身份，可单独编译，不会改变原版
`zhengchen/1.54tft-wifi` 的工厂函数或运行行为。

当前定制功能包括“小猫咪”桌面宠物状态管理、猫咪表情资源，以及“猫咪过来”
唤醒后的本地回应流程。自定义回应音频会先完整校验；校验或非阻塞入队失败时，
回退到官方提示音，再继续官方对话流程。

主机测试覆盖桌面宠物核心、唤醒状态机、语音上传门、音频播放限制和 OGG/Opus
完整性检查。真机刷写前仍应重新构建本板型，并保留现有整机备份作为恢复路径。

```powershell
python scripts/build.py --list-boards
```

板型列表中应同时存在：

- `zhengchen/1.54tft-wifi`
- `zhengchen/1.54tft-wifi-maomi`
