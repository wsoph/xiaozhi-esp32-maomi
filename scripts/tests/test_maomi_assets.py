import copy
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main" / "boards" / "zhengchen" / "1.54tft-wifi-maomi"
MANIFEST_PATH = BOARD / "assets-manifest.json"
VALIDATOR_PATH = BOARD / "validate_assets.py"

FALLBACK_EMOJIS = [
    "angry.png",
    "confident.png",
    "confused.png",
    "cool.png",
    "crying.png",
    "delicious.png",
    "embarrassed.png",
    "funny.png",
    "happy.png",
    "kissy.png",
    "laughing.png",
    "loving.png",
    "neutral.png",
    "relaxed.png",
    "sad.png",
    "shocked.png",
    "silly.png",
    "sleepy.png",
    "surprised.png",
    "thinking.png",
    "winking.png",
]

ANIMATED_EMOJIS = [
    "maomi_blink.gif",
    "maomi_charge.gif",
    "maomi_eat.gif",
    "maomi_look.gif",
    "maomi_low_battery.gif",
    "maomi_pet.gif",
    "maomi_play.gif",
    "maomi_reminder.gif",
    "maomi_sleep.gif",
]

ANIMATION_FRAME_COUNTS = {
    "maomi_blink.gif": 3,
    **{filename: 4 for filename in ANIMATED_EMOJIS if filename != "maomi_blink.gif"},
}

LOCAL_SOUNDS = [
    "maomi_meow_1.ogg",
    "maomi_meow_2.ogg",
    "maomi_prompt.ogg",
    "maomi_wake.ogg",
]


def load_validator():
    spec = importlib.util.spec_from_file_location("validate_maomi_assets", VALIDATOR_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_manifest():
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


class MaomiAssetsTest(unittest.TestCase):
    def test_board_enables_the_approved_custom_wake_word(self):
        config = json.loads((BOARD / "config.json").read_text(encoding="utf-8"))
        options = config["builds"][0]["sdkconfig_append"]

        self.assertIn("CONFIG_USE_CUSTOM_WAKE_WORD=y", options)
        self.assertIn('CONFIG_CUSTOM_WAKE_WORD="mao mi guo lai"', options)
        self.assertIn('CONFIG_CUSTOM_WAKE_WORD_DISPLAY="猫咪过来"', options)
        self.assertIn("CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=20", options)
        self.assertIn("CONFIG_SR_MN_CN_MULTINET7_QUANT=y", options)

    def test_manifest_matches_the_approved_base_assets(self):
        manifest = load_manifest()

        self.assertEqual(
            manifest["wake_word"],
            {
                "model": "mn7_cn",
                "command": "mao mi guo lai",
                "display": "猫咪过来",
                "threshold": 0.2,
            },
        )
        self.assertEqual(
            manifest["font"],
            {
                "builtin": "font_noto_sans_basic_20_4",
                "file": "font_noto_sans_common_20_4.bin",
                "bundle": "noto-v1",
            },
        )
        self.assertEqual(manifest["fallback_emoji"]["collection"], "noto-color-emoji_64")
        self.assertEqual(manifest["fallback_emoji"]["files"], FALLBACK_EMOJIS)
        self.assertEqual(len(set(manifest["fallback_emoji"]["files"])), 21)
        expected_extra = FALLBACK_EMOJIS + ANIMATED_EMOJIS + LOCAL_SOUNDS
        self.assertEqual(
            [item["file"] for item in manifest["extra_files"]], expected_extra
        )
        self.assertTrue(all(item["required"] for item in manifest["extra_files"]))
        self.assertEqual(manifest["capacity"]["partition_size_bytes"], 8 * 1024 * 1024)
        self.assertEqual(manifest["capacity"]["minimum_free_percent"], 10)

    def test_custom_emojis_are_128_pixels_with_expected_animation_frames(self):
        for filename in FALLBACK_EMOJIS:
            image = (BOARD / "assets-extra" / filename).read_bytes()
            self.assertEqual(image[:8], b"\x89PNG\r\n\x1a\n", filename)
            self.assertEqual(struct.unpack_from(">II", image, 16), (128, 128), filename)

        for filename in ANIMATED_EMOJIS:
            image = (BOARD / "assets-extra" / filename).read_bytes()
            self.assertIn(image[:6], (b"GIF87a", b"GIF89a"), filename)
            self.assertEqual(struct.unpack_from("<HH", image, 6), (128, 128), filename)
            self.assertEqual(
                image.count(b"\x21\xf9\x04"),
                ANIMATION_FRAME_COUNTS[filename],
                filename,
            )

    def test_local_sounds_are_short_24_khz_mono_opus(self):
        for filename in LOCAL_SOUNDS:
            sound = (BOARD / "assets-extra" / filename).read_bytes()

            self.assertTrue(sound.startswith(b"OggS"), filename)
            opus_head = sound.find(b"OpusHead")
            self.assertGreaterEqual(opus_head, 0, filename)
            self.assertEqual(sound[opus_head + 9], 1, filename)
            self.assertEqual(
                struct.unpack_from("<I", sound, opus_head + 12)[0], 24_000, filename
            )
            self.assertIn(b"OpusTags", sound, filename)
            granules = [
                struct.unpack_from("<Q", sound, offset + 6)[0]
                for offset in range(len(sound) - 14)
                if sound[offset : offset + 4] == b"OggS"
            ]
            self.assertTrue(granules, filename)
            self.assertLessEqual(max(granules), 2 * 48_000, filename)

    def test_board_sources_contain_no_deprecated_name(self):
        deprecated_chinese = "咪" * 2
        deprecated_pinyin = ("mi " * 2 + "guo lai").strip()
        candidates = [path for path in BOARD.rglob("*") if path.is_file()]

        for path in candidates:
            if path.suffix.lower() not in {".cc", ".h", ".json", ".md", ".py"}:
                continue
            text = path.read_text(encoding="utf-8")
            self.assertNotIn(deprecated_chinese, text, path)
            self.assertNotIn(deprecated_pinyin, text.casefold(), path)

    def test_cmake_uses_extra_files_and_validates_the_generated_archive(self):
        cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn('set(DEFAULT_ASSETS_EXTRA_FILES "${MAOMI_ASSETS_EXTRA_DIR}")', cmake)
        self.assertEqual(cmake.count("set(MAOMI_INDEX_EXTRA_IMAGES ON)"), 1)
        self.assertIn('list(APPEND BUILD_ARGS "--index_extra_images")', cmake)
        self.assertIn("validate_assets.py", cmake)
        self.assertIn('"--assets-bin" "${GENERATED_ASSETS_BIN}"', cmake)

    def test_reminders_use_the_dedicated_prompt_sound(self):
        board_source = (BOARD / "zhengchen-1.54tft-wifi-maomi.cc").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'constexpr char kMaomiReminderSoundName[] = "maomi_prompt.ogg";',
            board_source,
        )

    def test_validator_accepts_overridden_static_and_custom_animated_emojis(self):
        validator = load_validator()
        manifest = load_manifest()
        indexed = [
            {"name": Path(filename).stem, "file": filename}
            for filename in FALLBACK_EMOJIS + ANIMATED_EMOJIS
        ]
        archive_files = {filename: b"asset" for filename in FALLBACK_EMOJIS + ANIMATED_EMOJIS}

        validator.validate_emoji_index(manifest, indexed, archive_files)

        with self.assertRaisesRegex(
            validator.AssetValidationError, "duplicate emoji name"
        ):
            validator.validate_emoji_index(
                manifest, indexed + [{"name": "happy", "file": "happy.png"}], archive_files
            )

    def test_source_validation_fails_without_the_wake_model_but_allows_optional_files(self):
        validator = load_validator()
        manifest = load_manifest()

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model_root = root / "esp-sr" / "multinet_model"
            fonts_root = root / "fonts"
            extra_root = root / "extra"
            model_dir = model_root / manifest["wake_word"]["model"]
            emoji_dir = fonts_root / "png" / manifest["fallback_emoji"]["collection"]
            model_dir.mkdir(parents=True)
            emoji_dir.mkdir(parents=True)
            extra_root.mkdir()
            (fonts_root / "cbin").mkdir()
            (model_dir / "mn7_data").write_bytes(b"model")
            (fonts_root / "cbin" / manifest["font"]["file"]).write_bytes(b"font")
            for item in manifest["extra_files"]:
                filename = item["file"]
                (extra_root / filename).write_bytes(
                    (BOARD / "assets-extra" / filename).read_bytes()
                )
            for filename in manifest["fallback_emoji"]["files"]:
                (emoji_dir / filename).write_bytes(b"png")

            warnings = validator.validate_sources(
                manifest, model_root.parent, fonts_root, extra_root
            )
            self.assertEqual(warnings, [])

            (extra_root / "angry.png").write_bytes(b"not a png")
            with self.assertRaisesRegex(validator.AssetValidationError, "invalid PNG"):
                validator.validate_sources(
                    manifest, model_root.parent, fonts_root, extra_root
                )
            (extra_root / "angry.png").write_bytes(
                (BOARD / "assets-extra" / "angry.png").read_bytes()
            )

            optional_manifest = copy.deepcopy(manifest)
            optional_manifest["extra_files"] = [
                {"file": "optional-cat.png", "required": False}
            ]
            warnings = validator.validate_sources(
                optional_manifest, model_root.parent, fonts_root, extra_root
            )
            self.assertEqual(len(warnings), 1)

            (model_dir / "mn7_data").unlink()
            with self.assertRaisesRegex(
                validator.AssetValidationError, "required wake model"
            ):
                validator.validate_sources(
                    manifest, model_root.parent, fonts_root, extra_root
                )

    def test_capacity_gate_preserves_ten_percent(self):
        validator = load_validator()
        manifest = load_manifest()
        maximum = int(8 * 1024 * 1024 * 0.9)

        validator.validate_capacity(manifest, maximum)
        with self.assertRaisesRegex(validator.AssetValidationError, "10% free"):
            validator.validate_capacity(manifest, maximum + 1)


if __name__ == "__main__":
    unittest.main()
