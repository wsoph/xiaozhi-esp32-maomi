import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main" / "boards" / "zhengchen" / "1.54tft-wifi-maomi"


def find_host_compiler():
    candidates = []
    if os.environ.get("CXX"):
        candidates.append(Path(os.environ["CXX"]))
    for name in ("g++", "clang++"):
        resolved = shutil.which(name)
        if resolved:
            candidates.append(Path(resolved))
    candidates.append(
        ROOT.parent
        / "tool-cache"
        / "w64devkit-2.9.1"
        / "w64devkit"
        / "bin"
        / "g++.exe"
    )
    return next((candidate for candidate in candidates if candidate.is_file()), None)


class MaomiHostCppTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_host_compiler()
        if cls.compiler is None:
            raise RuntimeError(
                "required host C++ tests cannot run because no C++ compiler is available"
            )

    def compile_and_run(self, name, sources):
        directory = Path(tempfile.mkdtemp(prefix=f"{name}-"))
        self.addCleanup(shutil.rmtree, directory, True)
        executable = directory / (f"{name}.exe" if os.name == "nt" else name)
        environment = os.environ.copy()
        environment["PATH"] = os.pathsep.join(
            [str(self.compiler.parent), environment.get("PATH", "")]
        )
        command = [
            str(self.compiler),
            "-std=c++20",
            "-pthread",
            "-I",
            str(ROOT / "scripts" / "tests" / "stubs"),
            "-I",
            str(ROOT / "main"),
            "-I",
            str(BOARD),
            *map(str, sources),
            "-o",
            str(executable),
        ]
        compiled = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)

        completed = subprocess.run(
            [str(executable)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_wake_sequence_contract(self):
        self.compile_and_run(
            "maomi-wake-test",
            [ROOT / "scripts" / "tests" / "maomi_wake_test.cc", BOARD / "maomi_wake.cc"],
        )

    def test_voice_upload_gate_contract(self):
        self.compile_and_run(
            "voice-upload-gate-test",
            [ROOT / "scripts" / "tests" / "voice_upload_gate_test.cc"],
        )

    def test_sound_playback_limits_contract(self):
        self.compile_and_run(
            "sound-playback-limits-test",
            [ROOT / "scripts" / "tests" / "sound_playback_limits_test.cc"],
        )

    def test_pet_core_contract(self):
        self.compile_and_run(
            "maomi-pet-core-test",
            [
                ROOT / "scripts" / "tests" / "maomi_pet_core_test.cc",
                BOARD / "maomi_pet_core.cc",
            ],
        )

    def test_autonomy_contract(self):
        self.compile_and_run(
            "maomi-autonomy-test",
            [
                ROOT / "scripts" / "tests" / "maomi_autonomy_test.cc",
                BOARD / "maomi_autonomy.cc",
                BOARD / "maomi_clock.cc",
            ],
        )

    def test_proactive_sound_contract(self):
        self.compile_and_run(
            "maomi-proactive-sound-test",
            [
                ROOT / "scripts" / "tests" / "maomi_proactive_sound_test.cc",
                BOARD / "maomi_autonomy.cc",
                BOARD / "maomi_clock.cc",
            ],
        )

    def test_clock_and_storage_contract(self):
        self.compile_and_run(
            "maomi-clock-storage-test",
            [
                ROOT / "scripts" / "tests" / "maomi_clock_storage_test.cc",
                BOARD / "maomi_clock.cc",
                BOARD / "maomi_storage.cc",
            ],
        )

    def test_ui_mapping_contract(self):
        self.compile_and_run(
            "maomi-ui-test",
            [
                ROOT / "scripts" / "tests" / "maomi_ui_test.cc",
                BOARD / "maomi_ui.cc",
            ],
        )

    def test_ogg_integrity_contract(self):
        self.compile_and_run(
            "ogg-demuxer-test",
            [
                ROOT / "scripts" / "tests" / "ogg_demuxer_test.cc",
                ROOT / "main" / "audio" / "demuxer" / "ogg_demuxer.cc",
            ],
        )


if __name__ == "__main__":
    unittest.main()
