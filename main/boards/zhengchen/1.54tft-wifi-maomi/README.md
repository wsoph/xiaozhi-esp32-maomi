# 小猫咪 1.54 TFT Wi-Fi 定制板型

该目录为“小猫咪”桌面宠物固件保留独立的板型和 OTA 身份：
`zhengchen-1.54tft-wifi-maomi`。它不会覆盖官方原板型
`zhengchen-1.54tft-wifi`。

## 硬件基线

本板型以 Zhengchen 1.54-inch TFT Wi-Fi（ESP32-S3）为硬件基线。
`config.h` 中的音频采样率、I2S、三个按键、显示屏和背光参数均与原板型保持一致。

## 当前开发阶段

T01 仅注册独立的 `config.json`、Kconfig 和 CMake 选择链，不包含板型实现文件，
也不包含唤醒词、语言或桌面宠物行为。板型实现将在 T02 单独加入并验证。

在 T02 完成前，本板型只用于运行板型发现和配置链静态检查，不应生成或刷写真机固件。

```powershell
python scripts/build.py --list-boards
```

预期列表中同时存在：

- `zhengchen/1.54tft-wifi`
- `zhengchen/1.54tft-wifi-maomi`
