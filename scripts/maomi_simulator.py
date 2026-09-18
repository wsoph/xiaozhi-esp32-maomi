"""Desktop pet playground using the firmware's actual C++ LearningEngine."""

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'main/boards/zhengchen/1.54tft-wifi-maomi'


def build_core():
    candidates = [os.environ.get('CXX'), shutil.which('g++'), shutil.which('clang++'),
                  ROOT.parent / 'tool-cache/w64devkit-2.9.1/w64devkit/bin/g++.exe']
    compiler = next((Path(p) for p in candidates if p and Path(p).is_file()), None)
    if compiler is None:
        raise RuntimeError('需要电脑 C++ 编译器：安装 g++ / clang++，或通过 CXX 指定编译器路径。')
    sources = [ROOT / 'scripts/maomi_simulator_core.cc', BOARD / 'maomi_learning.cc']
    digest = hashlib.sha256(str(compiler).encode() + b''.join(
        p.read_bytes() for p in sources + [BOARD / 'maomi_learning.h', BOARD / 'maomi_pet_life.h'])).hexdigest()[:16]
    executable = ROOT / '.cache/maomi-simulator' / (digest + ('.exe' if os.name == 'nt' else ''))
    environment = os.environ.copy()
    environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
    if not executable.exists():
        executable.parent.mkdir(parents=True, exist_ok=True)
        command = [str(compiler), '-std=c++20', '-O1', '-I', str(BOARD),
                   *map(str, sources), '-o', str(executable)]
        result = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace',
                                env=environment, timeout=120,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        if result.returncode:
            raise RuntimeError('模拟器编译失败：\n' + result.stderr)
    return executable, environment


class Simulator:
    def __init__(self):
        executable, environment = build_core()
        self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, encoding='utf-8', env=environment,
                                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)

    def call(self, *args):
        payload = ' '.join(str(arg).encode('utf-8').hex() or '-' for arg in args)
        self.process.stdin.write(payload + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('模拟引擎已退出，请重新打开试玩窗口。')
        return json.loads(line)

    def import_words(self, words):
        return self.call('import', *(part for word in words for part in word))

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if not self.process.stdin.closed:
            self.process.stdin.close()
        self.process.stdout.close()


def main():
    import argparse
    import tkinter as tk
    if __package__:
        from .maomi_import import load_csv
        from .maomi_simulator_ui import Playground
    else:
        from maomi_import import load_csv
        from maomi_simulator_ui import Playground
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smoke-test', action='store_true', help='隐藏窗口完成界面启动和操作检查')
    args = parser.parse_args()
    sim = Simulator()
    root = tk.Tk()
    if args.smoke_test:
        root.withdraw()
    try:
        app = Playground(root, sim, BOARD, load_csv)
        if args.smoke_test:
            app.act('adopt', '小橘')
            app.act('advance', 24)
            app.act('care', 'feed')
            app.start()
            app.reveal(True)
            app.correct()
            root.update()
            root.after(100, app.close)
        root.mainloop()
    finally:
        sim.close()


if __name__ == '__main__':
    main()
