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
