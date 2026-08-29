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
