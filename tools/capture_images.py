"""Thu ảnh train cho bài #7: tải JPEG từ ESP32-CAM (env 07_capture) và lưu theo nhãn.

Tên file theo dạng "<nhãn>.<thời gian>_<số>.jpg": Edge Impulse lấy nhãn từ phần trước
dấu chấm đầu tiên, nên upload cả thư mục là ảnh tự có nhãn.

    python tools/capture_images.py 192.168.1.50 --label coc --count 80 --interval 1
    python tools/capture_images.py 192.168.1.50 --label nen --count 80 --manual
    python tools/capture_images.py --summary                  # đếm ảnh đã có theo nhãn

Chế độ --interval: chụp liên tục, trong lúc đó xoay / dịch vật, đổi góc, khoảng cách.
Chế độ --manual: nhấn Enter mỗi lần chụp, gõ q rồi Enter để dừng.

Upload lên Edge Impulse (cần Node.js, `npm install -g edge-impulse-cli`):
    edge-impulse-uploader --category split dataset/*/*.jpg
hoặc trên web: Data acquisition → Upload data → chọn thư mục, "Infer from filename",
"Automatically split between training and testing".

Chỉ dùng thư viện chuẩn của Python.
"""

import argparse
import os
import re
import sys
import time
import urllib.request

LABEL_RE = re.compile(r"^[A-Za-z0-9_-]+$")


def fetch(host, timeout):
    url = f"http://{host}/capture.jpg?{int(time.time() * 1000)}"
    with urllib.request.urlopen(url, timeout=timeout) as r:
        data = r.read()
    if not data.startswith(b"\xff\xd8"):
        raise ValueError("không phải JPEG")
    return data


def summary(out):
    if not os.path.isdir(out):
        print(f"Chưa có thư mục {out}")
        return
    rows = []
    for label in sorted(os.listdir(out)):
        d = os.path.join(out, label)
        if os.path.isdir(d):
            rows.append((label, sum(f.lower().endswith(".jpg") for f in os.listdir(d))))
    if not rows:
        print("Chưa có ảnh")
        return
    for label, n in rows:
        print(f"  {label:<16} {n:4d} ảnh")
    counts = [n for _, n in rows]
    if max(counts) > 1.5 * min(counts):
        print("  Cảnh báo: số ảnh giữa các lớp lệch nhiều, model dễ nghiêng về lớp nhiều ảnh")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", nargs="?", help="IP của ESP32-CAM (in ra Serial lúc khởi động)")
    p.add_argument("--label", help="nhãn (chữ không dấu, số, _ hoặc -), ví dụ coc, dien_thoai, nen")
    p.add_argument("--count", type=int, default=50, help="số ảnh cần chụp (mặc định 50)")
    p.add_argument("--interval", type=float, default=1.0, help="giây giữa hai ảnh (mặc định 1)")
    p.add_argument("--manual", action="store_true", help="nhấn Enter mỗi lần chụp")
    p.add_argument("--out", default="dataset", help="thư mục gốc (mặc định ./dataset)")
    p.add_argument("--summary", action="store_true", help="chỉ đếm ảnh đã có theo nhãn")
    a = p.parse_args()

    if a.summary:
        summary(a.out)
        return
    if not a.host or not a.label:
        p.error("cần IP và --label")
    if not LABEL_RE.match(a.label):
        p.error("nhãn chỉ gồm chữ không dấu, số, _ hoặc - (không có dấu chấm, dấu cách)")

    folder = os.path.join(a.out, a.label)
    os.makedirs(folder, exist_ok=True)
    before = sum(f.lower().endswith(".jpg") for f in os.listdir(folder))
    stamp = time.strftime("%Y%m%d_%H%M%S")
    print(f"Nhãn '{a.label}': đã có {before} ảnh, chụp thêm {a.count} vào {folder}")
    if not a.manual:
        print(f"Bắt đầu sau 3 s, mỗi {a.interval:g} s một ảnh. Đổi góc / khoảng cách / nền giữa các ảnh.")
        time.sleep(3)

    saved = 0
    errors = 0
    while saved < a.count:
        if a.manual:
            if input(f"[{saved + 1}/{a.count}] Enter để chụp, q để dừng: ").strip().lower() == "q":
                break
        t0 = time.time()
        try:
            data = fetch(a.host, timeout=5)
        except Exception as e:  # mạng chập chờn: thử lại, dừng nếu lỗi liên tục
            errors += 1
            print(f"  Lỗi tải ảnh: {e}")
            if errors >= 5:
                sys.exit("Lỗi 5 lần liền: kiểm tra IP, board có chạy env 07_capture không")
            time.sleep(1)
            continue
        errors = 0
        saved += 1
        path = os.path.join(folder, f"{a.label}.{stamp}_{saved:03d}.jpg")
        with open(path, "wb") as f:
            f.write(data)
        if not a.manual:
            print(f"  [{saved}/{a.count}] {len(data) / 1024:.1f} KB -> {path}")
            time.sleep(max(0.0, a.interval - (time.time() - t0)))
    print(f"Xong: {saved} ảnh mới, '{a.label}' có {before + saved} ảnh\n")
    summary(a.out)


if __name__ == "__main__":
    main()
