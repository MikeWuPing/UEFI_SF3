#!/usr/bin/env python3
"""
Decode all screen nametables directly from orgrom.nes binary.
Implements the exact ASM RLE algorithm from sub_FC16 (bank_FF.asm).

Outputs C arrays suitable for background_data.c.
"""
import struct
import sys
import os

ROM_PATH = os.path.join(os.path.dirname(__file__), '..', '..', 'Ref', 'orgrom.nes')

# PRG bank layout in ROM file (after 16-byte header)
# Banks stored sequentially: 00,01,02,03,04,05,06,07,08,09,0A,0B,0D,FF
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
    0x0A: (0x8000, '09'),  # $A000 - $2000
    0x0B: (0x803F, '09'),  # $A03F - $2000
    0x0C: (0x82C7, '09'),  # $A2C7 - $2000
    0x0D: (0x83D6, '09'),  # $A3D6 - $2000
    0x0E: (0x9C5F, '08'),
    0x0F: (0x9D2A, '08'),
}

def rom_offset(cpu_addr, bank_name):
    """Convert CPU address + bank to ROM file offset (including 16-byte header)."""
    bank_start = BANK_START_ADDRS[BANK_NAMES.index(bank_name)]
    return 16 + BANK_ROM_OFFSETS[bank_name] + (cpu_addr - bank_start)

def decode_rle(data, pos):
    """
    Exact reimplementation of sub_FC16 from bank_FF.asm.
    Returns (decoded_bytes, new_pos).
    """
    output = []
    while pos < len(data):
        cmd = data[pos]
        if cmd == 0x00:
            pos += 1
            break

        if cmd >= 0x80:
            # Upper range
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
            # Lower range $01-$7F
            if cmd == 0x7F:
                # Multi-row incrementing fill
                pos += 1
                param = data[pos]
                tiles_per_row = param & 0x0F
                if tiles_per_row == 0:
                    tiles_per_row = 0x10
                row_counter = (param & 0xF0) >> 1  # high nibble * 8
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
                # ASM uses do-while: body executes at least once
                # count=0 → 256 iterations (unsigned wraparound)
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
    """Decode a single screen's data from ROM."""
    if screen_id not in SCREEN_TABLE:
        return None
    cpu_addr, bank = SCREEN_TABLE[screen_id]
    offset = rom_offset(cpu_addr, bank)

    # Byte 0: CHR bank
    chr_bank = rom_data[offset]

    # Bytes 1-17: palette (17 bytes to $3F00-$3F10)
    palette = list(rom_data[offset+1:offset+18])

    # Pad palette to 32 bytes (sprite palettes = 0x00)
    while len(palette) < 32:
        palette.append(0x00)

    # Byte 18+: RLE nametable data
    rle_start = offset + 18
    decoded, _ = decode_rle(rom_data, rle_start)

    # First 1024 bytes = nametable 0 (960 tiles + 64 attributes)
    nametable = decoded[:1024] if len(decoded) >= 1024 else decoded + b'\x00' * (1024 - len(decoded))

    return {
        'chr_bank': chr_bank,
        'palette': palette,
        'nametable': nametable,
        'decoded_len': len(decoded),
    }

def format_c_array(name, data, per_line=16, comment_prefix=''):
    """Format a byte array as C code."""
    lines = [f'STATIC CONST UINT8 {name}[{len(data)}] = {{']
    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        row_num = i // 32 if per_line == 16 and len(data) > 960 else i // per_line
        if comment_prefix and i < 960 and per_line == 16:
            # Add row comment for nametable tile data
            row = i // 32
            lines.append(f'  {hex_str},  /* {comment_prefix}{row:2d} */')
        elif comment_prefix and i >= 960 and per_line == 8:
            lines.append(f'  {hex_str},')
        else:
            lines.append(f'  {hex_str},')
    lines.append('};')
    return '\n'.join(lines)

def main():
    with open(ROM_PATH, 'rb') as f:
        rom_data = f.read()

    print(f'ROM size: {len(rom_data)} bytes')
    print(f'Header: {rom_data[:4]}')

    # Screens we actually use
    used_screens = [0x00, 0x0A, 0x0B, 0x0C]

    for sid in used_screens:
        result = decode_screen(rom_data, sid)
        if result is None:
            print(f'Screen 0x{sid:02X}: NOT FOUND')
            continue

        print(f'\n=== Screen 0x{sid:02X} ===')
        print(f'CHR bank: 0x{result["chr_bank"]:02X}')
        print(f'Palette (17 bytes): {" ".join(f"{b:02X}" for b in result["palette"][:17])}')
        print(f'Decoded RLE length: {result["decoded_len"]} bytes')

        nt = result['nametable']
        tiles = nt[:960]
        attrs = nt[960:1024]
        print(f'Tiles (960 bytes): first 32 = {" ".join(f"{b:02X}" for b in tiles[:32])}')
        print(f'Attributes (64 bytes):')
        for row in range(8):
            row_data = attrs[row*8:(row+1)*8]
            print(f'  row {row}: {" ".join(f"{b:02X}" for b in row_data)}')

    # Now generate the C output
    print('\n\n========== C CODE OUTPUT ==========\n')

    screen_names = {
        0x00: ('Arena0', 'gArena0Palette', 'gArena0Nametable'),
        0x0A: ('Vs', 'gVsPalette', 'gVsNametable'),
        0x0B: ('Select', 'gSelectPalette', 'gSelectNametable'),
        0x0C: ('Title', 'gTitlePalette', 'gTitleNametable'),
    }

    for sid in used_screens:
        result = decode_screen(rom_data, sid)
        if result is None:
            continue
        label, pal_name, nt_name = screen_names[sid]

        # Palette array
        print(f'/* {label} palette - decoded from ROM screen 0x{sid:02X} */')
        print(f'STATIC CONST UINT8 {pal_name}[PPU_PALETTE_SIZE] = {{')
        pal = result['palette']
        for i in range(0, 32, 4):
            chunk = pal[i:i+4]
            comments = ['BG palette 0', 'BG palette 1', 'BG palette 2', 'BG palette 3',
                        'Sprite palette 0', 'Sprite palette 1', 'Sprite palette 2', 'Sprite palette 3']
            cmt = comments[i//4] if i//4 < len(comments) else ''
            print(f'  0x{chunk[0]:02X}, 0x{chunk[1]:02X}, 0x{chunk[2]:02X}, 0x{chunk[3]:02X},  /* {cmt} */')
        print(f'}};')
        print()

        # Nametable array
        nt = result['nametable']
        print(f'/* {label} nametable - decoded from ROM screen 0x{sid:02X} */')
        print(f'STATIC CONST UINT8 {nt_name}[PPU_NAMETABLE_SIZE] = {{')
        # Tile indices
        for row in range(30):
            row_data = nt[row*32:(row+1)*32]
            hex1 = ', '.join(f'0x{b:02X}' for b in row_data[:16])
            hex2 = ', '.join(f'0x{b:02X}' for b in row_data[16:32])
            print(f'  {hex1},  /* row {row:2d} */')
            print(f'  {hex2},')
        # Attribute table
        print(f'  /* attribute table (64 bytes) */')
        attrs = nt[960:1024]
        for row in range(8):
            row_data = attrs[row*8:(row+1)*8]
            hex_str = ', '.join(f'0x{b:02X}' for b in row_data)
            print(f'  {hex_str},')
        print(f'}};')
        print()

    # CHR bank table
    print('/* CHR bank values */')
    for sid in used_screens:
        result = decode_screen(rom_data, sid)
        if result:
            label = screen_names[sid][0]
            print(f'  /* 0x{sid:02X} ({label}): */ 0x{result["chr_bank"]:02X},')

if __name__ == '__main__':
    main()
