# 小猫咪语音互动游戏 MVP 任务

## Task 1: 产品与接口文档

**Acceptance criteria:**
- [x] 七种游戏均有玩法说明
- [x] 前三种标为 MVP，后四种明确延期
- [x] 记录回合边界、退出方式、亲密度和动画规则

**Verification:**
- [x] `docs/maomi-voice-games.md` 覆盖已确认需求

**Dependencies:** None

## Task 2: MCP 契约测试

**Acceptance criteria:**
- [x] schema 只接受三种已实现游戏
- [x] 输出包含游戏、回合上限和亲密度结果
- [x] 未知值、不可用及队列拒绝均失败

**Verification:**
- [x] 测试在实现前失败
- [x] 实现后聚焦测试通过

**Dependencies:** Task 1

## Task 3: 游戏工具实现

**Acceptance criteria:**
- [x] 注册 `self.pet.start_game`
- [x] 返回每种游戏对应的稳定规则和回合上限
- [x] 不修改 `self.pet.interact` 的既有行为

**Verification:**
- [x] `python -m unittest scripts.tests.test_maomi_tools -v`

**Dependencies:** Task 2

## Task 4: 板级开局接线

**Acceptance criteria:**
- [x] 成功开局复用 `HandleMaomiInteraction(kPlay)`
- [x] 每局开局只触发一次设备亲密度记录
- [x] 开局播放现有玩耍动画且不播放独立猫叫

**Verification:**
- [x] 板级静态契约测试通过

**Dependencies:** Task 3

## Task 5: 最终验证与提交

**Acceptance criteria:**
- [x] 聚焦及全量主机测试通过
- [x] C/C++ 排版已按 100 列规则人工检查；当前环境无 `clang-format`
- [x] 固件编译成功
- [x] 差异仅包含本功能文件，未跟踪构建目录保持原样

**Verification:**
- [x] 记录测试与构建结果
- [x] 检查暂存差异中无密钥和无生成目录

**Dependencies:** Tasks 3–4

---

# 倒计时抢占提醒与逐秒显示任务

## Task C1: 行为契约测试

**Acceptance criteria:**
- [x] 覆盖最先到期倒计时、毫秒向上取整、相同时间按 ID 选择和无倒计时隐藏
- [x] 覆盖聆听与说话均不进入忙延期，并执行不同的会话终止动作
- [x] 覆盖提醒声音因说话尾帧暂不可播时的后续重试

**Verification:**
- [x] 新测试在实现前失败

**Dependencies:** None

## Task C2: 到点抢占语音会话

**Acceptance criteria:**
- [x] 聆听中到点停止语音上传和本轮聆听
- [x] 说话中到点中止回复、清空播放并切回待命
- [x] 动画立即提交，声音在播放空闲后可靠启动

**Verification:**
- [x] 板级契约测试通过
- [x] 既有提醒调度 C++ 测试通过

**Dependencies:** Task C1

## Task C3: 逐秒倒计时图层

**Acceptance criteria:**
- [x] 创建成功后立即显示初始秒数
- [x] 秒数按向上取整逐秒递减，到点显示 0
- [x] 独立图层不覆盖 AI 对话字幕；取消或结束后隐藏/切换

**Verification:**
- [x] 倒计时视图 C++ 测试通过
- [x] LCD 和板级静态契约测试通过

**Dependencies:** Task C1

## Task C4: 最终验证与提交

**Acceptance criteria:**
- [x] 聚焦及全量主机测试通过
- [x] ESP-IDF 6.0.2 固件编译成功
- [x] 差异只包含本功能；未跟踪构建目录保持原样
- [x] 未刷机、未推送、未合并

**Verification:**
- [x] `git diff --check` 通过
- [x] 记录测试、构建和固件校验值

**Dependencies:** Tasks C2–C3

---

# 语音互动游戏扩展任务

## Task G1: 游戏契约

**Acceptance criteria:**
- [x] `no_yes_no` 不再出现在工具说明中并被明确拒绝
- [x] 故事接龙、猫咪小侦探、记忆旅行箱和快问快答均有稳定标识及结束边界
- [x] 猫咪猜心和三分钟冒险保持可用

**Verification:**
- [x] 新契约测试在实现前失败
- [x] 实现后聚焦契约测试通过

## Task G2: 最终验证

**Acceptance criteria:**
- [x] 全量主机测试通过
- [x] C/C++ 格式及差异检查通过（环境无 `clang-format`；由 `git diff --check`、人工复核和 `-Werror` 编译覆盖）
- [x] ESP-IDF 固件编译成功
- [x] 未刷机、未推送、未合并，现有构建目录原样保留

---

# 小猫咪专注计时与唤醒直达聆听任务

## Task FT1: 倒计时暂停、继续和冻结剩余时间

**Acceptance criteria:**
- [x] 为指定倒计时提供 pause/resume，非法状态返回明确失败
- [x] 暂停期间剩余时间不变，恢复后从冻结值继续，到零仍产生原有提醒事件
- [x] Snapshot/List 能真实报告 paused 和冻结秒数，不改变其他提醒类型

**Verification:**
- [x] 新测试在实现前失败
- [x] `maomi_reminders_clock_test` 与压力测试通过

**Dependencies:** None

**Files likely touched:** `maomi_reminders.h/.cc`、`maomi_reminders_clock_test.cc`

## Task FT2: 正计时单调时钟状态机

**Acceptance criteria:**
- [x] 从零开始，支持 pause/resume/reset/stop，暂停时间不计入 elapsed
- [x] 单调时钟回退不导致下溢，显示在 `99:59:59` 饱和且不回绕
- [x] 不持久化、不分配无界内存

**Verification:**
- [x] 新测试在实现前失败
- [x] 新正计时主机测试通过

**Dependencies:** None

**Files likely touched:** `maomi_timing.h/.cc`、`maomi_timing_test.cc`、`test_maomi_host_cpp.py`

## Task WL1: 应用层无欢迎语直接聆听入口

**Acceptance criteria:**
- [x] idle 状态可打开音频通道并进入默认 listening mode
- [x] 不发送 wake-word-detected，不播放 popup，不触发服务器欢迎语路径
- [x] 失败时返回 idle 并恢复唤醒检测，其他板型旧入口不变

**Verification:**
- [x] 音频契约测试先失败后通过
- [x] application 相关编译检查通过

**Dependencies:** None

**Files likely touched:** `main/application.h`、`main/application.cc`、`test_maomi_audio_contract.py`

## Task WL2: 猫叫完成后直达聆听并移除唤醒表情

**Acceptance criteria:**
- [x] 仅匹配的猫叫播放完成票据可启动直接聆听
- [x] 不再调用官方 wake-word 请求，也不提交 `Event::UserWake()` 表情
- [x] 超时、失败、重复唤醒和非 idle 状态仍安全恢复

**Verification:**
- [x] `maomi_wake_test` 新旧场景通过
- [x] 板型音频静态契约通过

**Dependencies:** Task WL1

**Files likely touched:** `maomi_wake.h/.cc`、`maomi_wake_test.cc`、Maomi 板型主文件、`test_maomi_audio_contract.py`

## Task FC1: 计时 MCP 契约

**Acceptance criteria:**
- [x] 注册 `self.timer.control`，只接受 pause/resume/stop/reset
- [x] 注册 `self.stopwatch.start`，所有响应包含真实 kind/state/time
- [x] 无活动计时、重复启动和对倒计时 reset 均明确失败

**Verification:**
- [x] MCP 契约测试先失败后通过
- [x] 既有十个 Maomi 工具契约不回归

**Dependencies:** Tasks FT1–FT2

**Files likely touched:** `maomi_tools.h/.cc`、`test_maomi_tools.py`

## Task FC2: 倒计时专注待命和恢复

**Acceptance criteria:**
- [x] 创建成功立即退出 listening/speaking，保留本地唤醒并抑制自主喵叫
- [x] 暂停、继续、停止只作用于唯一前台倒计时
- [x] 到零播放原有提醒并退出专注；后台提醒优先级不变

**Verification:**
- [x] 板型契约覆盖会话终止、焦点互斥和恢复
- [x] 提醒引擎及 MCP 聚焦测试通过

**Dependencies:** Tasks FT1, WL2, FC1

**Files likely touched:** Maomi 板型主文件、`test_maomi_tools.py`

## Task SW1: 正计时板型及全屏显示接线

**Acceptance criteria:**
- [x] 正计时启动后进入与倒计时相同的专注待命并逐秒递增
- [x] 暂停显示冻结时间和图形暂停标记；聆听期间隐藏，idle 后恢复
- [x] 停止退出专注，reset 保留原运行/暂停状态并归零

**Verification:**
- [x] LCD 静态契约和板型接线测试通过
- [x] 正计时纯逻辑及 MCP 契约测试通过

**Dependencies:** Tasks FT2, WL2, FC1, FC2

**Files likely touched:** `maomi_lcd_display.h`、Maomi 板型主文件、`test_maomi_tools.py`

## Task V1: 文档与最终验证

**Acceptance criteria:**
- [x] 板型 README 说明专注倒计时、暂停/继续、正计时和新唤醒行为
- [x] 全量主机测试、格式检查及 ESP-IDF 6.0.2 Maomi 构建结果如实记录
- [x] 真机验证结论和授权状态明确记录

**Verification:**
- [x] `python -m unittest discover -s scripts/tests -v`
- [x] `clang-format --dry-run -Werror <touched-files>`（工具可用时）
- [x] `python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi`
- [x] `git diff --check` 与最终差异审查

**Dependencies:** Tasks FT1–SW1

**Files likely touched:** Maomi 板型 `README.md`、`tasks/plan.md`、`tasks/todo.md`

---

# 双击摸摸语音与普通陪玩收敛任务

## Task PV1: 交互契约

**Acceptance criteria:**
- [x] 双击音量上键仍触发摸摸动画和亲密度
- [x] 互动成功后播放“摸摸好舒服呀！”本地短语音
- [x] `self.pet.interact` 拒绝 `play`，只保留 `pet` 和 `feed`
- [x] 普通“陪我玩”不调用互动或游戏工具
- [x] 六种真实语音游戏仍在开局时播放玩耍动画并记录亲密度

**Verification:**
- [x] 新契约测试在实现前失败
- [x] 工具与资源聚焦测试通过
- [x] 完整主机测试通过（123 项）
- [x] ESP-IDF 6.0.2 固件构建通过
- [x] 真机验收语音音色、音量和播放时机
