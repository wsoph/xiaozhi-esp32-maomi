import hashlib
import json
import struct
import tempfile
import unittest
from pathlib import Path
from scripts import maomi_migrate as migration


def table_bytes(entries):
    return b"".join(struct.pack("<HBBII16sI", 0x50AA, 1, 2, offset, size,
                                name.encode(), 0) for name, (offset, size) in entries.items())


class MigrationTest(unittest.TestCase):
    def test_preserves_original_nvs_and_rejects_wrong_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entries = dict(migration.EXPECTED, nvs=(0x9000, 0x4000), ota_0=(0x20000, 0x3F0000))
            files = {hex(address): f"{address}.bin" for address in migration.LIMITS}
            for filename in files.values():
                (root / filename).write_bytes(b"firmware")
            table = root / files["0x8000"]
            table.write_bytes(table_bytes(entries))
            manifest = root / "flasher_args.json"
            manifest.write_text(json.dumps({"flash_files": files}))
            self.assertEqual(set(migration.build_files(root)), set(migration.LIMITS))
            entries["nvs"] = (0x9000, 0x3000)
            table.write_bytes(table_bytes(entries))
            with self.assertRaisesRegex(ValueError, "preserved"):
                migration.build_files(root)

    def test_rejects_writes_outside_build_and_unknown_flash_address(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "flasher_args.json"
            for files in [{"0x0": "../secret.bin"}, {"0x9000": "nvs.bin"}]:
                manifest.write_text(json.dumps({"flash_files": files}))
                with self.assertRaises(ValueError):
                    migration.build_files(root)

    def test_refuses_incomplete_or_corrupt_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "backup.bin"
            path.write_bytes(b"short")
            with self.assertRaisesRegex(ValueError, "16 MiB"):
                migration.verify_backup(path)
            data = b"\xff" * migration.FLASH_SIZE
            path.write_bytes(data)
            digest = path.with_suffix(".bin.sha256")
            digest.write_text("incorrect")
            with self.assertRaisesRegex(ValueError, "checksum"):
                migration.verify_backup(path)
            digest.write_text(hashlib.sha256(data).hexdigest())
            self.assertEqual(migration.verify_backup(path), data)
