"""Validate the GTA SA native profile against a real PE32 executable."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_PROFILE = ROOT / "config" / "legacy" / "gta-san-andreas-native.json"


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def pe_layout(data: bytes) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    if data[:2] != b"MZ":
        raise ValueError("missing MZ header")
    pe = u32(data, 0x3C)
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    if u16(data, pe + 4) != 0x14C:
        raise ValueError("executable is not PE32/x86")
    optional = pe + 24
    if u16(data, optional) != 0x10B:
        raise ValueError("executable is not a PE32 optional header")
    image_base = u32(data, optional + 28)
    image_size = u32(data, optional + 56)
    section_count = u16(data, pe + 6)
    section_table = optional + u16(data, pe + 20)
    sections = []
    for index in range(section_count):
        offset = section_table + index * 40
        virtual_size = u32(data, offset + 8)
        virtual_address = u32(data, offset + 12)
        raw_size = u32(data, offset + 16)
        raw_pointer = u32(data, offset + 20)
        sections.append((virtual_address, virtual_size, raw_pointer, raw_size))
    return image_base, image_size, sections


def virtual_address_to_file_offset(
    address: int, image_base: int, sections: list[tuple[int, int, int, int]]
) -> int:
    rva = address - image_base
    for virtual_address, virtual_size, raw_pointer, raw_size in sections:
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            offset = raw_pointer + rva - virtual_address
            if offset >= raw_pointer and offset < raw_pointer + raw_size:
                return offset
    raise ValueError(f"address 0x{address:08X} is not backed by a PE section")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-exe", required=True, type=Path)
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    args = parser.parse_args()

    game = args.game_exe.read_bytes()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    errors: list[str] = []

    digest = hashlib.sha256(game).hexdigest()
    if digest.lower() != profile["game_sha256"].lower():
        errors.append(f"sha256 mismatch: {digest}")
    if len(game) != profile["image_size"]:
        errors.append(f"file size mismatch: {len(game)}")

    try:
        image_base, mapped_size, sections = pe_layout(game)
    except (IndexError, struct.error, ValueError) as exc:
        errors.append(str(exc))
        image_base = mapped_size = 0
        sections = []

    configured_base = int(profile["image_base"], 0)
    if image_base != configured_base:
        errors.append(f"image base mismatch: 0x{image_base:08X}")

    def in_image(address: int, size: int) -> bool:
        return configured_base <= address <= configured_base + mapped_size and size <= (
            configured_base + mapped_size - address
        )

    camera = int(profile["camera_address"], 0) + int(profile["camera_matrix_offset"], 0)
    if not in_image(camera, 0x48):
        errors.append("camera matrix is outside the mapped image")

    rw_camera = int(profile["camera_address"], 0) + int(profile["rw_camera_offset"], 0)
    if not in_image(rw_camera, 4):
        errors.append("RenderWare camera pointer is outside the mapped image")

    fov = int(profile["fov_address"], 0)
    if not in_image(fov, 4):
        errors.append("FOV value is outside the mapped image")

    aspect_ratio = int(profile["aspect_ratio_address"], 0)
    if not in_image(aspect_ratio, 4):
        errors.append("aspect-ratio value is outside the mapped image")

    active_addresses = {
        "copy_camera_matrix_to_rw_cam": 1,
        "set_rw_view_window": 1,
        "set_rw_view_offset": 1,
        "screen_dimensions": 8,
        "derive_camera": 1,
        "front_end_menu_active": 1,
        "fade_status": 1,
        "cutscene_running": 1,
        "cutscene_processing": 1,
        "player_pad": 0x134,
    }
    for address_key, size in active_addresses.items():
        if not in_image(int(profile[address_key], 0), size):
            errors.append(f"{address_key} is outside the mapped image")

    fixed_signatures = {
        "d3d9_delay_thunk": "d3d9_delay_thunk_expected",
        "pad_update_call": "pad_update_call_expected",
        "frontend_pad_update_call": "frontend_pad_update_call_expected",
        "find_player_ped": "find_player_ped_expected",
        "set_player_heading": "set_player_heading_expected",
        "find_player_vehicle": "find_player_vehicle_expected",
        "get_bone_position": "get_bone_position_expected",
        "ped_pre_render": "ped_pre_render_expected",
        "get_anim_hierarchy_from_clump": "get_anim_hierarchy_from_clump_expected",
        "rp_hanim_id_get_index": "rp_hanim_id_get_index_expected",
        "rp_hanim_get_matrix_array": "rp_hanim_get_matrix_array_expected",
        "hud_player_info": "hud_player_info_expected",
        "hud_wanted": "hud_wanted_expected",
        "hud_radar": "hud_radar_expected",
        "hud_vital_stats": "hud_vital_stats_expected",
        "font_print_string": "font_print_string_expected",
        "render_tail": "render_tail_expected",
        "render_tail_return": "render_tail_return_expected",
        "camera_size": "camera_size_expected",
    }
    for address_key, signature_key in fixed_signatures.items():
        address = int(profile[address_key], 0)
        expected = bytes.fromhex(profile[signature_key])
        if not in_image(address, len(expected)):
            errors.append(f"{address_key} is outside the mapped image")
            continue
        try:
            offset = virtual_address_to_file_offset(address, configured_base, sections)
            actual = game[offset : offset + len(expected)]
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if actual != expected:
            errors.append(
                f"{address_key} signature mismatch: expected {expected.hex(' ')}, "
                f"got {actual.hex(' ')}"
            )

    if errors:
        print("gtasa_native_profile: FAILED")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        "gtasa_native_profile: passed "
        f"sha256={digest} mapped_image=0x{mapped_size:X} fixed_signatures={len(fixed_signatures)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
