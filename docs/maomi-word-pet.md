# 单词养成学习版：使用与迁移

本功能的规格见 [已确认规则](maomi-word-pet-spec.md)。当前为待真机验收的开发版本。

## 刷机前先在电脑试玩

Windows 双击 [maomi_simulator.cmd](../scripts/maomi_simulator.cmd)，或运行：

```powershell
python scripts/maomi_simulator.py
```

需要 Python（含 Tkinter）和电脑 C++ 编译器。当前开发电脑已配置；其他电脑可安装
g++ / clang++ 并加入 PATH，或通过 CXX 指定编译器完整路径。首次启动会编译桌面引擎，
缓存位于 `.cache/maomi-simulator/`，源文件变化后自动重新编译。

建议试玩顺序：领养并取名 → 「背单词」完成五词 → 「用品商店」购买 → 快进一天 →
喂两次猫粮、铲一次屎 → 检查照料日和日龄 → 「模拟重启」检查本次存档恢复。
默认带五词示例，也能导入自己的 CSV。用「查看提示」可体验提示后答对的积分差异。

试玩直接运行固件的 `LearningEngine`，复用领养、积分、库存、复习和存档编码；
使用原项目的 240×240 橘猫素材及互动 GIF。操作面板是电脑试玩界面，屏幕字体和动画
时序仅供预览，并非实际 LVGL 画面模拟。电脑端输入答案后由你手动判定，不调用在线语音 AI。

模拟时间只由快进按钮推进。「模拟重启」会重新读取本次试玩内存中的存档；
关闭窗口或「重新试玩」会清空试玩记录。它不连接串口，也不改动设备数据或固件。
这能检查玩法与经济流程，不能代替麦克风、联网对话、屏幕驱动、Flash 掉电和电源的真机测试。

2026-09-17：新增三项桌面流程用例，全量 136 项主机测试通过；已检查窗口启动、
实际照料/学习画面以及隐藏窗口交互测试（`--smoke-test`）。

## 构建和第一次安装

学习版沿用原硬件引脚和一个板工厂，使用独立 OTA variant：
`zhengchen-1.54tft-wifi-maomi-learning`。资源分区为 7 MiB，尾部 1 MiB 为学习存档。
原版应用分区位置、网络 NVS 和提醒存储位置不变。

在 ESP-IDF 6.0.2 环境运行：

```powershell
python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi-learning
python scripts/maomi_migrate.py --port COM5 --build build --backup .cache/maomi-original-16m.bin
```

第二条命令只检查产物并打印操作，不连接设备。实际安装时，在确认连接的是目标设备后加
`--apply`：先完整备份 16 MiB、记录 SHA-256，再确认旧分区布局，然后清空新增存储区域、
写入固件与分区表并校验。不会覆盖已有备份，遇到已经安装学习版的分区拒绝再次初始化。
使用实际串口号替换 COM5。安装后重新上电。

不能通过普通应用 OTA 完成第一次迁移。以后学习版 OTA 必须匹配学习版 variant；
不向原版设备推送学习版资源。原版资源可能覆盖新存档，禁止混用资源包。

恢复原机：

```powershell
python scripts/maomi_migrate.py --port COM5 --backup .cache/maomi-original-16m.bin --restore
```

同样默认仅检查；加 `--apply` 才完整恢复、校验。恢复会回到备份时的所有数据。
完整备份可能包含 Wi-Fi 等设备配置，保持本地，不提交到 Git。

## 领养和照料

说“领养猫咪”，为现有橘猫取名并确认；领取六份猫粮、三份猫砂。
领养时出生，年龄从 0 天开始，满 24 小时增长一天。旧亲密度和陪伴记录不继承，
亲密度积分及等级已移除。名字和生日确认后不能再次领养覆盖。

可说“看看猫咪状态”“买两份猫粮”“喂猫粮”“给零食”“铲屎”“摸摸”。
购买和使用分开：食品和猫砂进入背包，再由照料动作消耗。
饱食、清洁和心情影响即时表现；累计照料 7/21 天解锁成长称号。
没来照顾不会死亡或离家，年龄正常增加。

设备保存未知版本或损坏数据时会拒绝改写，不能通过重领养覆盖；使用备份恢复。
存档写入报错后暂停继续写入；重新上电并查询状态，以实际保存的数据为准。

## 导入自己的单词

电脑安装 Python（含 Tkinter），运行：

```powershell
python -m pip install -r scripts/requirements-maomi-import.txt
python scripts/maomi_import.py
```

选择 [CSV 模板](../scripts/maomi-word-template.csv) 格式的文件和猫咪串口，检查后上传。
用 Excel 保存时选择 CSV UTF-8。关闭串口监视器，使用支持数据传输的 USB 线。
上传过程中不要开始学习或操作猫咪；设备必须已联网校时，导入本身不走云端。

```powershell
python scripts/maomi_import.py --check scripts/maomi-word-template.csv
python scripts/maomi_import.py --port COM5 --csv scripts/maomi-word-template.csv
```

仅接受两列 `word,meaning`，最多 1000 词；英文不超过32字符，可含内部连字符/撇号，
释义不超过120个UTF-8字节，近义解释可用分号隔开。错误、空项和重复词必须先改正。
活动词表替换后，同词进度保留；释义改变重新复习，但当天奖励不会重置。
历史最多保存2000个不同英文词，满后拒绝新增，仍可导入已有词。不自动删除历史以免刷币。
拔线或校验失败时旧词表继续有效。若保存成功的回复丢失，先重新查询状态，不直接重做消费。

## 背单词赚猫粮

说“开始背单词”或“赚猫粮”。默认听英文答中文，也可说“听中文答英文”。
每局最多五词，优先选到期复习词，再按 CSV 顺序选新词；不足五词就练实际数量，
不拿未到期或已掌握词补题。答错后简短讲解，初轮题目之后再纠正，最多十个答题回合。
首次独立答对后隔 1 天复习，再答对隔 3 天复习；连续三次间隔独立答对即掌握，退出日常出题。
答错或依赖提示清零连续次数，约 24 小时后再复习；同局纠正不算独立掌握。
没有待学词时提示等待，全部掌握后提示换词表；说“复习已掌握的词”可主动抽查，
抽查答错或依赖提示重新进入待巩固，奖励仍受同词每天一次和日上限约束。
状态查询提供未学、待巩固、已掌握数量，重启及重导相同词义保留进度。
单词页不显示词表数量和进度统计，底部提示真实聆听状态，只有录音处理开启时才显示“正在聆听，请回答”。
新词和复习都先出题，不提前示范答案：屏幕显示英文单词，
猫咪读英文后问“这个是什么意思？”，然后等待作答。长词缩小显示，必要时换行，不替换成 Listen。
答错、说“不会”或主动求助后才解释中文意思；提示后答对不计为独立答对。
中文出题模式不提前在屏幕泄露英文答案。

当天完成五词可得10币；每词首次独立答对另加2，提示后加1；当天最多40币。
答错后认真完成纠正可计入每日目标，跳过/沉默/识别不清不计入。重复同词不重复领奖。
两份猫粮和一份猫砂共10币，额外收入可买零食。

“不学了”结束练习。断网暂停语音，下次查询当前题恢复；提醒和计时优先，不重复结算。
AI 判断表达意思，不提供专业发音分数。录音不存入学习存档。

## MCP 和后台人设

将 [人设中的学习版规则](maomi-persona.md) 同步到当前使用的 AI 服务端。
设备注册新工具并附带规则，后台人设仍应同步，避免旧人设承诺免费喂食或亲密度。
如果后台已配置“新词先示范单词与释义，再回忆”，请删除这条旧规则，并同步新的先问后答规则。
学习状态的 `question_prompt` 是可朗读的题目；`question_instruction` 说明等待作答和隐藏答案的要求。
`word`/`meaning` 供 AI 出题与判分，不能在用户作答前把两者一起念出。

- `self.pet.life`：名字、生日、日龄、心情、成长、钱包、背包、当前题、revision。
- `self.pet.adopt / buy / care`：领养、购买、照料；传入最近 revision。
- `self.pet.shop`：静态价格。`self.pet.operation(id)`：后台操作结果。
- `self.learning.start(mode,revision,review_mastered=false)`、`answer(session,question,verdict)`、`stop(revision)`。
  答案按局号和题号防重放，不要求全局 revision；旧客户端传入的 revision 兼容。
  当前题返回 `answer_tool/answer_context/answer_instruction`，必须按其提交答案，确认 completed 才问下一词。
  pending 返回 `next_tool/next_arguments` 指明查询操作结果；不会自行换题或计分。
- 状态新增 `unlearned_count/consolidating_count/mastered_count`，三者之和为当前词表数量；
  `due_count` 是待巩固中已到期数量，`next_review` 是最早待巩固时间戳（无则为 0）。
  `no_words_due/all_mastered/no_mastered_words` 分别表示等待复习、词表已掌握、无可主动复习的掌握词。
- 变更先返回 `pending` 与 operation_id，随后查结果。只有 `completed` 才播报成功；
  `stale_revision/stale_question` 不自动重新结算，应查询状态。重启后查询已保存状态，旧operation_id失效。
- USB使用115200串口、`@ML1 `前缀加一行JSON；仅允许status和词表上传，不开放加币、重置或任意Flash操作。
  状态的 `answer_attempts_since_boot/last_answer_error` 用于区分“AI未调用作答工具”和“提交被拒绝”，
  只保留本次开机的计数及错误，不记录用户语音或答案文本。

两个网络协议共用上述设备MCP工具；玩法不依赖某一种传输。

## 验证

2026-09-17：133 项主机测试通过；另通过桌面导入器启动检查、CSV 模板校验、
迁移工具只读演练和已修改 C/C++ 文件的格式检查。主机用例包含 1000 词导入、
2000 词历史上限、跨日和时钟回拨、重复奖励、词表切换写入失败及损坏存档拒写。

ESP-IDF v6.0.2 下，原版和学习版均使用 `scripts/build.py` 完成构建、链接和打包，
两次最终命令均退出 0。学习版应用为 3,037,584 字节，应用分区余量 26%；
资源为 4,574,867 字节，满足 7 MiB 分区及预留空间要求。
本地最终 `build/` 为学习版，另保存了一份 `.cache/maomi-learning-artifacts/`；
迁移工具的 `--build` 也可指向该目录。以上检查均未连接或刷写设备。

```powershell
python -m unittest discover -s scripts/tests -v
python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi
python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi-learning
```

2026-09-18：原机 16 MiB 备份及设备摘要校验通过；首次迁移完成，五个镜像校验通过，
原 NVS 保留，启动、联网校时和 CH340 USB 状态查询正常。
备份、刷写校验与启动记录保存在 `.cache/maomi-backup-20260918-081911/`。

同日根据试玩修正“新词先示范答案”的规则，固件与模拟器使用统一的先问后答话术。
137 项主机测试、模拟器启动检查和 ESP-IDF 6.0.2 学习版构建通过；修复版保存在
`.cache/maomi-question-first-artifacts/`；该单独版本未刷写，修复已并入下述掌握规则版本。远端后台人设未同步。
已有学习存档的设备不能再次用首次迁移流程清空尾部存储；本次修复只需更新应用。

同日追加掌握规则：141 项主机测试、模拟器启动与格式检查、ESP-IDF 6.0.2 canonical
学习版构建通过；包含先问后答和掌握规则的最终产物为 `.cache/maomi-mastery-artifacts/`，
应用 3,040,608 字节。保留原存档格式，旧 stage 3..5 视为已掌握，释义变更仍重置该词进度。
已通过 COM5 刷入 ota_0，仅写主程序。当前系统配置与尾部词表/宠物存档已备份，
刷写前后分区 MD5 一致，应用镜像校验通过；重启后联网校时及状态查询正常，无启动崩溃。
领养、名字、生日、积分、背包、照料天数及 99 词保持一致；新版进度为未学 90、待巩固 9、已掌握 0。
备份、刷写验证与启动记录位于 `.cache/maomi-mastery-update-20260918-093247/`。
实际语音出题/判分与屏幕展示仍需用户试玩，远端人设未同步。

二次试玩修正：USB 证实 stamp 仍为当前第 1 题，后续答案未成功推进设备进度。
修复当前题被旧全局 revision 拒绝的路径，保留局号/题号防重放；强化每题提交和 pending 查询规则，
增加只含调用次数与错误的诊断。单词页移除统计，新增真实聆听提示、题目发布即时刷新和长词换行。
142 项主机测试、模拟器启动及格式检查、ESP-IDF 6.0.2 学习版构建通过。
产物 `.cache/maomi-listening-artifacts/`（应用 3,042,800 字节）已刷入，应用及存档校验通过，
但启动时主题更换字体后崩溃，验收失败。证据保存在 `.cache/maomi-listening-update-20260918-100424/`。
已修正提示与长词的字体生命周期：继承屏幕字体，避免持有被资源加载替换并释放的字体指针。
修正版 `.cache/maomi-listening-font-artifacts/`（应用 3,042,640 字节）已通过 142 项主机测试与 ESP-IDF 6.0.2 构建，
再次手动 BOOT 后已通过 COM5 刷入，仅更新 ota_0 应用；应用及存档摘要校验通过。
启动加载字体、联网校时和状态查询正常，随后连续观察 45.1 秒无崩溃或额外重启。
猫咪、积分、背包、99 词及当前 stamp 学习局均保留；记录在 `.cache/maomi-listening-font-update-20260918-101911/`。
2026-09-18 用户实机试玩确认“背单词功能正常了”，本轮背词故障修复验收通过；远端人设未同步。

真机专项待验收：原生 USB、拔线与掉电、喂食音画、长词显示等边界场景、
提醒抢占恢复、备份恢复，以及连续七天的学习时长与收入支出。编译通过不等于这些项目已通过。
