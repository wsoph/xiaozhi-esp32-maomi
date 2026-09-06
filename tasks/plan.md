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

# Implementation Plan: 语音互动游戏扩展

## Overview

根据真机体验移除“不许说是/不是”，并把已记录的故事接龙、猫咪小侦探、记忆旅行箱和快问快答加入现有 `self.pet.start_game` 工具。

## Architecture Decisions

- 继续使用单一开局工具和联网 AI 对话状态，不在设备端增加六套状态机。
- 六种游戏共享一次玩耍动画和一次亲密度记录，游戏后续回合不重复调用设备工具。
- 每款游戏在返回指令里写明 1–3 分钟内可执行的硬性结束边界。
- 删除 `no_yes_no` 的枚举、解析、工具描述和返回指令，旧标识明确报错。

## Task List

- [x] 用契约测试定义四种新增游戏和旧游戏删除行为
- [x] 确认测试在现有实现上失败
- [x] 扩展游戏枚举、解析、回合上限和联网 AI 指令
- [x] 更新语音游戏产品及接口文档
- [x] 运行聚焦及全量主机测试
- [x] 运行格式检查和 ESP-IDF 固件编译
- [x] 复核差异并记录结果；不刷机、不推送、不合并

## Verification Results

- 2026-08-29：新增契约测试先因缺少四种新游戏枚举而失败，随后实现通过。
- `python -m unittest scripts.tests.test_maomi_tools -v`：9 项通过。
- `python -m unittest discover -s scripts/tests -v`：114 项通过。
- ESP-IDF 6.0.2 `ninja -C build-m`：成功，应用分区剩余 28%。
- 固件大小：2,975,056 字节；SHA256：`3A648C6A01B0D61EB74DC57A64972D3F55AC84B86DA3FB5ACCAAA1CC9E0D4BA2`。
- 当前环境没有 `clang-format`；`git diff --check` 通过，固件以 `-Werror` 编译通过。
- 未修改正式倒计时界面；未刷机、未提交、未推送、未合并。

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

---

# Implementation Plan: 小猫咪专注计时与唤醒直达聆听

## Overview

实现已批准的 `timer-session-core`、`wake-to-listen`、`countdown-focus` 和 `stopwatch`
四项能力。倒计时或正计时运行时，设备退出当前语音会话并保持本地唤醒检测；用户明确
唤醒后，猫叫结束即进入聆听。倒计时支持暂停、继续和停止，正计时另支持归零。

## Architecture Decisions

- 保留 `ReminderEngine` 作为倒计时到期和提醒优先级的唯一事实源，只增加倒计时暂停/
  继续状态；不迁移现有闹钟、周期提醒和番茄钟。
- 新增轻量、无分配的正计时策略，使用单调时钟计算累计时间，不按轮询次数累加，避免漂移。
- 前台计时互斥在 Maomi 板型适配层执行：现有多个倒计时的底层容量不删除，但产品入口只允许
  一个前台倒计时或正计时，避免语音控制目标和 1.54 英寸屏幕归属不明确。
- 新增 `self.timer.control`，通过 `pause`、`resume`、`stop`、`reset` 控制当前前台计时；新增
  `self.stopwatch.start`。已有 `self.timer.start_countdown` 和 `self.reminder.cancel` 保持兼容。
- 为 `Application` 增加一个仅由明确调用者使用的“直接进入默认聆听”入口：负责打开音频通道并
  发送 listen/start，但不发送 wake-word-detected、不请求服务器欢迎语、不播放额外 popup 音。
- Maomi 唤醒状态机在精确猫叫播放票据完成并排空输出后调用直接聆听入口；不再提交会生成
  `curious` 表情的 `Event::UserWake()`，但仍向自主行为控制器报告唤醒活动。
- 专注待命由板型层结束 listening/speaking、恢复本地唤醒检测并抑制自主动画/喵叫；闹钟和周期
  提醒继续按现有优先级抢占。MCP 工具只在计时状态真实改变后返回成功。
- 计时图层扩展为统一运行/暂停视图：大号 `HH:MM:SS`，暂停时用两个 LVGL 矩形绘制暂停标记；
  聆听期间暂时隐藏计时层，回到待命后恢复。
- 开发阶段遵守未授权不刷机、不提交、不推送、不合并的边界；完成构建后，用户已明确授权刷机，
  并在真机试用通过后授权提交、推送和合并。

## Dependency Order

1. 倒计时暂停/继续和正计时纯逻辑。
2. 应用层直接聆听入口及 Maomi 唤醒状态机。
3. MCP 契约、专注待命和屏幕接线。
4. 全量回归、固件构建和真机待验证清单。

## Task List

### Phase 1: Timing foundation

- [x] Task FT1: 倒计时暂停、继续和冻结剩余时间
- [x] Task FT2: 正计时单调时钟状态机

### Checkpoint: Timing foundation

- [x] 新增行为测试先失败后通过
- [x] 既有闹钟、周期提醒、番茄钟测试不回归

### Phase 2: Wake directly to listening

- [x] Task WL1: 应用层无欢迎语直接聆听入口
- [x] Task WL2: 猫叫完成后直达聆听并移除唤醒表情

### Checkpoint: Wake flow

- [x] 成功、播放失败、超时、重复唤醒和联网失败路径均有测试
- [x] 非 Maomi 板型的既有唤醒行为不变

### Phase 3: Focus integration

- [x] Task FC1: 计时 MCP 契约
- [x] Task FC2: 倒计时专注待命和恢复
- [x] Task SW1: 正计时板型及全屏显示接线

### Checkpoint: End-to-end contract

- [x] 创建、暂停、继续、停止、归零的工具结果与设备状态一致
- [x] 专注时只保留本地唤醒，聆听结束后恢复计时画面
- [x] 后台闹钟和周期提醒仍能抢占

### Phase 4: Verification and documentation

- [x] Task V1: 更新板型文档并执行最终验证

### Checkpoint: Complete

- [x] 聚焦及全量主机测试通过
- [x] 触及的 C/C++ 文件格式检查通过
- [x] ESP-IDF 6.0.2 Maomi 固件构建成功
- [x] 真机验证通过，用户确认可以提交、推送和合并

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| 结束语音会话与 MCP 返回发生竞争 | 工具实际成功但服务器收不到结果，或仍播报一句话 | 保留文本协议通道，使用现有主任务调度顺序；增加板型契约测试，并把无额外 TTS 列为真机检查 |
| 猫叫播放完成回调与超时竞争 | 重复进入聆听或无法恢复唤醒 | 沿用精确 playback id、单 pending 操作和两秒冷却，扩展现有 wake 压力测试 |
| 暂停期间单调时钟继续前进 | 恢复后时间跳变 | 暂停时保存剩余/累计毫秒，恢复时重新建立基准点 |
| 倒计时与正计时争用屏幕 | 显示和语音控制对象不一致 | 板型入口拒绝第二个前台计时，所有控制只作用于唯一前台会话 |
| 计时层遮住聆听反馈 | 用户无法判断麦克风是否已开启 | listening/connecting 时隐藏计时层，回到 idle 后恢复 |
| 构建环境或主机编译器不可用 | 无法执行完整验证 | 使用可用的 ESP-IDF 6.0.2 与主机编译器执行测试和构建，并如实记录结果 |

## Open Questions

无。规格由用户于 2026-09-06 确认。

---

# Implementation Plan: 双击摸摸语音与普通陪玩收敛

## Overview

音量上键双击摸摸在动画和亲密度之外播放一句离线语音。取消口头普通陪玩所触发的空玩耍动画和亲密度，仅在真实语音游戏开局时保留玩耍反馈。

## Architecture Decisions

- 双击使用独立的 24 kHz 单声道 Opus 短语音，不依赖联网 TTS。
- 短语音只由双击入口触发；语音 `pet` 互动仍由 AI 自然回应，避免重复声音。
- 用单个 pending 标志在播放忙时延后短语音，不增加无界队列。
- `self.pet.interact` 对外只接受 `pet` 和 `feed`；内部 `kPlay` 仍专用于六种语音游戏开局。
- 只说普通陪玩不调用工具；只说玩游戏时先提供菜单，选定具体游戏后才开局。

## Task List

- [x] 用失败测试锁定双击本地语音和旧 `play` 值拒绝行为
- [x] 新增“摸摸好舒服呀！”离线语音及延后播放逻辑
- [x] 收紧 MCP 工具描述和参数契约，保留游戏开局反馈
- [x] 同步板型、人设和语音游戏文档
- [x] 完整主机回归、格式与差异检查
- [x] ESP-IDF 6.0.2 固件构建
- [x] 真机确认双击动画与语音的同步感、音量和音色

## Safety

- 开发阶段未经授权未执行刷机或 Git 发布操作；最终由用户明确授权刷机，并在验收通过后授权提交、推送和合并。

## Verification Results

- 工具与资源聚焦测试：25 项通过。
- 完整主机测试：123 项通过。
- 新语音资源：1.77 秒、单声道 Opus、Opus 头声明 24 kHz 输入。
- ESP-IDF 6.0.2 构建成功；`xiaozhi.bin` 为 2,982,816 字节，应用分区余量 1,145,952 字节（28%）。
- `merged-binary.bin` 已写入 ESP32-S3，写后哈希校验通过；用户真机试用确认通过。
