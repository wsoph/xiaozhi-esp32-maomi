#!/usr/bin/env python3
"""Validate the reproducible base assets for the Maomi board variant."""

import argparse
import json
import struct
import sys
from pathlib import Path


ENTRY_NAME_BYTES = 32
ENTRY_SIZE = 44
HEADER_SIZE = 12


class AssetValidationError(RuntimeError):
    pass


def _require(condition, message):
    if not condition:
        raise AssetValidationError(message)


def _deprecated_terms():
    return ("咪" * 2, ("mi " * 2 + "guo lai").strip())


def load_manifest(path):
    try:
        manifest = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AssetValidationError(f"cannot read assets manifest: {error}") from error
    validate_manifest(manifest)
    return manifest


def validate_manifest(manifest):
    _require(manifest.get("schema_version") == 1, "unsupported assets manifest schema")

    wake_word = manifest.get("wake_word", {})
    for key in ("model", "command", "display", "threshold"):
        _require(wake_word.get(key) not in (None, ""), f"wake_word.{key} is required")
    _require(0 < wake_word["threshold"] < 1, "wake word threshold must be between 0 and 1")

    font = manifest.get("font", {})
    for key in ("builtin", "file", "bundle"):
        _require(font.get(key), f"font.{key} is required")

    fallback = manifest.get("fallback_emoji", {})
    emoji_files = fallback.get("files", [])
    _require(fallback.get("collection"), "fallback emoji collection is required")
    _require(len(emoji_files) == 21, "exactly 21 fallback emoji files are required")
    _require(len(set(emoji_files)) == 21, "fallback emoji filenames must be unique")

    extra_files = manifest.get("extra_files", [])
    _require(isinstance(extra_files, list), "extra_files must be a list")
    for item in extra_files:
        _require(isinstance(item, dict), "each extra file must be an object")
        filename = item.get("file", "")
        _require(filename and Path(filename).name == filename, "extra file must be a flat filename")
        _require(isinstance(item.get("required"), bool), "extra file required flag must be boolean")

    capacity = manifest.get("capacity", {})
    _require(capacity.get("partition_size_bytes", 0) > 0, "assets partition size is required")
    minimum_free = capacity.get("minimum_free_percent", -1)
    _require(0 <= minimum_free < 100, "minimum free percent is invalid")

    serialized = json.dumps(manifest, ensure_ascii=False).casefold()
    for deprecated in _deprecated_terms():
        _require(deprecated.casefold() not in serialized, "deprecated wake word is present")


def validate_sources(manifest, esp_sr_model_path, noto_fonts_path, extra_files_path):
    validate_manifest(manifest)
    esp_sr_model_path = Path(esp_sr_model_path)
    noto_fonts_path = Path(noto_fonts_path)
    extra_files_path = Path(extra_files_path)

    model_dir = esp_sr_model_path / "multinet_model" / manifest["wake_word"]["model"]
    model_files = [path for path in model_dir.rglob("*") if path.is_file()]
    _require(model_files, f"required wake model is missing or empty: {model_dir}")

    font_path = noto_fonts_path / "cbin" / manifest["font"]["file"]
    _require(font_path.is_file(), f"required text font is missing: {font_path}")

    emoji_dir = noto_fonts_path / "png" / manifest["fallback_emoji"]["collection"]
    missing_emojis = [
        filename
        for filename in manifest["fallback_emoji"]["files"]
        if not (emoji_dir / filename).is_file()
    ]
    _require(not missing_emojis, f"fallback emoji files are missing: {missing_emojis}")

    warnings = []
    for item in manifest["extra_files"]:
        source = extra_files_path / item["file"]
        if source.is_file():
            continue
        message = f"extra asset is missing: {source}"
        if item["required"]:
            raise AssetValidationError(message)
        warnings.append(message)
    return warnings


def validate_capacity(manifest, assets_size):
    capacity = manifest["capacity"]
    partition_size = capacity["partition_size_bytes"]
    minimum_free = capacity["minimum_free_percent"]
    maximum_size = partition_size * (100 - minimum_free) // 100
    _require(
        assets_size <= maximum_size,
        f"assets.bin is too large to keep {minimum_free}% free "
        f"({assets_size} > {maximum_size} bytes)",
    )


def _read_archive(path):
    data = Path(path).read_bytes()
    _require(len(data) >= HEADER_SIZE, "assets.bin is shorter than its header")
    file_count, checksum, combined_size = struct.unpack_from("<III", data, 0)
    combined = data[HEADER_SIZE:]
    _require(combined_size == len(combined), "assets.bin length field is invalid")
    _require(sum(combined) & 0xFFFF == checksum, "assets.bin checksum is invalid")
    _require(file_count * ENTRY_SIZE <= len(combined), "assets.bin file table is invalid")

    payload_start = HEADER_SIZE + file_count * ENTRY_SIZE
    files = {}
    for index in range(file_count):
        entry = HEADER_SIZE + index * ENTRY_SIZE
        raw_name = data[entry : entry + ENTRY_NAME_BYTES]
        name = raw_name.split(b"\0", 1)[0].decode("utf-8")
        size, offset = struct.unpack_from("<II", data, entry + ENTRY_NAME_BYTES)
        content_start = payload_start + offset
        content_end = content_start + 2 + size
        _require(content_end <= len(data), f"asset entry is outside archive: {name}")
        _require(data[content_start : content_start + 2] == b"ZZ", f"asset marker is invalid: {name}")
        _require(name not in files, f"duplicate asset entry: {name}")
        files[name] = data[content_start + 2 : content_end]
    return data, files


def validate_archive(manifest, assets_bin_path):
    validate_manifest(manifest)
    archive, files = _read_archive(assets_bin_path)
    validate_capacity(manifest, len(archive))

    _require("index.json" in files, "assets.bin has no index.json")
    try:
        index = json.loads(files["index.json"].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AssetValidationError(f"assets index is invalid: {error}") from error

    wake_word = manifest["wake_word"]
    _require(wake_word["model"].encode() in archive, "wake model name is absent from assets.bin")
    model_info = index.get("multinet_model", {})
    commands = model_info.get("commands", [])
    _require(len(commands) == 1, "assets index must contain one wake command")
    _require(commands[0].get("command") == wake_word["command"], "wake command does not match")
    _require(commands[0].get("text") == wake_word["display"], "wake display text does not match")
    _require(model_info.get("threshold") == wake_word["threshold"], "wake threshold does not match")

    font = manifest["font"]
    _require(index.get("text_font") == font["file"], "text font does not match")
    _require(font["file"] in files, "text font file is absent from assets.bin")
    _require(index.get("text_font_meta", {}).get("bundle") == font["bundle"], "font bundle does not match")

    expected_emojis = manifest["fallback_emoji"]["files"]
    indexed_emojis = [item.get("file") for item in index.get("emoji_collection", [])]
    _require(set(indexed_emojis) == set(expected_emojis), "fallback emoji index does not match")
    _require(all(filename in files for filename in expected_emojis), "fallback emoji is absent from assets.bin")

    warnings = []
    indexed_extra = set(index.get("extra_files", []))
    for item in manifest["extra_files"]:
        if item["file"] in indexed_extra and item["file"] in files:
            continue
        message = f"extra asset is absent from assets.bin: {item['file']}"
        if item["required"]:
            raise AssetValidationError(message)
        warnings.append(message)

    for deprecated in _deprecated_terms():
        _require(deprecated.encode("utf-8") not in archive, "deprecated wake word is present in assets.bin")
    return warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--esp-sr-model-path", required=True, type=Path)
    parser.add_argument("--noto-fonts-path", required=True, type=Path)
    parser.add_argument("--extra-files", required=True, type=Path)
    parser.add_argument("--assets-bin", type=Path)
    args = parser.parse_args()

    try:
        manifest = load_manifest(args.manifest)
        warnings = validate_sources(
            manifest, args.esp_sr_model_path, args.noto_fonts_path, args.extra_files
        )
        if args.assets_bin:
            warnings.extend(validate_archive(manifest, args.assets_bin))
    except (AssetValidationError, OSError) as error:
        print(f"Maomi assets validation failed: {error}", file=sys.stderr)
        return 1

    for warning in warnings:
        print(f"Maomi assets warning: {warning}")
    print("Maomi assets validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
