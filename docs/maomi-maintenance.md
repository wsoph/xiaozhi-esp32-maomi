# 小猫咪 1.0 维护说明

## 代码位置与边界

- 功能分支：`feature/maomi-1.0`
- 板型目录：`main/boards/zhengchen/1.54tft-wifi-maomi/`
- 核心固件使用 ESP-IDF C/C++；Python 只负责构建、资源生成、校验和主机测试。
- `origin` 保留为小智 AI 官方仓库。定制分支应同步到主人自己的私有远程仓库，不应直接推送到官方仓库。

## 关键资料

- 后台人设：`docs/maomi-persona.md`
- 语言游戏：`docs/maomi-voice-games.md`
- 倒计时显示与抢占提醒：`docs/maomi-countdown-presentation-spec.md`
- 板型资源清单：`main/boards/zhengchen/1.54tft-wifi-maomi/assets-manifest.json`
- 真机交付应同时保存完整刷写分区、`flasher_args.json`、哈希清单和对应 Git 标签。

## 验证命令

使用 ESP-IDF 6.0.2。完整主机测试：

```powershell
python -m unittest discover -s scripts/tests -v
```

板型构建：

```powershell
python scripts/build.py zhengchen/1.54tft-wifi-maomi --name xiaozhi-1.54tft-wifi-maomi
```

## 本地输出

- `build/`、`build-*`、`bo/`、`releases/` 是本地生成内容，不进入普通源码提交。
- 不使用 `reset`、`clean` 或 `stash` 处理主人已有修改。
- 不手工修改 ESP-IDF 生成文件；资源源文件、生成脚本和校验规则应一起提交。

## 发布与恢复

1. 确认完整测试通过并成功构建。
2. 保存 bootloader、分区表、OTA 初始数据、Assets 和应用五个分区。
3. 保存 `flash_args`、`flasher_args.json`、SHA-256 清单、Git commit 和版本标签。
4. 真机刷写必须获得主人当次明确授权。
5. 出现启动、显示、音频或联网回归时，使用最近一次已验证的完整发布包恢复，不只写入单个应用分区。
