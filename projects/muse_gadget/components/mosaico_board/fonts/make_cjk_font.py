#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerates cjk_16.bin, the board's wide font for Muse's captions.

A 16 px, 4 bpp LVGL binary font (lv_font_conv's "bin" format, uncompressed)
of GB2312's 6763 hanzi, its punctuation and full-width rows (A1, A3) and the
em dash and middle dot as commonly typed. Glyphs come from Source Han Sans CN
Regular, under the SIL Open Font License (OFL.txt); this file is a modified
version of it in the OFL's sense, so it doesn't carry the reserved name.

    curl -LO https://github.com/adobe-fonts/source-han-sans/raw/release/SubsetOTF/CN/SourceHanSansCN-Regular.otf
    python3 make_cjk_font.py SourceHanSansCN-Regular.otf

Needs Node.js for npx. The OTF used for the committed font had SHA-256
e2bc8a2e7f37474b774fff8db758681ece40bb6947a90d571bce9dd60671a8e4.
"""
import os
import subprocess
import sys

LV_FONT_CONV = "lv_font_conv@1.5.3"


def gb2312_row(hi):
    out = []
    for lo in range(0xA1, 0xFF):
        try:
            out.append(bytes([hi, lo]).decode("gb2312"))
        except UnicodeDecodeError:
            pass
    return "".join(out)


def symbols():
    rows = [0xA1, 0xA3] + list(range(0xB0, 0xF8))   # punctuation, full width, hanzi
    return "".join(gb2312_row(r) for r in rows) + "—·"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cjk_16.bin")
    subprocess.run(["npx", "-y", LV_FONT_CONV, "--font", sys.argv[1], "--size", "16",
                    "--bpp", "4", "--no-compress", "--format", "bin",
                    "--symbols", symbols(), "-o", out], check=True)
    print(f"{out}: {os.path.getsize(out)} bytes, {len(set(symbols()))} characters")


if __name__ == "__main__":
    main()
