"""Lấy ảnh 96x96 mà model của bài #6 đang thấy, qua Serial, lưu thành PNG có ghi kết quả.

Gửi ký tự 'c' cho board (firmware bật PD_SNAPSHOT), board trả về:
    SNAP 96 96 person 95 detected 1
    <96 dòng hex, mỗi dòng 96 điểm ảnh>
    SNAP END

    python tools/person_snapshot.py COM5                    # 1 ảnh, mở luôn để xem
    python tools/person_snapshot.py COM5 --count 10 --interval 2 --no-show
    python tools/person_snapshot.py COM5 --scale 6 --out anh_chup
    python tools/person_snapshot.py COM5 --no-label         # chỉ ảnh, không ghi kết quả

Ảnh lưu có viền và dải chữ bên dưới: xanh "PERSON 95%" nếu firmware kết luận có người
(điểm person >= PD_THRESHOLD), xám "NO PERSON 88%" nếu không. Số trên nhãn là điểm của
chính kết luận đó; tên file luôn ghi điểm person.

Cần pyserial (có sẵn trong venv của ESP-IDF). Đóng idf.py monitor trước khi chạy.
"""

import argparse
import os
import struct
import sys
import time
import zlib

HEADER = "SNAP "


# Font 5x7 cho các ký tự cần dùng (không cần Pillow)
FONT = {
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "%": ["11001", "11010", "00010", "00100", "01000", "01011", "10011"],
    " ": ["00000"] * 7,
}
COLOR_PERSON = (0, 160, 60)
COLOR_NONE = (90, 90, 90)
WHITE = (255, 255, 255)
BORDER = 4  # px


def render(pixels, w, h, scale, label=None, color=None):
    """Trả về (W, H, ảnh RGB dạng list các dòng bytearray). Có label thì thêm viền + dải chữ."""
    img_w, img_h = w * scale, h * scale
    if label is None:
        rows = []
        for y in range(h):
            row = bytearray()
            for x in range(w):
                v = pixels[y * w + x]
                row += bytes((v, v, v)) * scale
            rows += [row] * scale
        return img_w, img_h, rows

    glyph = max(2, scale)  # mỗi điểm font = glyph x glyph px
    banner = 7 * glyph + 2 * glyph
    W, H = img_w + 2 * BORDER, img_h + 2 * BORDER + banner
    canvas = [bytearray(bytes(color) * W) for _ in range(H)]
    for y in range(img_h):
        row = canvas[BORDER + y]
        for x in range(w):
            v = pixels[(y // scale) * w + x]
            o = (BORDER + x * scale) * 3
            row[o:o + 3 * scale] = bytes((v, v, v)) * scale
    # chữ canh giữa trong dải dưới, thu nhỏ nếu quá rộng
    while glyph > 1 and len(label) * 6 * glyph > W - 2 * BORDER:
        glyph -= 1
    tx = (W - (len(label) * 6 - 1) * glyph) // 2
    ty = BORDER + img_h + (banner + BORDER - 7 * glyph) // 2
    for i, ch in enumerate(label):
        for gy, bits in enumerate(FONT.get(ch, FONT[" "])):
            for gx, b in enumerate(bits):
                if b == "1":
                    x0 = tx + (i * 6 + gx) * glyph
                    for yy in range(ty + gy * glyph, ty + (gy + 1) * glyph):
                        canvas[yy][x0 * 3:(x0 + glyph) * 3] = bytes(WHITE) * glyph
    return W, H, canvas


def write_png(path, width, height, rows):
    """Ghi PNG RGB 8 bit từ danh sách dòng (mỗi dòng width*3 byte)."""
    raw = b"".join(b"\x00" + bytes(r) for r in rows)  # filter 0 cho mỗi dòng

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def snapshot(ser, timeout):
    """Gửi lệnh, trả về (w, h, person %, detected, pixels) hoặc None nếu quá thời gian."""
    ser.reset_input_buffer()
    ser.write(b"c")
    end = time.time() + timeout
    header = None
    rows = []
    while time.time() < end:
        line = ser.readline().decode("ascii", "replace").strip()
        if header is None:
            if line.startswith(HEADER) and "END" not in line:
                parts = line.split()  # SNAP 96 96 person 95 detected 1
                person = int(parts[4])
                detected = parts[6] == "1" if len(parts) > 6 else person >= 60  # firmware cũ: ngưỡng mặc định
                header = (int(parts[1]), int(parts[2]), person, detected)
            continue
        if line == "SNAP END":
            w, h, person, detected = header
            pixels = b"".join(bytes.fromhex(r) for r in rows)
            if len(pixels) != w * h:
                print(f"  Ảnh thiếu dữ liệu ({len(pixels)}/{w * h} byte), bỏ qua")
                return None
            return w, h, person, detected, pixels
        if len(line) % 2 == 0 and all(c in "0123456789abcdef" for c in line):
            rows.append(line)
        # dòng khác (log PERSON/[stats]...) in xen vào thì bỏ qua
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("port", help="cổng serial, ví dụ COM5")
    p.add_argument("--count", type=int, default=1, help="số ảnh cần chụp (mặc định 1)")
    p.add_argument("--interval", type=float, default=0, help="nghỉ giữa các ảnh, giây")
    p.add_argument("--scale", type=int, default=4, help="phóng to khi lưu (mặc định 4 → 384x384)")
    p.add_argument("--out", default="snapshots", help="thư mục lưu (mặc định ./snapshots)")
    p.add_argument("--no-label", action="store_true", help="không ghi kết quả lên ảnh")
    p.add_argument("--no-show", action="store_true", help="không tự mở ảnh sau khi lưu")
    a = p.parse_args()

    import serial

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 1
    ser.dtr = ser.rts = False  # đế ESP32-CAM-MB: giữ DTR/RTS thả, không reset board
    ser.open()
    os.makedirs(a.out, exist_ok=True)
    saved = []
    for i in range(a.count):
        # Lệnh được xử lý ở khung kế tiếp (chờ tối đa 1 lần suy luận) rồi mất ~1.6 s để gửi
        result = snapshot(ser, timeout=10)
        if not result:
            print("Không nhận được ảnh: kiểm tra cổng, firmware có PD_SNAPSHOT 1 chưa")
            continue
        w, h, person, detected, pixels = result
        verdict = "PERSON" if detected else "NO PERSON"
        name = time.strftime("snap_%Y%m%d_%H%M%S") + f"_person{person:02d}.png"
        path = os.path.join(a.out, name)
        if a.no_label:
            write_png(path, *render(pixels, w, h, a.scale))
        else:
            # Số trên nhãn là điểm của chính kết luận: PERSON -> điểm person, NO PERSON -> điểm no person
            score = person if detected else 100 - person
            color = COLOR_PERSON if detected else COLOR_NONE
            write_png(path, *render(pixels, w, h, a.scale, f"{verdict} {score}%", color))
        saved.append(path)
        print(f"[{i + 1}/{a.count}] {verdict} (person {person}%) -> {path}")
        if i + 1 < a.count and a.interval:
            time.sleep(a.interval)
    ser.close()
    if saved and not a.no_show and hasattr(os, "startfile"):
        os.startfile(saved[-1])
    sys.exit(0 if saved else 1)


if __name__ == "__main__":
    main()
