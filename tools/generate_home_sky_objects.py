#!/usr/bin/env python3
"""Extract home-page clouds and geese and generate Alpha8 resources."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from math import sqrt
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "file" / "yun"
OUTPUT_DIR = SOURCE_DIR / "generated"
HEADER_PATH = ROOT / "include" / "gui" / "resources" / "home_sky_objects_res.h"
SOURCE_PATH = ROOT / "src" / "gui" / "resources" / "home_sky_objects_res.c"
PREVIEW_PATH = OUTPUT_DIR / "home_sky_objects_preview.png"

EXPECTED_CLOUDS = 20
EXPECTED_BIRDS = 17
CLOUD_MIN_WIDTH = 34
CLOUD_MAX_WIDTH = 68
BIRD_MIN_WIDTH = 12
BIRD_MAX_WIDTH = 30


@dataclass(frozen=True)
class Component:
    mask: Image.Image
    area: int
    source_x: int
    source_y: int


@dataclass(frozen=True)
class Asset:
    name: str
    mask: Image.Image


def binary_mask(image: Image.Image, predicate) -> Image.Image:
    return image.point(lambda value: 255 if predicate(value) else 0, mode="L")


def connected_components(mask: Image.Image, min_area: int) -> list[Component]:
    width, height = mask.size
    pixels = mask.load()
    visited = bytearray(width * height)
    components: list[Component] = []

    for y in range(height):
        for x in range(width):
            start = y * width + x
            if visited[start] or pixels[x, y] == 0:
                continue

            visited[start] = 1
            queue = deque([(x, y)])
            points: list[tuple[int, int]] = []
            min_x = max_x = x
            min_y = max_y = y

            while queue:
                px, py = queue.popleft()
                points.append((px, py))
                min_x = min(min_x, px)
                max_x = max(max_x, px)
                min_y = min(min_y, py)
                max_y = max(max_y, py)

                for ny in range(max(0, py - 1), min(height, py + 2)):
                    for nx in range(max(0, px - 1), min(width, px + 2)):
                        index = ny * width + nx
                        if not visited[index] and pixels[nx, ny] != 0:
                            visited[index] = 1
                            queue.append((nx, ny))

            if len(points) < min_area:
                continue

            component = Image.new("L", (max_x - min_x + 1, max_y - min_y + 1), 0)
            component_pixels = component.load()
            for px, py in points:
                component_pixels[px - min_x, py - min_y] = 255
            components.append(Component(component, len(points), min_x, min_y))

    return sorted(components, key=lambda item: (item.source_y, item.source_x))


def extract_clouds() -> list[Component]:
    with Image.open(SOURCE_DIR / "yun1.png") as image:
        screenshot_mask = binary_mask(ImageOps.grayscale(image), lambda value: value >= 230)
    screenshot_clouds = connected_components(screenshot_mask, min_area=500)

    with Image.open(SOURCE_DIR / "yun2.png") as image:
        alpha_mask = binary_mask(image.convert("RGBA").getchannel("A"), lambda value: value >= 24)
    alpha_clouds = connected_components(alpha_mask, min_area=1000)

    clouds = screenshot_clouds + alpha_clouds
    if len(clouds) != EXPECTED_CLOUDS:
        raise ValueError(f"expected {EXPECTED_CLOUDS} clouds, found {len(clouds)}")
    return clouds


def extract_birds() -> list[Component]:
    with Image.open(SOURCE_DIR / "dayan.png") as image:
        bird_mask = binary_mask(ImageOps.grayscale(image), lambda value: value < 153)
    birds = connected_components(bird_mask, min_area=10)
    if len(birds) != EXPECTED_BIRDS:
        raise ValueError(f"expected {EXPECTED_BIRDS} birds, found {len(birds)}")
    return birds


def resize_binary(mask: Image.Image, target_width: int) -> Image.Image:
    target_height = max(1, round(mask.height * target_width / mask.width))
    resized = mask.resize((target_width, target_height), Image.Resampling.LANCZOS)
    return binary_mask(resized, lambda value: value >= 96)


def pad_mask(mask: Image.Image, padding: int = 2) -> Image.Image:
    return ImageOps.expand(mask, border=padding, fill=0)


def make_cloud_assets(components: list[Component]) -> list[Asset]:
    min_size = min(sqrt(component.area) for component in components)
    max_size = max(sqrt(component.area) for component in components)
    assets: list[Asset] = []

    for index, component in enumerate(components, start=1):
        relative = (sqrt(component.area) - min_size) / (max_size - min_size)
        target_width = round(CLOUD_MIN_WIDTH + relative * (CLOUD_MAX_WIDTH - CLOUD_MIN_WIDTH))
        solid = pad_mask(resize_binary(component.mask, target_width))
        outline_radius = 2 if target_width >= 52 else 1
        eroded = solid.filter(ImageFilter.MinFilter((outline_radius * 2) + 1))
        outline = Image.frombytes(
            "L",
            solid.size,
            bytes(max(0, outer - inner) for outer, inner in zip(solid.tobytes(), eroded.tobytes())),
        )
        if outline.getbbox() is None or ImageChops_equal(outline, solid):
            raise ValueError(f"cloud {index}: invalid transparent outline")
        assets.append(Asset(f"home_sky_cloud_{index:02d}", outline))
    return assets


def make_bird_assets(components: list[Component]) -> list[Asset]:
    assets: list[Asset] = []
    for index, component in enumerate(components, start=1):
        target_width = min(BIRD_MAX_WIDTH, max(BIRD_MIN_WIDTH, component.mask.width))
        solid = pad_mask(resize_binary(component.mask, target_width), padding=1)
        if solid.getbbox() is None:
            raise ValueError(f"bird {index}: empty mask")
        assets.append(Asset(f"home_sky_bird_{index:02d}", solid))
    return assets


def ImageChops_equal(left: Image.Image, right: Image.Image) -> bool:
    return left.tobytes() == right.tobytes()


def format_bytes(data: bytes) -> str:
    return "\n".join(
        "    " + ", ".join(f"0x{value:02X}" for value in data[offset : offset + 16]) + ","
        for offset in range(0, len(data), 16)
    )


def generate_header() -> str:
    return '''#ifndef __HOME_SKY_OBJECTS_RES_H__
#define __HOME_SKY_OBJECTS_RES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "image/egui_image_std.h"

typedef struct
{
    const egui_image_std_t *image;
    uint16_t width;
    uint16_t height;
} home_sky_asset_t;

#if EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_ALPHA_8
uint8_t home_sky_cloud_count(void);
const home_sky_asset_t *home_sky_cloud_get(uint8_t index);
uint8_t home_sky_bird_count(void);
const home_sky_asset_t *home_sky_bird_get(uint8_t index);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __HOME_SKY_OBJECTS_RES_H__ */
'''


def generate_source(clouds: list[Asset], birds: list[Asset]) -> str:
    lines = [
        '#include "gui/resources/home_sky_objects_res.h"',
        "",
        "#include <stddef.h>",
        "",
        "#if EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_ALPHA_8",
        "",
        "// Generated by tools/generate_home_sky_objects.py. Do not edit by hand.",
    ]

    for asset in clouds + birds:
        lines += [
            "",
            f"static const uint8_t {asset.name}_alpha[] = {{",
            format_bytes(asset.mask.tobytes()),
            "};",
            "",
            f"static const egui_image_std_info_t {asset.name}_info = {{",
            f"    .data_buf = {asset.name}_alpha,",
            "    .alpha_buf = NULL,",
            "    .data_type = EGUI_IMAGE_DATA_TYPE_ALPHA,",
            "    .alpha_type = EGUI_IMAGE_ALPHA_TYPE_8,",
            "    .res_type = EGUI_RESOURCE_TYPE_INTERNAL,",
            f"    .width = {asset.mask.width},",
            f"    .height = {asset.mask.height},",
            "};",
            "",
            f"extern const egui_image_std_t {asset.name};",
            f"EGUI_IMAGE_SUB_DEFINE_CONST(egui_image_std_t, {asset.name}, &{asset.name}_info);",
        ]

    def append_table(name: str, assets: list[Asset]) -> None:
        lines.extend(["", f"static const home_sky_asset_t s_{name}_assets[] = {{"])
        lines.extend(
            f"    {{&{asset.name}, {asset.mask.width}U, {asset.mask.height}U}}," for asset in assets
        )
        lines.extend(["};", ""])

    append_table("cloud", clouds)
    append_table("bird", birds)
    lines += [
        "uint8_t home_sky_cloud_count(void)",
        "{",
        "    return (uint8_t)(sizeof(s_cloud_assets) / sizeof(s_cloud_assets[0]));",
        "}",
        "",
        "const home_sky_asset_t *home_sky_cloud_get(uint8_t index)",
        "{",
        "    return (index < home_sky_cloud_count()) ? &s_cloud_assets[index] : NULL;",
        "}",
        "",
        "uint8_t home_sky_bird_count(void)",
        "{",
        "    return (uint8_t)(sizeof(s_bird_assets) / sizeof(s_bird_assets[0]));",
        "}",
        "",
        "const home_sky_asset_t *home_sky_bird_get(uint8_t index)",
        "{",
        "    return (index < home_sky_bird_count()) ? &s_bird_assets[index] : NULL;",
        "}",
        "",
        "#endif /* EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_ALPHA_8 */",
        "",
    ]
    return "\n".join(lines)


def rgba_asset(mask: Image.Image) -> Image.Image:
    image = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    image.putalpha(mask)
    return image


def write_pngs_and_preview(clouds: list[Asset], birds: list[Asset]) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for stale in OUTPUT_DIR.glob("cloud_*.png"):
        stale.unlink()
    for stale in OUTPUT_DIR.glob("bird_*.png"):
        stale.unlink()

    for index, asset in enumerate(clouds, start=1):
        rgba_asset(asset.mask).save(OUTPUT_DIR / f"cloud_{index:02d}.png")
    for index, asset in enumerate(birds, start=1):
        rgba_asset(asset.mask).save(OUTPUT_DIR / f"bird_{index:02d}.png")

    cell_width = 92
    cell_height = 64
    columns = 6
    rows = (len(clouds) + columns - 1) // columns + (len(birds) + columns - 1) // columns
    preview = Image.new("RGB", (columns * cell_width, rows * cell_height), "white")
    draw = ImageDraw.Draw(preview)
    items = [("C", index, asset) for index, asset in enumerate(clouds, start=1)]
    cloud_rows = (len(clouds) + columns - 1) // columns
    items += [("B", index, asset) for index, asset in enumerate(birds, start=1)]

    for item_index, (kind, index, asset) in enumerate(items):
        if kind == "C":
            row, column = divmod(item_index, columns)
        else:
            bird_index = item_index - len(clouds)
            bird_row, column = divmod(bird_index, columns)
            row = cloud_rows + bird_row
        x = column * cell_width
        y = row * cell_height
        draw.rectangle((x, y, x + cell_width - 1, y + cell_height - 1), outline=(220, 220, 220))
        px = x + (cell_width - asset.mask.width) // 2
        py = y + 14 + (cell_height - 18 - asset.mask.height) // 2
        preview.paste((0, 0, 0), (px, py, px + asset.mask.width, py + asset.mask.height), asset.mask)
        draw.text((x + 4, y + 2), f"{kind}{index:02d}", fill=(80, 80, 80))
    preview.save(PREVIEW_PATH)


def validate_assets(clouds: list[Asset], birds: list[Asset]) -> None:
    if len(clouds) != EXPECTED_CLOUDS or len(birds) != EXPECTED_BIRDS:
        raise ValueError("unexpected generated asset count")
    for asset in clouds + birds:
        if asset.mask.mode != "L" or asset.mask.getbbox() is None:
            raise ValueError(f"{asset.name}: invalid Alpha8 mask")
        if set(asset.mask.tobytes()) - {0, 255}:
            raise ValueError(f"{asset.name}: mask is not binary")
    for asset in clouds:
        opaque = sum(1 for value in asset.mask.tobytes() if value != 0)
        if opaque >= (asset.mask.width * asset.mask.height) // 2:
            raise ValueError(f"{asset.name}: cloud interior is not transparent")


def main() -> None:
    clouds = make_cloud_assets(extract_clouds())
    birds = make_bird_assets(extract_birds())
    validate_assets(clouds, birds)
    write_pngs_and_preview(clouds, birds)
    HEADER_PATH.write_text(generate_header(), encoding="ascii", newline="\n")
    SOURCE_PATH.write_text(generate_source(clouds, birds), encoding="ascii", newline="\n")
    print(
        f"generated {len(clouds)} clouds and {len(birds)} birds; "
        f"preview: {PREVIEW_PATH.relative_to(ROOT)}"
    )


if __name__ == "__main__":
    main()
