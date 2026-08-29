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

---

# Implementation Plan: 倒计时抢占提醒与逐秒显示

## Overview

修复倒计时在聆听和说话状态被延迟的问题，并增加不覆盖对话字幕的逐秒倒计时图层。

## Architecture Decisions

- 提醒引擎继续负责到期与非对话忙状态延期；板级策略把聆听、说话识别为可抢占状态。
- 板级代码先结束当前语音会话，再提交提醒表情；如说话音频仍有尾帧，保留声音待播并在播放空闲后重试。
- 从提醒列表生成纯数据倒计时视图，选择最先到期项并将毫秒向上取整为秒。
- LCD 使用独立 LVGL 图层显示倒计时，避免与 AI 字幕互相覆盖。

## Task List

### Phase 1: Contract

- [x] Task C1: 添加倒计时视图选择和板级抢占失败测试

### Phase 2: Runtime

- [x] Task C2: 实现聆听/说话抢占和提醒声音可靠启动
- [x] Task C3: 实现逐秒倒计时视图和 LCD 独立图层

### Phase 3: Verification

- [x] Task C4: 运行聚焦、全量主机测试和 ESP-IDF 构建
- [x] Task C5: 复核、记录结果并提交；不刷机、不推送、不合并

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| 说话音频重置时仍有尾帧 | 提醒声音首次启动失败 | 将声音保留为待播，在后续 100 ms 轮询中启动 |
| 多倒计时争用一个图层 | 显示跳动或选择不确定 | 固定选择剩余时间最短、再按 ID 排序 |
| 对话字幕更新覆盖倒计时 | 用户看不到递减 | 使用独立 LVGL 对象，不调用 `SetChatMessage` |
| 非对话系统状态被误中断 | 配网或升级异常 | 只抢占 listening/speaking，其他状态沿用忙延期 |

## Open Questions

无。详细行为见 `docs/maomi-countdown-presentation-spec.md`。

## Verification Results

- 2026-08-29：新增测试先在缺少倒计时视图和抢占策略时失败，随后实现通过。
- `python -m unittest discover -s scripts/tests -v`：114 项通过。
- ESP-IDF 6.0.2 `ninja -C build-m`：成功生成 `xiaozhi.bin`，应用分区剩余 30%。
- 固件大小：2,875,776 字节；SHA256：`39D72DEC2E7D323B32BFD9386B0E9325A434D27EACA776BB1C554DD0E33C1DD1`。
- `git diff --check` 通过；未刷机、未推送、未合并。
