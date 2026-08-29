# Implementation Plan: 小猫咪语音互动游戏 MVP

## Overview

新增一个与普通陪玩分离的设备 MCP 开局工具。工具负责选择校验、玩耍动画和一次亲密度记录；联网 AI 根据工具契约完成三种短局语音游戏。

## Architecture Decisions

- 使用一个 `self.pet.start_game` 工具和稳定的字符串游戏标识，避免为每款游戏复制设备逻辑。
- 复用现有 `HandleMaomiInteraction(kPlay)`，保证动画、亲密度、持久化和队列规则与 RC3 一致。
- 回合状态保留在联网 AI 对话上下文，不在设备上复制语音理解或剧情状态机。
- 后续游戏通过新增允许值扩展；本阶段拒绝四个未实现游戏，避免承诺不可用功能。

## Task List

### Phase 1: Contract

- [x] Task 1: 记录七种游戏、MVP 边界和设备/AI 契约
- [x] Task 2: 用主机契约测试定义新工具的输入、输出、错误和板级接线

### Checkpoint: Contract

- [x] 新测试在现有代码上失败，证明测试覆盖的是新增行为

### Phase 2: Implementation

- [x] Task 3: 实现游戏标识解析、响应和 MCP 注册
- [x] Task 4: 将游戏开局连接到现有玩耍动画与亲密度逻辑

### Checkpoint: Implementation

- [x] 聚焦测试通过
- [x] 现有摸摸、喂食和普通玩耍契约不变

### Phase 3: Verification

- [x] Task 5: 运行全量主机测试、格式检查和固件编译
- [x] Task 6: 复核差异、更新任务状态并提交

### Checkpoint: Complete

- [x] 所有自动化测试通过
- [x] 固件构建成功
- [x] 真机刷写仍等待用户另行授权

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| AI 重复调用开局工具 | 一局可能重复动画 | 工具契约明确一局只调用一次；沿用陪玩冷却避免短时重复加分 |
| AI 未按回合数结束 | 游戏拖长 | 工具描述和返回值同时给出硬上限，契约测试锁定 |
| 新描述增大 MCP 工具列表 | 工具分页或发现异常 | 控制描述长度，并在主机测试中检查完整工具 schema |
| 复用陪玩逻辑影响旧互动 | RC3 回归 | 新增独立入口，只复用内部处理函数；保留旧契约测试 |

## Open Questions

无。产品规则已由用户确认。

## Verification Results

- 2026-08-29：先确认新增契约测试在实现前失败，再完成实现。
- `python -m unittest discover -s scripts/tests -v`：111 项通过。
- ESP-IDF 6.0.2 `ninja -C build-m`：成功生成 `xiaozhi.bin`，应用分区剩余 30%。
- 当前环境未安装 `clang-format`；已按仓库 100 列规则检查改动行，`git diff --check` 通过，固件以 `-Werror` 编译通过。
- 未执行刷机。
