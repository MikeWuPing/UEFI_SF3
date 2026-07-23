"""Decode RLE nametable for screen 0x0B (character select)."""

RLE_DATA_0B = [
    0x80, 0x00, 0x8C, 0x00, 0xC4, 0x01, 0x01, 0x01, 0x98, 0x00, 0xD0, 0x05, 0x8C, 0x00, 0xC8, 0x15,
    0x84, 0x0A, 0xC2, 0x1D, 0x83, 0x0A, 0xC3, 0x1F, 0x8E, 0x00, 0xC3, 0x22, 0x83, 0x0A, 0xC7, 0x25,
    0x82, 0x0A, 0xC3, 0x2C, 0x8E, 0x00, 0x02, 0x2F, 0x09, 0xC3, 0x30, 0x83, 0x0A, 0xC5, 0x33, 0x82,
    0x0A, 0xC3, 0x38, 0x8E, 0x00, 0xC5, 0x3B, 0x82, 0x0A, 0xC3, 0x40, 0x82, 0x00, 0xC2, 0x43, 0x82,
    0x0A, 0xC2, 0x45, 0x8E, 0x00, 0x03, 0x47, 0x0A, 0x0A, 0xC3, 0x48, 0x03, 0x0A, 0x4B, 0x4C, 0x84,
    0x00, 0xC5, 0x4D, 0x8E, 0x00, 0xC2, 0x52, 0x01, 0x0A, 0xC8, 0x54, 0x82, 0x00, 0xC4, 0x5C, 0x90,
    0x00, 0x02, 0x60, 0x0A, 0xC8, 0x61, 0x83, 0x00, 0xC4, 0x69, 0x8F, 0x00, 0x03, 0x6D, 0x4A, 0x6E,
    0x84, 0x00, 0x05, 0x6F, 0x0A, 0x70, 0x00, 0x00, 0xC5, 0x71, 0x90, 0x00, 0xC2, 0x76, 0x84, 0x00,
    0xC3, 0x78, 0x83, 0x00, 0xC2, 0x7B, 0x02, 0x0A, 0x7D, 0x9D, 0x00, 0xC3, 0x7E, 0x9D, 0x00, 0x02,
    0x76, 0x81, 0xB2, 0x00, 0xCC, 0x82, 0x94, 0x00, 0xCC, 0x8E, 0x94, 0x00, 0xCB, 0x9A, 0x01, 0x99,
    0x94, 0x00, 0xCB, 0xA5, 0x01, 0x99, 0x94, 0x00, 0xCC, 0xB0, 0x94, 0x00, 0xCC, 0xBC, 0x94, 0x00,
    0xCC, 0xC8, 0x94, 0x00, 0xCC, 0xD4, 0x94, 0x00, 0xCC, 0xE0, 0x94, 0x00, 0xCC, 0xEC, 0x94, 0x00,
    0xCC, 0x01, 0x94, 0x00, 0xCC, 0x0D, 0x80, 0x00, 0x8C, 0x00, 0x84, 0xF0, 0x04, 0x30, 0x00, 0x00,
    0xCD, 0x84, 0xFF, 0x09, 0x33, 0x00, 0x00, 0x0C, 0xFF, 0xFF, 0x31, 0xFF, 0x33, 0x83, 0x00, 0x05,
    0x0F, 0x0C, 0x03, 0xCF, 0x03, 0x84, 0x00, 0x03, 0x88, 0x66, 0x11, 0x84, 0x00, 0x03, 0x88, 0xEE,
    0x33, 0x86, 0x00, 0x03, 0x44, 0x99, 0x22, 0x8A, 0x00, 0x00,
]


def decode_rle_nametable(data):
    """Decode sub_FC16 RLE format into 1024-byte nametable."""
    output = []
    pos = 0
    while pos < len(data):
        cmd = data[pos]
        if cmd == 0x00:
            pos += 1
            break
        elif cmd <= 0x7D:
            count = cmd
            pos += 1
            for _ in range(count):
                if pos < len(data):
                    output.append(data[pos] & 0xFF)
                    pos += 1
        elif cmd == 0x7E:
            # Alternating pair fill: write (tile_a, tile_b) count times
            pos += 1
            count = data[pos]; pos += 1
            tile_a = data[pos]; pos += 1
            tile_b = data[pos]; pos += 1
            for _ in range(count):
                output.append(tile_a & 0xFF)
                output.append(tile_b & 0xFF)
        elif cmd == 0x7F:
            # Multi-row fill: same incrementing pattern repeated per row
            # ASM: sub_FCC0 reads start_val from (ram_0000),Y without
            # advancing Y, so each row restarts from the same start_val.
            pos += 1
            param = data[pos]; pos += 1
            tiles_per_row = param & 0x0F
            if tiles_per_row == 0:
                tiles_per_row = 16
            row_counter = (param & 0xF0) >> 1
            start_val = data[pos]; pos += 1
            while True:
                val = start_val  # reset each row (ASM re-reads same Y)
                for _ in range(tiles_per_row):
                    output.append(val & 0xFF)
                    val = (val + 1) & 0xFF
                row_counter -= 8
                if row_counter < 0:
                    break
        elif cmd <= 0xBF:
            count = cmd & 0x3F
            if count == 0:
                count = 0x40
            pos += 1
            fill_val = data[pos]; pos += 1
            for _ in range(count):
                output.append(fill_val & 0xFF)
        else:
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
    lines = []
    lines.append(f"STATIC CONST UINT8 {name}[PPU_NAMETABLE_SIZE] = {{")
    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
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
    output = decode_rle_nametable(RLE_DATA_0B)
    print(f"Decoded {len(output)} bytes (expected 1024)")
    if len(output) != 1024:
        print(f"WARNING: Expected 1024 bytes, got {len(output)}")
        if len(output) < 1024:
            output.extend([0] * (1024 - len(output)))
        else:
            output = output[:1024]

    tiles = output[:960]
    attrs = output[960:]
    non_zero_tiles = sum(1 for t in tiles if t != 0)
    non_zero_attrs = sum(1 for a in attrs if a != 0)
    print(f"Tiles: {non_zero_tiles}/960 non-zero")
    print(f"Attributes: {non_zero_attrs}/64 non-zero")

    c_code = format_c_array(output, "gSelectNametable")
    out_path = r"D:\AIProject\street_Fighter_III\SFC3\resource_extract\select_nametable.inc"
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(f"/* Auto-generated by decode_select_screen.py */\n")
        f.write(f"/* Source: bank_09.asm off_0x01204F_0B byte 18+ (RLE nametable) */\n\n")
        f.write(c_code)
        f.write("\n")
    print(f"C array written to: {out_path}")

    # Print first few rows
    print("\nFirst 8 rows:")
    for row in range(8):
        row_data = tiles[row*32:(row+1)*32]
        hex_str = " ".join(f"{b:02X}" for b in row_data)
        print(f"  Row {row:2d}: {hex_str}")


if __name__ == "__main__":
    main()
