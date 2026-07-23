#!/usr/bin/env python3
"""
Generate background_data.c directly from orgrom.nes binary.
Implements the exact ASM RLE algorithm from sub_FC16 (bank_FF.asm).

This script reads the ROM, decodes all screen nametables and palettes,
and outputs a complete C source file for background_data.c.
"""
import struct
import sys
import os
import datetime

ROM_PATH = os.path.join(os.path.dirname(__file__), '..', '..', 'Ref', 'orgrom.nes')
OUTPUT_PATH = os.path.join(os.path.dirname(__file__), '..', 'background_data.c')

# PRG bank layout in ROM file (after 16-byte header)
BANK_SIZES = [0x2000]*7 + [0x2000] + [0x2000]*3 + [0x4000] + [0x2000] + [0x4000]
BANK_NAMES = ['00','01','02','03','04','05','06','07','08','09','0A','0B','0D','FF']
BANK_START_ADDRS = [0x8000]*7 + [0xA000] + [0x8000]*3 + [0x8000] + [0xA000] + [0xC000]

def compute_bank_offsets():
    offsets = {}
    pos = 0
    for i, name in enumerate(BANK_NAMES):
        offsets[name] = pos
        pos += BANK_SIZES[i]
    return offsets

BANK_ROM_OFFSETS = compute_bank_offsets()

# Screen table: screen_id -> (cpu_addr, bank_name)
SCREEN_TABLE = {
    0x00: (0x8051, '08'),
    0x01: (0x836D, '08'),
    0x02: (0x8592, '08'),
    0x03: (0x89E7, '08'),
    0x04: (0x8CE2, '08'),
    0x05: (0x8FF0, '08'),
    0x06: (0x92DB, '08'),
    0x07: (0x95E7, '08'),
    0x08: (0x991D, '08'),
    0x09: (0x802E, '08'),
    0x0A: (0x8000, '09'),
    0x0B: (0x803F, '09'),
    0x0C: (0x82C7, '09'),
    0x0D: (0x83D6, '09'),
    0x0E: (0x9C5F, '08'),
    0x0F: (0x9D2A, '08'),
}

# Screen names for C arrays
SCREEN_NAMES = {
    0x00: ('Arena0', 'gArena0Palette', 'gArena0Nametable'),
    0x0A: ('Vs', 'gVsPalette', 'gVsNametable'),
    0x0B: ('Select', 'gSelectPalette', 'gSelectNametable'),
    0x0C: ('Title', 'gTitlePalette', 'gTitleNametable'),
}

USED_SCREENS = [0x00, 0x0A, 0x0B, 0x0C]

def rom_offset(cpu_addr, bank_name):
    bank_start = BANK_START_ADDRS[BANK_NAMES.index(bank_name)]
    return 16 + BANK_ROM_OFFSETS[bank_name] + (cpu_addr - bank_start)

def decode_rle(data, pos):
    """Exact reimplementation of sub_FC16 from bank_FF.asm."""
    output = []
    while pos < len(data):
        cmd = data[pos]
        if cmd == 0x00:
            pos += 1
            break

        if cmd >= 0x80:
            if cmd & 0x40:
                # $C0-$FF: incrementing run
                count = cmd & 0x3F
                if count == 0:
                    count = 0x40
                pos += 1
                start_val = data[pos]
                val = start_val
                for _ in range(count):
                    output.append(val & 0xFF)
                    val = (val + 1) & 0xFF
                pos += 1
            else:
                # $80-$BF: constant fill
                count = cmd & 0x3F
                if count == 0:
                    count = 0x40
                pos += 1
                fill_val = data[pos]
                output.extend([fill_val] * count)
                pos += 1
        else:
            if cmd == 0x7F:
                # Multi-row incrementing fill
                pos += 1
                param = data[pos]
                tiles_per_row = param & 0x0F
                if tiles_per_row == 0:
                    tiles_per_row = 0x10
                row_counter = (param & 0xF0) >> 1
                pos += 1
                start_val = data[pos]
                while True:
                    val = start_val
                    for _ in range(tiles_per_row):
                        output.append(val & 0xFF)
                        val = (val + 1) & 0xFF
                    row_counter -= 8
                    if row_counter < 0:
                        break
                pos += 1
            elif cmd == 0x7E:
                # Alternating pair fill
                pos += 1
                count = data[pos]
                pos += 1
                tile_a = data[pos]
                pos += 1
                tile_b = data[pos]
                if count == 0:
                    count = 256
                for _ in range(count):
                    output.append(tile_a)
                    output.append(tile_b)
                pos += 1
            else:
                # $01-$7D: literal run
                count = cmd
                pos += 1
                for _ in range(count):
                    output.append(data[pos])
                    pos += 1
    return bytes(output), pos

def decode_screen(rom_data, screen_id):
    if screen_id not in SCREEN_TABLE:
        return None
    cpu_addr, bank = SCREEN_TABLE[screen_id]
    offset = rom_offset(cpu_addr, bank)

    chr_bank = rom_data[offset]
    palette = list(rom_data[offset+1:offset+18])
    while len(palette) < 32:
        palette.append(0x00)

    rle_start = offset + 18
    decoded, _ = decode_rle(rom_data, rle_start)

    nametable = decoded[:1024] if len(decoded) >= 1024 else decoded + b'\x00' * (1024 - len(decoded))

    return {
        'chr_bank': chr_bank,
        'palette': palette,
        'nametable': nametable,
        'decoded_len': len(decoded),
    }

def generate_c_file(screens_data):
    """Generate the complete background_data.c file."""
    lines = []

    lines.append('/** @file')
    lines.append('  背景数据表 - 从原始 ROM (orgrom.nes) 自动解码生成')
    lines.append('')
    lines.append('  画面 ID 对应 (从 sub_E7E9_draw_screen 的 ram_screen 值):')
    lines.append('  0x00 = 战斗场景 1 (第一关竞技场)')
    lines.append('  0x0A = VS 过场画面')
    lines.append('  0x0B = 角色选择画面')
    lines.append('  0x0C = 标题画面')
    lines.append('')
    lines.append('  数据结构: 画面 ID 不连续, 使用 16 槽位平坦数组直接以画面 ID 为下标索引,')
    lines.append('  未使用的槽位填 NULL / 0。与原厂 sub_E7E9 中 ASL A 查跳转表语义一致。')
    lines.append('')
    lines.append(f'  自动生成时间: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}')
    lines.append('  生成脚本: resource_extract/generate_background_data.py')
    lines.append('  RLE 解码算法: sub_FC16 (bank_FF.asm $FC16)')
    lines.append('**/')
    lines.append('')
    lines.append('#include <Uefi.h>')
    lines.append('#include "nes_types.h"')
    lines.append('')

    # Generate palette and nametable arrays for each screen
    for sid in USED_SCREENS:
        data = screens_data[sid]
        label, pal_name, nt_name = SCREEN_NAMES[sid]

        # Palette
        lines.append(f'/* {label} 调色板 - ROM screen 0x{sid:02X}, CHR bank 0x{data["chr_bank"]:02X}')
        lines.append(f'   17 字节写入 $3F00-$3F10, $3F11-$3F1F 填 0x00 */')
        lines.append(f'STATIC CONST UINT8 {pal_name}[PPU_PALETTE_SIZE] = {{')
        pal = data['palette']
        comments = ['BG 调色板 0', 'BG 调色板 1', 'BG 调色板 2', 'BG 调色板 3',
                    'Sprite 调色板 0', 'Sprite 调色板 1', 'Sprite 调色板 2', 'Sprite 调色板 3']
        for i in range(0, 32, 4):
            chunk = pal[i:i+4]
            cmt = comments[i//4] if i//4 < len(comments) else ''
            lines.append(f'  0x{chunk[0]:02X}, 0x{chunk[1]:02X}, 0x{chunk[2]:02X}, 0x{chunk[3]:02X},  /* {cmt} */')
        lines.append('};')
        lines.append('')

        # Nametable
        nt = data['nametable']
        lines.append(f'/* {label} Nametable - ROM screen 0x{sid:02X}')
        lines.append(f'   RLE 解码长度: {data["decoded_len"]} 字节, 取前 1024 字节 (960 图块 + 64 属性) */')
        lines.append(f'STATIC CONST UINT8 {nt_name}[PPU_NAMETABLE_SIZE] = {{')
        lines.append('  /* 图块索引 (960 字节 = 32 列 x 30 行) */')

        # Tile data: 30 rows x 32 columns, output as 2 lines of 16 bytes per row
        for row in range(30):
            row_data = nt[row*32:(row+1)*32]
            hex1 = ', '.join(f'0x{b:02X}' for b in row_data[:16])
            hex2 = ', '.join(f'0x{b:02X}' for b in row_data[16:32])
            lines.append(f'  {hex1},  /* row {row:2d} */')
            lines.append(f'  {hex2},')

        # Attribute table
        lines.append('  /* 属性表 (64 字节 = 8x8, 每字节控制 2x2 图块区域的调色板) */')
        attrs = nt[960:1024]
        for row in range(8):
            row_data = attrs[row*8:(row+1)*8]
            hex_str = ', '.join(f'0x{b:02X}' for b in row_data)
            lines.append(f'  {hex_str},')
        lines.append('};')
        lines.append('')

    # CHR bank table
    lines.append('/* CHR bank 值表 - 每个画面的背景 CHR bank (写入 $6000, bank+1 写入 $6001) */')
    lines.append('STATIC CONST UINT8 gScreenChrBanks[16] = {')
    chr_banks = [0] * 16
    for sid in USED_SCREENS:
        chr_banks[sid] = screens_data[sid]['chr_bank']
    for i in range(0, 16, 8):
        chunk = chr_banks[i:i+8]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        sids = ', '.join(f'{j:02X}' for j in range(i, i+8))
        lines.append(f'  {hex_str},  /* {sids} */')
    lines.append('};')
    lines.append('')

    # Palette pointer table
    lines.append('/* 调色板指针表 - 以画面 ID 为下标索引 */')
    lines.append('STATIC CONST UINT8 * CONST gScreenPalettes[16] = {')
    for i in range(0, 16, 4):
        entries = []
        for j in range(i, i+4):
            if j in SCREEN_NAMES:
                entries.append(SCREEN_NAMES[j][1])
            else:
                entries.append('NULL')
        lines.append(f'  {", ".join(entries)},  /* {i:02X}-{i+3:02X} */')
    lines.append('};')
    lines.append('')

    # Nametable pointer table
    lines.append('/* Nametable 指针表 - 以画面 ID 为下标索引 */')
    lines.append('STATIC CONST UINT8 * CONST gScreenNametables[16] = {')
    for i in range(0, 16, 4):
        entries = []
        for j in range(i, i+4):
            if j in SCREEN_NAMES:
                entries.append(SCREEN_NAMES[j][2])
            else:
                entries.append('NULL')
        lines.append(f'  {", ".join(entries)},  /* {i:02X}-{i+3:02X} */')
    lines.append('};')
    lines.append('')

    return '\n'.join(lines)

def main():
    with open(ROM_PATH, 'rb') as f:
        rom_data = f.read()

    print(f'ROM size: {len(rom_data)} bytes')

    # Decode all screens
    screens_data = {}
    for sid in USED_SCREENS:
        result = decode_screen(rom_data, sid)
        if result is None:
            print(f'ERROR: Screen 0x{sid:02X} not found!')
            sys.exit(1)
        screens_data[sid] = result
        label = SCREEN_NAMES[sid][0]
        print(f'Screen 0x{sid:02X} ({label}): CHR=0x{result["chr_bank"]:02X}, '
              f'RLE decoded={result["decoded_len"]} bytes')

    # Verify nametable data
    for sid in USED_SCREENS:
        nt = screens_data[sid]['nametable']
        tiles = nt[:960]
        attrs = nt[960:1024]
        label = SCREEN_NAMES[sid][0]
        max_tile = max(tiles) if tiles else 0
        print(f'  {label}: max tile index = 0x{max_tile:02X} ({max_tile})')
        # Check attribute table
        non_zero_attrs = sum(1 for b in attrs if b != 0)
        print(f'  {label}: {non_zero_attrs}/64 non-zero attribute bytes')

    # Generate C file
    c_code = generate_c_file(screens_data)

    # Write with UTF-8 BOM (per project coding standard)
    with open(OUTPUT_PATH, 'w', encoding='utf-8-sig') as f:
        f.write(c_code)

    print(f'\nGenerated: {OUTPUT_PATH}')
    print(f'File size: {os.path.getsize(OUTPUT_PATH)} bytes')

    # Also print verification data for manual comparison
    print('\n=== VERIFICATION: Last 4 rows of each nametable ===')
    for sid in USED_SCREENS:
        nt = screens_data[sid]['nametable']
        label = SCREEN_NAMES[sid][0]
        print(f'\n{label} (0x{sid:02X}):')
        for row in range(26, 30):
            row_data = nt[row*32:(row+1)*32]
            hex_str = ' '.join(f'{b:02X}' for b in row_data)
            print(f'  row {row}: {hex_str}')
        attrs = nt[960:1024]
        for arow in range(6, 8):
            row_data = attrs[arow*8:(arow+1)*8]
            hex_str = ' '.join(f'{b:02X}' for b in row_data)
            print(f'  attr {arow}: {hex_str}')

if __name__ == '__main__':
    main()
