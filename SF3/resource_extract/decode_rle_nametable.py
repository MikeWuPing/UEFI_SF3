"""
decode_rle_nametable.py - Decode RLE-compressed nametable from Street Fighter III (NES)

Decodes the sub_FC16 RLE format used by the original game's background loader.
Input: RLE data bytes from off_0x0122D7_0C (byte 18+, after CHR bank + palette)
Output: 1024-byte nametable (960 tiles + 64 attribute bytes) as C array

RLE Command Types (from bank_FF.asm sub_FC16):
  $00       : End of data
  $01-$7D   : Literal run - write cmd literal bytes from stream
  $7E       : Alternating pair fill - cnt pairs of (tile_A, tile_B)
  $7F       : Multi-row incrementing fill
  $80-$BF   : Constant fill - write (cmd&$3F) copies of next byte (0->$40)
  $C0-$FF   : Incrementing fill - write (cmd&$3F) bytes: start, start+1, ... (0->$40)
"""

# RLE data from bank_09.asm off_0x0122D7_0C, byte 18 onwards
# (after CHR bank byte $D4 and 17 palette bytes)
RLE_DATA = [
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x88, 0x00, 0xCF, 0x01, 0x90, 0x00, 0xD1, 0x10, 0x8F, 0x00,
    0xCD, 0x21, 0x01, 0x00, 0xC3, 0x2E, 0x01, 0x00, 0xC3, 0x31, 0x8C, 0x00, 0xCC, 0x34, 0x01, 0x00,
    0xC7, 0x40, 0x8C, 0x00, 0xCC, 0x47, 0x01, 0x00, 0xC5, 0x53, 0x8C, 0x00, 0xCF, 0x58, 0x01, 0x00,
    0xC4, 0x67, 0x8B, 0x00, 0xD0, 0x6B, 0x01, 0x00, 0xC4, 0x7B, 0x8C, 0x00, 0xCF, 0x7F, 0x01, 0x00,
    0xC4, 0x8E, 0x8C, 0x00, 0xCF, 0x92, 0x01, 0x00, 0xC4, 0xA1, 0x8C, 0x00, 0xCF, 0xA5, 0x01, 0x00,
    0xC5, 0xB4, 0x8B, 0x00, 0xCD, 0xB9, 0x01, 0x46, 0xC8, 0xC6, 0x8A, 0x00, 0xCD, 0xCE, 0x01, 0x00,
    0xC6, 0xDB, 0x8D, 0x00, 0xC4, 0xE1, 0x01, 0x00, 0xC2, 0xE5, 0x99, 0x00, 0x01, 0xE7, 0xBF, 0x00,
    0xC4, 0x01, 0x02, 0x00, 0x03, 0xC3, 0x05, 0x04, 0x05, 0x00, 0x05, 0x08, 0xC2, 0x00, 0x03, 0x09,
    0x06, 0x0A, 0xAE, 0x00, 0x06, 0x0B, 0x06, 0x03, 0x0A, 0x00, 0x08, 0xC7, 0x0C, 0x02, 0x00, 0x04,
    0xC2, 0x06, 0x01, 0x13, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x87, 0x00, 0x87, 0xFF, 0x04, 0xCC,
    0xFF, 0x77, 0x5D, 0x83, 0x5F, 0x09, 0xFF, 0xCC, 0xFF, 0xFB, 0xFA, 0x3F, 0x4F, 0x55, 0xFD, 0x82,
    0xFF, 0x02, 0x73, 0xE4, 0x82, 0xF5, 0x01, 0xF9, 0x8A, 0xFF, 0x88, 0xF0, 0x88, 0x0F, 0x88, 0x00,
    0x00,
]


def decode_rle_nametable(data):
    """Decode sub_FC16 RLE format into 1024-byte nametable."""
    output = []
    pos = 0

    while pos < len(data):
        cmd = data[pos]

        # $00: terminator
        if cmd == 0x00:
            pos += 1
            break

        # $01-$7D: literal run
        elif cmd <= 0x7D:
            count = cmd
            pos += 1
            for _ in range(count):
                if pos < len(data):
                    output.append(data[pos] & 0xFF)
                    pos += 1

        # $7E: alternating pair fill
        elif cmd == 0x7E:
            pos += 1  # skip $7E
            count = data[pos]; pos += 1
            tile_a = data[pos]; pos += 1
            tile_b = data[pos]; pos += 1
            for _ in range(count):
                output.append(tile_a & 0xFF)
                output.append(tile_b & 0xFF)

        # $7F: multi-row incrementing fill
        elif cmd == 0x7F:
            pos += 1  # skip $7F
            param = data[pos]; pos += 1
            tiles_per_row = param & 0x0F
            if tiles_per_row == 0:
                tiles_per_row = 16
            row_counter = (param & 0xF0) >> 1  # high nibble, LSR once
            start_val = data[pos]; pos += 1
            while True:
                val = start_val  # reset each row (ASM re-reads same Y)
                for _ in range(tiles_per_row):
                    output.append(val & 0xFF)
                    val = (val + 1) & 0xFF
                row_counter -= 8
                if row_counter < 0:  # BMI = branch if negative
                    break

        # $80-$BF: constant fill (RLE)
        elif cmd <= 0xBF:
            count = cmd & 0x3F
            if count == 0:
                count = 0x40
            pos += 1
            fill_val = data[pos]; pos += 1
            for _ in range(count):
                output.append(fill_val & 0xFF)

        # $C0-$FF: incrementing fill
        else:  # cmd >= 0xC0
            count = cmd & 0x3F
            if count == 0:
                count = 0x40
            pos += 1
            start_val = data[pos]; pos += 1
            val = start_val
            for _ in range(count):
                output.append(val & 0xFF)
                val = (val + 1) & 0xFF

    return output


def format_c_array(data, name, per_line=16):
    """Format byte list as C array initializer."""
    lines = []
    lines.append(f"STATIC CONST UINT8 {name}[PPU_NAMETABLE_SIZE] = {{")

    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        # Add row comment
        if i < 960:
            row = i // 32
            comment = f"  /* row {row:2d} */"
        elif i == 960:
            comment = "  /* attribute table */"
        else:
            comment = ""
        trailing = "," if i + per_line < len(data) else ""
        lines.append(f"  {hex_vals}{trailing}{comment}")

    lines.append("};")
    return "\n".join(lines)


def main():
    output = decode_rle_nametable(RLE_DATA)

    print(f"Decoded {len(output)} bytes (expected 1024)")

    if len(output) != 1024:
        print(f"WARNING: Expected 1024 bytes, got {len(output)}")
        # Pad or truncate
        if len(output) < 1024:
            output.extend([0] * (1024 - len(output)))
            print(f"Padded with zeros to 1024")
        else:
            output = output[:1024]
            print(f"Truncated to 1024")

    # Print stats
    tiles = output[:960]
    attrs = output[960:]
    non_zero_tiles = sum(1 for t in tiles if t != 0)
    non_zero_attrs = sum(1 for a in attrs if a != 0)
    print(f"Tiles: {non_zero_tiles}/960 non-zero")
    print(f"Attributes: {non_zero_attrs}/64 non-zero")

    # Generate C array
    c_code = format_c_array(output, "gTitleNametable")

    # Write to output file
    out_path = r"D:\AIProject\street_Fighter_III\SFC3\resource_extract\title_nametable.inc"
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(f"/* Auto-generated by decode_rle_nametable.py */\n")
        f.write(f"/* Source: bank_09.asm off_0x0122D7_0C byte 18+ (RLE nametable) */\n")
        f.write(f"/* Decoded via sub_FC16 algorithm from bank_FF.asm */\n\n")
        f.write(c_code)
        f.write("\n")

    print(f"\nC array written to: {out_path}")

    # Also print first few rows as hex dump for verification
    print("\nFirst 8 rows (tiles 0-255):")
    for row in range(8):
        row_data = tiles[row*32:(row+1)*32]
        hex_str = " ".join(f"{b:02X}" for b in row_data)
        print(f"  Row {row:2d}: {hex_str}")

    print("\nAttribute table (64 bytes):")
    for row in range(8):
        row_data = attrs[row*8:(row+1)*8]
        hex_str = " ".join(f"{b:02X}" for b in row_data)
        print(f"  {hex_str}")


if __name__ == "__main__":
    main()
