"""Prepare/perform the one-time USB learning partition migration (dry run by default)."""

import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path


FLASH_SIZE = 0x1000000
EXPECTED = {"assets": (0x800000, 0x700000), "words_a": (0xF00000, 0x40000),
            "words_b": (0xF40000, 0x40000), "pet_data": (0xF80000, 0x80000)}
LIMITS = {0: 0x8000, 0x8000: 0x1000, 0xD000: 0x2000, 0x20000: 0x3F0000, 0x800000: 0x700000}


def partitions(data):
    result = {}
    for index in range(0, min(len(data), 0xC00), 32):
        record = data[index:index + 32]
        if len(record) < 32 or record[:2] != b"\xaa\x50":
            break
        _, kind, subtype, offset, size, name, flags = struct.unpack("<HBBII16sI", record)
        result[name.split(b"\0")[0].decode("ascii")] = (offset, size)
    return result


def build_files(directory):
    directory = directory.resolve()
    manifest = json.loads((directory / "flasher_args.json").read_text())
    files = {}
    for address, filename in manifest["flash_files"].items():
        address = int(address, 0)
        path = (directory / filename).resolve()
        if address not in LIMITS or not path.is_relative_to(directory):
            raise ValueError("Unexpected flash address or path outside the selected build")
        if not path.is_file() or not 0 < path.stat().st_size <= LIMITS[address]:
            raise ValueError(f"Missing or oversized image: {path}")
        files[address] = path
    if set(files) != set(LIMITS):
        raise ValueError("Incomplete firmware image set")
    table = partitions(files[0x8000].read_bytes())
    if any(table.get(name) != value for name, value in EXPECTED.items()):
        raise ValueError("Build is not the Maomi learning partition layout")
    if table.get("nvs") != (0x9000, 0x4000) or table.get("ota_0") != (0x20000, 0x3F0000):
        raise ValueError("Existing NVS/application offsets must be preserved")
    return files


def verify_backup(path):
    if path.stat().st_size != FLASH_SIZE:
        raise ValueError("Backup must be exactly 16 MiB")
    data = path.read_bytes()
    checksum = path.with_suffix(path.suffix + ".sha256").read_text().strip()
    if hashlib.sha256(data).hexdigest() != checksum:
        raise ValueError("Backup checksum mismatch")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--restore", action="store_true")
    parser.add_argument("--apply", action="store_true", help="Actually access and write the connected device")
    args = parser.parse_args()
    base = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port,
            "--after", "no-reset"]
    if args.restore:
        verify_backup(args.backup)
        commands = [base + ["write-flash", "0x0", str(args.backup.resolve())],
                    base + ["verify-flash", "0x0", str(args.backup.resolve())]]
    else:
        files = build_files(args.build)
        if args.backup.exists() or args.backup.with_suffix(args.backup.suffix + ".sha256").exists():
            raise ValueError("Choose a new backup filename; existing backups are never overwritten")
        pairs = [value for address, path in sorted(files.items()) for value in (hex(address), str(path))]
        commands = [base + ["read-flash", "0x0", hex(FLASH_SIZE), str(args.backup.resolve())],
                    base + ["erase-region", "0xf00000", "0x100000"],
                    base + ["write-flash", *pairs], base + ["verify-flash", *pairs]]
    for command in commands:
        print(subprocess.list2cmdline(command))
    if not args.apply:
        print("Dry run only. No device was opened. Add --apply to execute; power-cycle after success.")
        return
    if not args.restore:
        args.backup.parent.mkdir(parents=True, exist_ok=True)
    for index, command in enumerate(commands):
        subprocess.run(command, check=True)
        if not args.restore and index == 0:
            if args.backup.stat().st_size != FLASH_SIZE:
                raise ValueError("Incomplete backup; migration stopped")
            data = args.backup.read_bytes()
            args.backup.with_suffix(args.backup.suffix + ".sha256").write_text(hashlib.sha256(data).hexdigest() + "\n")
            old = partitions(data[0x8000:0x9000])
            if old.get("assets") != (0x800000, 0x800000) or "pet_data" in old:
                raise ValueError("Device is not the original layout; refusing to erase an existing pet save")
    print("Verified. Disconnect and reconnect power to start the firmware.")


if __name__ == "__main__":
    main()
