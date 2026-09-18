"""USB vocabulary importer. Run without arguments for the desktop interface."""

import argparse
import csv
import json
import queue
import re
import secrets
import threading
import time
import zlib
from pathlib import Path


def read_csv(stream):
    reader = csv.DictReader(stream)
    if reader.fieldnames != ["word", "meaning"]:
        raise ValueError("请使用两列表头：word,meaning")
    words, seen = [], set()
    for line, row in enumerate(reader, 2):
        if None in row or row["meaning"] is None:
            raise ValueError(f"第 {line} 行列数错误")
        word = row["word"].strip().lower()
        meaning = row["meaning"].strip()
        if not re.fullmatch(r"[a-z](?:[a-z'-]{0,30}[a-z])?", word):
            raise ValueError(f"第 {line} 行：英文须为不超过 32 字符的单词")
        if not meaning or len(meaning.encode("utf-8")) > 120 or any(ord(c) < 32 or ord(c) == 127 for c in meaning):
            raise ValueError(f"第 {line} 行：释义为空、过长或含控制字符")
        if word in seen:
            raise ValueError(f"第 {line} 行：重复单词 {word}")
        seen.add(word)
        words.append((word, meaning))
        if len(words) > 1000:
            raise ValueError("一本词表最多 1000 个单词")
    if not words:
        raise ValueError("词表不能为空")
    return words


def load_csv(path):
    if Path(path).stat().st_size > 256_000:
        raise ValueError("CSV 文件过大，请限制为 1000 词以内")
    with open(path, encoding="utf-8-sig", newline="") as stream:
        return read_csv(stream)


def book_crc(words):
    data = b"".join(word.encode() + b"\0" + meaning.encode() + b"\0" for word, meaning in words)
    return zlib.crc32(data)


class Device:
    def __init__(self, serial_port, timeout=20):
        self.serial = serial_port
        self.request = secrets.randbelow(1_000_000_000) + 1
        self.timeout = timeout

    def call(self, op, **arguments):
        self.request += 1
        payload = dict(arguments, op=op, request=self.request)
        encoded = ("@ML1 " + json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
        if len(encoded) > 1024:
            raise ValueError("请求过长")
        # Retransmit the same request, never create a new transaction on lost output.
        for attempt in range(3):
            self.serial.write(encoded)
            result = self._receive(time.monotonic() + self.timeout)
            if result is not None:
                return result
        raise TimeoutError("未收到猫咪回复。请检查数据线、串口、学习版固件，并关闭串口监视器。")

    def _receive(self, deadline):
        while time.monotonic() < deadline:
            raw = self.serial.readline(4096)
            # ROM/IDF logs can share the UART; only consume complete protocol records.
            start = raw.find(b"@ML1 ")
            if start < 0 or not raw.endswith(b"\n"):
                continue
            try:
                response = json.loads(raw[start + 5:].decode("utf-8"))
            except (UnicodeError, json.JSONDecodeError):
                continue
            if not isinstance(response, dict) or response.get("request") != self.request:
                continue
            if not response.get("ok"):
                raise ValueError(str(response.get("error", "设备拒绝操作")))
            if response.get("status") == "pending":
                raise TimeoutError("设备仍在保存。请重新查询状态，不要立即重复导入。")
            return response
        return None

    def upload(self, words, progress=lambda current, total: None):
        status = self.call("status")
        if status.get("active"):
            raise ValueError("请先对猫咪说：结束学习")
        if not status.get("time_valid"):
            raise ValueError("请先让猫咪联网完成校时")
        begun = self.call("import_begin", revision=status["revision"], count=len(words), crc=book_crc(words))
        for index, (word, meaning) in enumerate(words):
            self.call("import_word", upload_id=begun["upload_id"], index=index, word=word, meaning=meaning)
            progress(index + 1, len(words))
        return self.call("import_commit", upload_id=begun["upload_id"])


def open_port(name):
    import serial
    # Set control lines before opening, avoiding deliberate reset/download-mode sequences.
    port = serial.Serial(port=None, baudrate=115200, timeout=0.5, write_timeout=3)
    port.dtr = False
    port.rts = False
    port.port = name
    port.open()
    return port


def launch_gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from serial.tools import list_ports

    app = tk.Tk()
    app.title("小猫咪 · 单词导入")
    app.geometry("680x490")
    app.minsize(600, 430)
    frame = ttk.Frame(app, padding=22)
    frame.pack(fill="both", expand=True)
    ttk.Label(frame, text="把你的单词带给小猫咪", font=("Microsoft YaHei UI", 17)).pack(anchor="w")
    ttk.Label(frame, text="连接 USB 数据线，选择 CSV 词表。猫咪需运行学习版固件。", wraplength=600).pack(anchor="w", pady=(8, 16))
    row = ttk.Frame(frame)
    row.pack(fill="x")
    path = tk.StringVar()
    ttk.Entry(row, textvariable=path, state="readonly").pack(side="left", fill="x", expand=True)
    words = []
    status = tk.StringVar(value="尚未选择词表")
    events = queue.Queue()
    preview = tk.Text(frame, height=10, state="disabled", font=("Microsoft YaHei UI", 11))

    def choose():
        nonlocal words
        filename = filedialog.askopenfilename(filetypes=[("CSV 词表", "*.csv")])
        if not filename:
            return
        try:
            words = load_csv(filename)
            path.set(filename)
            preview.configure(state="normal")
            preview.delete("1.0", "end")
            preview.insert("end", "\n".join(f"{word}    {meaning}" for word, meaning in words[:8]))
            preview.configure(state="disabled")
            status.set(f"检查通过，共 {len(words)} 词；预览前 8 词")
        except (OSError, UnicodeError, ValueError) as error:
            words = []
            status.set(str(error))
            messagebox.showerror("词表需要修改", str(error))

    choose_button = ttk.Button(row, text="选择词表", command=choose)
    choose_button.pack(side="left", padx=(8, 0))
    serial_row = ttk.Frame(frame)
    serial_row.pack(fill="x", pady=12)
    ttk.Label(serial_row, text="猫咪串口").pack(side="left")
    port = tk.StringVar()
    ports = ttk.Combobox(serial_row, textvariable=port, state="readonly", width=20)
    ports.pack(side="left", padx=8)

    def refresh():
        ports["values"] = [p.device for p in list_ports.comports()]
        if ports["values"]:
            port.set(ports["values"][0])

    refresh_button = ttk.Button(serial_row, text="刷新", command=refresh)
    refresh_button.pack(side="left")
    refresh()
    preview.pack(fill="both", expand=True)
    bar = ttk.Progressbar(frame, maximum=100)
    bar.pack(fill="x", pady=(12, 6))
    ttk.Label(frame, textvariable=status, wraplength=610).pack(anchor="w")

    def upload():
        if not words or not port.get():
            messagebox.showinfo("还差一步", "请选择通过检查的词表和猫咪串口。")
            return
        selected_words, selected_port = list(words), port.get()
        for widget in (upload_button, choose_button, refresh_button, ports):
            widget.configure(state="disabled")
        status.set("正在连接猫咪…")

        def run():
            try:
                with open_port(selected_port) as connection:
                    result = Device(connection).upload(selected_words, lambda n, total: events.put(("progress", (n, total))))
                events.put(("done", f"已导入 {result['word_count']} 词。对猫咪说：开始背单词。"))
            except Exception as error:
                events.put(("done", f"导入未确认：{error}"))
        threading.Thread(target=run, daemon=True).start()

    upload_button = ttk.Button(frame, text="导入猫咪", command=upload)
    upload_button.pack(anchor="e", pady=(10, 0))

    def poll():
        try:
            while True:
                kind, value = events.get_nowait()
                if kind == "progress":
                    current, total = value
                    bar["value"] = current * 100 / total
                    status.set(f"已传输 {current}/{total}，完成后将校验并保存…")
                else:
                    status.set(value)
                    for widget in (upload_button, choose_button, refresh_button):
                        widget.configure(state="normal")
                    ports.configure(state="readonly")
        except queue.Empty:
            pass
        app.after(100, poll)
    poll()
    app.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", type=Path, help="只检查词表，不连接设备")
    parser.add_argument("--port", help="串口，例如 COM5")
    parser.add_argument("--csv", type=Path, help="直接上传词表")
    args = parser.parse_args()
    if args.check:
        words = load_csv(args.check)
        print(f"Valid: {len(words)} words, CRC32={book_crc(words):08x}")
    elif args.port and args.csv:
        with open_port(args.port) as connection:
            print(json.dumps(Device(connection).upload(load_csv(args.csv)), ensure_ascii=False))
    elif args.port or args.csv:
        parser.error("--port 和 --csv 必须一起使用")
    else:
        launch_gui()


if __name__ == "__main__":
    main()
