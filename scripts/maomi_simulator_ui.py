"""Tk desktop presentation for the real firmware learning engine."""

import datetime
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.font import Font


ERRORS = {
    'already_healthy': '现在很健康，不需要治疗。',
    'not_thirsty': '饮水已经充足。',
    'already_stocked': '基础用品充足，今天的补给机会保留。',
    'supplies_claimed': '今天已经领取过补给，明天再来。',
    'pet_sleeping': '猫咪正在睡觉，可以叫它起床，或开始背词继续原题。',
    'growth_locked': '还未达到这件用品所需的照料天数。',
    'already_owned': '已经拥有，无需重复购买。',
    'not_owned': '还没有这件用品，先在商店购买。',
    'play_cooldown': '刚刚玩过，休息五分钟再玩吧。',
    'too_tired': '精力不足，先睡一会儿。',
    'already_full': '已经吃饱啦，快进一些时间再喂食。',
    'already_clean': '猫砂盆很干净，还不需要铲屎。',
    'out_of_stock': '背包里没有这件用品，先去商店购买。',
    'insufficient_coins': '猫爪币不够，先背几个单词吧。',
    'not_adopted': '先给小猫取名并领养。',
    'already_adopted': '这只小猫已经是你的啦。',
    'invalid_name': '请输入名字，最多 16 个汉字。',
    'study_active': '请先结束这局学习，再更换词表。',
    'no_words': '先导入一本词表。',
    'no_words_due': '暂时没有待学的词，等复习时间到了再来吧。',
    'all_mastered': '这本词表已全部掌握！可以换词表，或主动复习已掌握词。',
    'no_mastered_words': '还没有已掌握的词，先开始日常学习吧。',
    'stale_question': '本局已结束，请开始下一局。',
}


class Playground:
    def __init__(self, root, simulator, board, load_csv):
        self.root, self.sim, self.board, self.load_csv = root, simulator, board, load_csv
        self.template = board.parents[3] / 'scripts/maomi-word-template.csv'
        self.state = {}
        self.frames = {}
        self.animation = None
        self.hinted = False
        root.title('小猫咪 · 电脑试玩')
        root.geometry('1020x810')
        root.minsize(980, 790)
        root.configure(bg='#f6f1e9')
        root.option_add('*Font', ('Microsoft YaHei UI', 10))
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('TFrame', background='#f6f1e9')
        style.configure('TLabel', background='#f6f1e9', foreground='#3b3027')
        style.configure('TButton', padding=(12, 8))
        style.configure('Horizontal.TProgressbar', background='#e6a350', troughcolor='#eee1cf')
        shell = ttk.Frame(root, padding=24)
        shell.pack(fill='both', expand=True)
        ttk.Label(shell, text='今天也一起长大吧', font=('Microsoft YaHei UI', 23, 'bold')).pack(anchor='w')
        ttk.Label(shell, text='电脑试玩 · 使用固件实际玩法规则 · 本次关闭后重置，不连接设备').pack(anchor='w', pady=(5, 18))
        body = ttk.Frame(shell)
        body.pack(fill='both', expand=True)
        left = ttk.Frame(body, width=300)
        left.pack(side='left', fill='y', padx=(0, 28))
        ttk.Label(left, text='屏幕预览 · 240 × 240').pack(pady=(0, 10))
        self.screen = tk.Canvas(left, width=240, height=240, highlightthickness=0, bg='#fff4df')
        self.screen.pack()
        self.word_font = Font(root, family='Arial', size=-48)
        self.pet_title = tk.StringVar()
        ttk.Label(left, textvariable=self.pet_title, font=('Microsoft YaHei UI', 16, 'bold')).pack(pady=(14, 6))
        self.details = tk.StringVar()
        ttk.Label(left, textvariable=self.details, justify='center').pack()
        self.satiety = ttk.Progressbar(left, maximum=100, length=240)
        self.satiety.pack(pady=(12, 5))
        self.needs = tk.StringVar()
        ttk.Label(left, textvariable=self.needs).pack()
        self.wallet = tk.StringVar()
        ttk.Label(left, textvariable=self.wallet, font=('Microsoft YaHei UI', 24, 'bold'), foreground='#996020').pack(pady=(18, 0))
        self.inventory = tk.StringVar()
        ttk.Label(left, textvariable=self.inventory, wraplength=270, justify='center').pack(pady=(6, 0))
        right = ttk.Frame(body)
        right.pack(side='left', fill='both', expand=True)
        adoption = ttk.Frame(right)
        adoption.pack(fill='x', pady=(0, 12))
        self.name = tk.StringVar(value='小橘')
        ttk.Label(adoption, text='猫咪名字').pack(side='left')
        self.name_entry = ttk.Entry(adoption, textvariable=self.name, width=15)
        self.name_entry.pack(side='left', padx=8)
        self.adopt_button = ttk.Button(adoption, text='领养这只橘猫', command=lambda: self.act('adopt', self.name.get().strip()))
        self.adopt_button.pack(side='left')
        tabs = ttk.Notebook(right)
        tabs.pack(fill='both', expand=True)
        care, study, shop = [ttk.Frame(tabs, padding=18) for _ in range(3)]
        for frame, title in ((care, '照顾猫咪'), (study, '背单词'), (shop, '用品商店')):
            tabs.add(frame, text=title)
        ttk.Label(care, text='一份猫粮，一点陪伴', font=('Microsoft YaHei UI', 16, 'bold')).pack(anchor='w')
        ttk.Label(care, text='领养赠 6 份猫粮和 3 份猫砂。\n每天两次有效喂食、一次铲屎，记一个照料日。', wraplength=470).pack(anchor='w', pady=12)
        care_actions = ttk.Frame(care)
        care_actions.pack(fill='x')
        for index, (title, action) in enumerate([
                ('喂猫粮 · 饱食 +25', 'feed'), ('铲屎 · 消耗猫砂', 'clean'),
                ('给零食 · 饱食 +5', 'snack'), ('摸摸小猫', 'pet'), ('喝水', 'water'),
                ('免费就医', 'doctor'), ('睡八小时', 'sleep'), ('起床', 'wake'),
                ('领取每日基础补给', 'supplies')]):
            ttk.Button(care_actions, text=title, command=lambda a=action: self.act('care', a)).grid(
                row=index // 2, column=index % 2, sticky='ew', padx=3, pady=4)
        care_actions.columnconfigure((0, 1), weight=1)
        self.life_details = tk.StringVar()
        ttk.Label(care, textvariable=self.life_details, wraplength=470).pack(anchor='w', pady=10)
        ttk.Label(care, text='可以用下方的时间按钮体验饥饿和年龄变化。\n小猫不会因为没来照顾而死亡或离家。', wraplength=470).pack(anchor='w', pady=12)
        self.deck = tk.StringVar()
        ttk.Label(study, textvariable=self.deck).pack(anchor='w')
        row = ttk.Frame(study)
        row.pack(fill='x', pady=8)
        self.mode = tk.StringVar(value='英文 → 中文')
        ttk.Combobox(row, textvariable=self.mode, values=['英文 → 中文', '中文 → 英文'], state='readonly', width=16).pack(side='left')
        ttk.Button(row, text='开始一局', command=self.start).pack(side='left', padx=6)
        ttk.Button(row, text='导入 CSV', command=self.import_csv).pack(side='left')
        ttk.Button(study, text='主动复习已掌握词', command=lambda: self.start(True)).pack(anchor='w')
        self.prompt = tk.StringVar(value='先领养，再开始你的第一局。')
        ttk.Label(study, textvariable=self.prompt, font=('Microsoft YaHei UI', 15, 'bold'), wraplength=460).pack(anchor='w', pady=(8, 5))
        ttk.Label(study, text='电脑端输入作答后自行核对；真机通过语音回答。').pack(anchor='w')
        self.answer = tk.StringVar()
        ttk.Entry(study, textvariable=self.answer).pack(fill='x', pady=8)
        controls = ttk.Frame(study)
        controls.pack(fill='x')
        ttk.Button(controls, text='核对答案', command=self.reveal).pack(side='left')
        ttk.Button(controls, text='查看提示', command=lambda: self.reveal(True)).pack(side='left', padx=6)
        self.feedback = tk.StringVar()
        ttk.Label(study, textvariable=self.feedback, wraplength=460).pack(anchor='w', pady=8)
        answers = ttk.Frame(study)
        answers.pack(fill='x')
        self.correct_button = ttk.Button(answers, text='独立答对', command=self.correct)
        self.correct_button.pack(side='left')
        ttk.Button(answers, text='答错', command=lambda: self.act('answer', 'wrong')).pack(side='left', padx=6)
        ttk.Button(answers, text='跳过', command=lambda: self.act('answer', 'skip')).pack(side='left')
        ttk.Button(answers, text='结束学习', command=lambda: self.act('stop')).pack(side='left', padx=6)
        ttk.Label(study, text='每天完成 5 个不同词得 10 币；首次独立答对另加 2 币，\n提示后答对加 1 币；每日最多 40 币。', wraplength=470).pack(anchor='w', pady=14)
        ttk.Label(shop, text='把学习成果装进背包', font=('Microsoft YaHei UI', 16, 'bold')).pack(anchor='w', pady=(0, 14))
        for product in self.sim.call('status')['shop']:
            title, item, price = product['name'], product['id'], product['price']
            row = ttk.Frame(shop)
            row.pack(fill='x', pady=3)
            unlock = f" · 照料{product['care_days_required']}天" if product['care_days_required'] else ''
            ttk.Label(row, text=f'{title} · {price}币{unlock}', width=26).pack(side='left')
            ttk.Button(row, text='购买', command=lambda i=item: self.act('buy', i, 1)).pack(side='left')
            if product['durable']:
                ttk.Button(row, text='使用', command=lambda i=item: self.act('use', i)).pack(side='left', padx=3)
        reset_row = ttk.Frame(shop)
        reset_row.pack(fill='x', pady=8)
        ttk.Button(reset_row, text='卸下服饰', command=lambda: self.act('use', 'no_outfit')).pack(side='left')
        ttk.Button(reset_row, text='恢复默认房间', command=lambda: self.act('use', 'default_room')).pack(side='left', padx=4)
        ttk.Label(shop, text='耐用品只买一次，使用不扣币。玩具不产生学习奖励。').pack(anchor='w', pady=8)
        self.clock = tk.StringVar()
        ttk.Label(shell, textvariable=self.clock).pack(anchor='w', pady=(16, 6))
        time_row = ttk.Frame(shell)
        time_row.pack(fill='x')
        for title, hours in [('快进 12 小时', 12), ('快进 1 天', 24), ('快进 7 天', 168)]:
            ttk.Button(time_row, text=title, command=lambda h=hours: self.act('advance', h)).pack(side='left', padx=(0, 6))
        ttk.Button(time_row, text='模拟重启', command=lambda: self.act('restart')).pack(side='left', padx=6)
        ttk.Button(time_row, text='重新试玩', command=self.reset).pack(side='right')
        self.message = tk.StringVar(value='欢迎！给小橘取个名字，从领养开始。')
        ttk.Label(shell, textvariable=self.message, wraplength=940, foreground='#8a4b15').pack(anchor='w', pady=(12, 0))
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.sim.import_words(self.load_csv(self.template))
        self.render(self.sim.call('status'))

    def act(self, *args):
        try:
            result = self.sim.call(*args)
            if 'name' not in result:
                raise RuntimeError(result.get('error', '模拟失败'))
            self.render(result)
            if not result['ok']:
                self.message.set(ERRORS.get(result['error'], result['error']))
                return result
            if args[0] == 'answer':
                self.message.set(f"本题获得 {result['coins_added']} 币。" +
                                 (f"本局结束，共获得 {result['session_coins']} 币。" if not result['active'] else '继续下一题吧。'))
            else:
                self.message.set({'adopt': '领养成功！今天是它的出生日，日龄从 0 开始。',
                                  'buy': '已买好，用品已放入背包。', 'care': '照料完成。',
                                  'advance': '模拟时间已快进，查看猫咪的新状态吧。',
                                  'restart': '已从本次试玩的存档重新加载。',
                                  'stop': '本局已结束，已获得的猫爪币保留。',
                                  'start': '开始啦！先回答，再核对答案。'}.get(args[0], '已完成。'))
            if args[0] == 'care' and args[1] in ('feed', 'snack', 'pet', 'play', 'clean'):
                file = {'feed': 'maomi_eat.gif', 'snack': 'maomi_eat.gif', 'pet': 'maomi_pet.gif',
                        'play': 'maomi_play.gif', 'clean': 'happy.png'}[args[1]]
                self.animate(file)
            return result
        except (OSError, RuntimeError, ValueError) as error:
            self.message.set(str(error))
            return None

    def render(self, state):
        changed = (state.get('question'), state.get('active'), state.get('session')) != (
            self.state.get('question'), self.state.get('active'), self.state.get('session'))
        self.state = state
        if changed:
            self.answer.set('')
            self.feedback.set('')
            self.hinted = False
        adopted = bool(state['name'])
        self.adopt_button.configure(state='disabled' if adopted else 'normal')
        self.name_entry.configure(state='disabled' if adopted else 'normal')
        self.pet_title.set(state['name'] or '等你领养的小橘')
        birthday = str(state['birthday'])
        self.details.set(f"{state['growth']} · {state['age_days']} 日龄 · 照料 {state['care_days']} 天\n" +
                         (f'生日 {birthday[:4]}-{birthday[4:6]}-{birthday[6:]}' if adopted else '领养即出生'))
        self.satiety['value'] = state['satiety']
        mood = {'hungry': '肚子饿了', 'dirty': '该铲屎啦', 'happy': '心情很好', 'content': '很自在',
                'sleeping': '睡觉中', 'sick': '需要就医', 'thirsty': '想喝水', 'tired': '想休息',
                'bored': '想被摸摸'}[state['mood']]
        personality = {'curious': '好奇', 'gentle': '温顺', 'playful': '活泼'}[state['personality']]
        self.life_details.set(f"饮水 {state['hydration']}/100 · 心情 {state['happiness']}/100\n"
                              f"精力 {state['energy']}/100 · 健康 {state['health']}/100 · 性格 {personality}")
        self.needs.set(f"饱食 {state['satiety']}/100 · 便便 {state['poop']}/3 · {mood}")
        self.wallet.set(f"{state['coins']} 猫爪币")
        self.inventory.set(f"猫粮 {state['food']} 份 · 猫砂 {state['litter']} 份 · 零食 {state['snacks']} 份\n今日获得 {state['earned_today']}/40 币")
        self.deck.set(f"当前词表 {state['word_count']} 词 · 每局最多五词\n"
                      f"未学 {state['unlearned_count']} · 待巩固 {state['consolidating_count']} · 已掌握 {state['mastered_count']}\n"
                      f"待巩固中已到复习时间：{state['due_count']} 词")
        self.clock.set('模拟时间（手动快进）  ' + datetime.datetime.fromtimestamp(state['epoch']).strftime('%Y-%m-%d %H:%M'))
        if state['active']:
            prompt = state['question_prompt']
            prefix = '再回忆一次' if state['correction'] else f"第 {state['question'] + 1}/{state['target']} 词"
            self.prompt.set(f'{prefix}：{prompt}')
        else:
            self.prompt.set(('本词表已全部掌握！' if state['word_count'] and state['mastered_count'] == state['word_count']
                             else '暂时没有待学词，等待下次复习。' if not state['unlearned_count'] and not state['due_count']
                             else '选一种模式，开始学习。') if adopted else '先领养，再开始你的第一局。')
        self.correct_button.configure(text='完成纠正' if state['correction'] else '独立答对')
        self.draw()

    def draw(self):
        if self.animation:
            self.root.after_cancel(self.animation)
            self.animation = None
        self.screen.delete('all')
        s = self.state
        if s.get('active') and not s.get('sleeping'):
            word = s.get('word', '') if s['mode'] == 'en_zh' else ''
            font = self.word_font if word and self.word_font.measure(word) <= 208 else ('Microsoft YaHei UI', 18)
            self.screen.create_text(120, 102, text=word or '听题作答', width=208,
                                    font=font, fill='#493226')
            self.screen.create_text(120, 218, text='请在电脑输入答案', width=224,
                                    font=('Microsoft YaHei UI', 12), fill='#493226')
        else:
            self.draw_home(s)

    def draw_home(self, s):
        c = self.screen
        sleeping = bool(s.get('sleeping'))
        c.create_rectangle(0, 0, 240, 240, fill='#d7dfe6' if sleeping else '#fff1dc', outline='')
        c.create_rectangle(0, 162, 240, 240, fill='#eed9be', outline='')
        c.create_rectangle(181, 63, 215, 108, fill='#b9dde3', outline='white', width=4)
        if s.get('room') == 2:
            c.create_oval(53, 117, 187, 185, fill='#a5bfa9', outline='')
        if s.get('room'):
            c.create_oval(48, 157, 192, 185, fill='#dca568', outline='')
        scale = 1 if s.get('care_days', 0) >= 21 else .9 if s.get('care_days', 0) >= 7 else .8
        def ellipse(box, fill):
            x1,y1,x2,y2 = box
            c.create_oval(*(120+(x-120)*scale if i%2==0 else 178+(x-178)*scale
                            for i,x in enumerate((x1,y1,x2,y2))), fill=fill, outline='')
        ellipse((89, 117, 153, 179), '#eca24d')
        ellipse((78, 70, 108, 115), '#eca24d')
        ellipse((134, 70, 164, 115), '#eca24d')
        ellipse((78, 90, 164, 154), '#ffbd69')
        for x in (99, 138):
            ellipse((x, 119, x+7, 122 if sleeping else 129), '#493226')
        ellipse((116, 132, 125, 138), '#cf7c77')
        if s.get('outfit') == 1:
            c.create_rectangle(91, 151, 151, 159, fill='#d77d70', outline='')
            c.create_rectangle(138, 156, 149, 172, fill='#d77d70', outline='')
        if s.get('outfit') == 2:
            c.create_rectangle(96, 75, 143, 93, fill='#6b91b3', outline='')
            c.create_text(120, 84, text='★', fill='#ffe99a')
        if s.get('owned', 0) & 3:
            c.create_oval(188, 157, 206, 175, fill='#6b91b3', outline='')
        c.create_text(120, 15, text=s.get('name') or '等你领养', fill='#493226')
        c.create_text(120, 38, text=f"{s.get('growth', '')} · {s.get('age_days', 0)}天", fill='#493226')
        c.create_text(120, 193, text=f"饱食 {s.get('satiety', 100)}  健康 {s.get('health', 100)}", fill='#493226')
        hint = '需要就医' if s.get('health', 100) < 60 else '正在睡觉' if sleeping else '生日快乐！' if s.get('birthday_today') else '该铲屎啦' if s.get('poop') else '一起背单词吧'
        c.create_text(120, 222, text=hint, fill='#493226')

    def images(self, filename):
        if filename not in self.frames:
            frames = []
            for index in range(32 if filename.endswith('.gif') else 1):
                try:
                    frames.append(tk.PhotoImage(file=str(self.board / 'assets-extra' / filename),
                                               format=f'gif -index {index}' if filename.endswith('.gif') else 'png'))
                except tk.TclError:
                    if not frames:
                        raise
                    break
            self.frames[filename] = frames
        return self.frames[filename]

    def animate(self, filename, index=0):
        if self.state.get('active'):
            return
        frames = self.images(filename)
        self.screen.delete('all')
        self.screen.create_image(0, 0, image=frames[index % len(frames)], anchor='nw')
        if self.animation:
            self.root.after_cancel(self.animation)
        self.animation = self.root.after(180, lambda: self.animate(filename, index + 1)) if index < 11 else self.root.after(180, self.draw)

    def start(self, review_mastered=False):
        self.act('start', 'zh_en' if self.mode.get() == '中文 → 英文' else 'en_zh',
                 'review_mastered' if review_mastered else 'normal')

    def reveal(self, hint=False):
        if self.state.get('active'):
            self.hinted |= hint or not self.answer.get().strip()
            self.feedback.set(f"参考：{self.state['word']} — {self.state['meaning']}" + ('（已看提示）' if self.hinted else ''))
            if not self.state['correction']:
                self.correct_button.configure(text='提示后答对' if self.hinted else '独立答对')

    def correct(self):
        self.act('answer', 'corrected' if self.state.get('correction') else 'hinted' if self.hinted else 'correct')

    def import_csv(self):
        path = filedialog.askopenfilename(filetypes=[('UTF-8 单词表', '*.csv')])
        if path:
            try:
                result = self.sim.import_words(self.load_csv(path))
                self.render(result)
                self.message.set(f"已导入 {result['word_count']} 词。" if result['ok'] else ERRORS.get(result['error'], result['error']))
            except (OSError, ValueError, RuntimeError) as error:
                messagebox.showerror('词表导入失败', str(error), parent=self.root)

    def reset(self):
        self.sim.call('reset')
        self.sim.import_words(self.load_csv(self.template))
        self.render(self.sim.call('status'))
        self.message.set('已重新开始试玩，恢复五词示例词表。')

    def close(self):
        if self.animation:
            self.root.after_cancel(self.animation)
        self.sim.close()
        self.root.destroy()
