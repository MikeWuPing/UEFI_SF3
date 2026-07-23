#!/usr/bin/env python3
"""从 NES iNES ROM 中提取 PRG 和 CHR 数据"""

import struct
import os
import sys

def extract(nes_path, output_dir):
    with open(nes_path, 'rb') as f:
        header = f.read(16)
        if header[:4] != b'NES\x1a':
            print(f"错误: {nes_path} 不是有效的 iNES ROM")
            sys.exit(1)

        prg_banks = header[4]
        chr_banks = header[5]
        flags6 = header[6]
        flags7 = header[7]
        mapper = (flags7 & 0xF0) | (flags6 >> 4)

        prg_size = prg_banks * 16384
        chr_size = chr_banks * 8192

        print(f"ROM: {nes_path}")
        print(f"  Mapper: {mapper}")
        print(f"  PRG: {prg_banks} x 16KB = {prg_size} bytes ({prg_size//1024}KB)")
        print(f"  CHR: {chr_banks} x 8KB = {chr_size} bytes ({chr_size//1024}KB)")
        print(f"  Mirroring: {'Vertical' if flags6 & 1 else 'Horizontal'}")

        prg_data = f.read(prg_size)
        chr_data = f.read(chr_size)

    os.makedirs(output_dir, exist_ok=True)

    prg_path = os.path.join(output_dir, 'sfc3_prg.bin')
    chr_path = os.path.join(output_dir, 'sfc3_chr.bin')

    with open(prg_path, 'wb') as f:
        f.write(prg_data)
    with open(chr_path, 'wb') as f:
        f.write(chr_data)

    print(f"\n输出:")
    print(f"  PRG: {prg_path} ({len(prg_data)} bytes)")
    print(f"  CHR: {chr_path} ({len(chr_data)} bytes)")

    assert len(prg_data) == prg_size, f"PRG 大小不匹配: {len(prg_data)} != {prg_size}"
    assert len(chr_data) == chr_size, f"CHR 大小不匹配: {len(chr_data)} != {chr_size}"
    print(f"\n校验通过!")

if __name__ == '__main__':
    rom = os.path.join(os.path.dirname(__file__), '..', '..', 'Ref', 'orgrom.nes')
    out = os.path.join(os.path.dirname(__file__), '..', '..', 'qemu_disk')
    extract(os.path.abspath(rom), os.path.abspath(out))
