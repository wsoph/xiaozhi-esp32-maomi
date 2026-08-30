import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "build_default_assets", ROOT / "scripts" / "build_default_assets.py"
)
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class BuildDefaultAssetsTest(unittest.TestCase):
    def test_extra_images_are_not_indexed_by_default(self):
        original = [{"name": "neutral", "file": "neutral.png"}]

        result = BUILD.merge_extra_image_index(
            original,
            ["maomi_sleep.gif", "custom.png", "maomi_prompt.ogg"],
        )

        self.assertEqual(result, original)

    def test_extra_images_are_indexed_in_stable_order_when_enabled(self):
        result = BUILD.merge_extra_image_index(
            [],
            ["z-last.png", "maomi_prompt.ogg", "a-first.gif"],
            enabled=True,
        )

        self.assertEqual(
            result,
            [
                {"name": "a-first", "file": "a-first.gif"},
                {"name": "z-last", "file": "z-last.png"},
            ],
        )

    def test_extra_image_with_existing_name_does_not_duplicate_index_entry(self):
        result = BUILD.merge_extra_image_index(
            [{"name": "happy", "file": "happy.png"}],
            ["maomi_blink.gif", "happy.png"],
            enabled=True,
        )

        self.assertEqual(
            result,
            [
                {"name": "happy", "file": "happy.png"},
                {"name": "maomi_blink", "file": "maomi_blink.gif"},
            ],
        )

    def test_extra_file_list_is_reproducibly_sorted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            assets = root / "assets"
            source.mkdir()
            assets.mkdir()
            (source / "z.ogg").write_bytes(b"z")
            (source / "a.png").write_bytes(b"a")

            result = BUILD.process_extra_files(str(source), str(assets))

            self.assertEqual(result, ["a.png", "z.ogg"])

    def test_text_font_metadata_uses_bundle_charset_size_and_bpp(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory)
            BUILD.generate_index_json(
                str(assets),
                None,
                "font_noto_sans_common_20_4.bin",
                None,
                font_bundle_id="noto-v1",
            )
            index = json.loads((assets / "index.json").read_text(encoding="utf-8"))
            self.assertEqual(
                index["text_font_meta"],
                {"charset": "common", "size": 20, "bpp": 4, "bundle": "noto-v1"},
            )

    def test_text_font_requires_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                BUILD.generate_index_json(
                    directory, None, "font_noto_sans_common_20_4.bin", None
                )


if __name__ == "__main__":
    unittest.main()
