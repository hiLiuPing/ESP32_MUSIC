#!/usr/bin/env python3
"""Validate RGB565 + Alpha8 weather and Classic scene resources."""

from __future__ import annotations

import re
from pathlib import Path

from generate_weather_icons import ICON_SIZE, SOURCE_DIR, read_icon

ROOT = Path(__file__).resolve().parents[1]
HEX16 = re.compile(r"0x([0-9A-Fa-f]{4})")
HEX8 = re.compile(r"0x([0-9A-Fa-f]{2})")


def validate_weather() -> int:
    source = (ROOT / "src" / "gui" / "resources" / "icons.c").read_text()
    blocks = re.findall(
        r"static const uint16_t weather_icon_(\d+)_data\[WEATHER_ICON_PIXELS\] = \{(.*?)\};\s*"
        r"static const uint8_t weather_icon_\1_alpha\[WEATHER_ICON_ALPHA8_BYTES\] = \{(.*?)\};",
        source,
        re.DOTALL,
    )
    png_paths = list(SOURCE_DIR.glob("*.png"))
    png_codes = {int(path.stem) for path in png_paths}
    generated_codes = {int(code) for code, _, _ in blocks}
    assert len(blocks) == len(png_codes) == 61
    assert generated_codes == png_codes and 999 in generated_codes
    for code, pixel_body, alpha_body in blocks:
        pixels = [int(value, 16) for value in HEX16.findall(pixel_body)]
        alpha = bytes(int(value, 16) for value in HEX8.findall(alpha_body))
        expected_pixels, expected_alpha = read_icon(SOURCE_DIR / f"{code}.png")
        assert len(pixels) == ICON_SIZE * ICON_SIZE
        assert len(alpha) == ICON_SIZE * ICON_SIZE
        assert pixels == expected_pixels
        assert alpha == expected_alpha
    return len(blocks)


def validate_scene(path: Path) -> int:
    source = path.read_text()
    data_blocks = dict(re.findall(r"static const uint16_t (\w+)_data\[\] = \{(.*?)\};", source, re.DOTALL))
    alpha_blocks = dict(re.findall(r"static const uint8_t (\w+)_alpha\[\] = \{(.*?)\};", source, re.DOTALL))
    infos = dict(re.findall(
        r"static const egui_image_std_info_t (\w+)_info = \{(.*?)\};", source, re.DOTALL))
    total = 0
    for name, data_body in data_blocks.items():
        info = infos[name]
        width = int(re.search(r"\.width = (\d+),", info).group(1))
        height = int(re.search(r"\.height = (\d+),", info).group(1))
        assert name in alpha_blocks
        assert "EGUI_IMAGE_DATA_TYPE_RGB565" in info
        assert "EGUI_IMAGE_ALPHA_TYPE_8" in info
        assert len(HEX16.findall(data_body)) == width * height
        assert len(HEX8.findall(alpha_blocks[name])) == width * height
        total += 1
    assert total > 0
    return total


if __name__ == "__main__":
    weather_count = validate_weather()
    scene_count = validate_scene(ROOT / "src/gui/resources/home_scene_res.c")
    camp_count = validate_scene(ROOT / "src/gui/resources/home_camp_res.c")
    print(f"validated {weather_count} weather RGB565/Alpha8 icons and {scene_count + camp_count} scene resources")
