"""Đo độ chính xác của bài #6 trên các cảnh thật, đọc trực tiếp từ cổng serial.

Firmware in mỗi khung một dòng:
    PERSON    person  95%  no person   5%

Với mỗi cảnh: đặt camera, nhấn Enter, script lấy điểm person của vài khung
tiếp theo rồi lấy trung vị làm điểm của cảnh. Cuối cùng tính độ chính xác,
số báo nhầm / bỏ sót ở nhiều ngưỡng (không cần nạp lại firmware).

    python tools/person_eval.py COM5
    python tools/person_eval.py COM5 --scenes 10 --frames 3 --csv ket_qua.csv
    python tools/person_eval.py --from-csv ket_qua.csv      # chỉ tính lại

Cần pyserial (có sẵn trong venv của ESP-IDF). Đóng idf.py monitor trước khi chạy.
"""

import argparse
import csv
import re
import statistics
import sys
import time

LINE_RE = re.compile(r"(PERSON|no person)\s+person\s+(\d+)%")
THRESHOLDS = (0.5, 0.6, 0.7, 0.8, 0.9)


def read_scores(ser, frames, timeout):
    """Bỏ các dòng cũ trong bộ đệm, trả về điểm person (0..1) của `frames` khung mới."""
    ser.reset_input_buffer()
    ser.readline()  # dòng đầu có thể bị cắt giữa chừng
    scores, end = [], time.time() + timeout
    while len(scores) < frames and time.time() < end:
        m = LINE_RE.search(ser.readline().decode("utf-8", "replace"))
        if m:
            scores.append(int(m.group(2)) / 100)
    return scores


def collect(port, scenes, frames):
    import serial

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 1
    ser.dtr = ser.rts = False  # đế ESP32-CAM-MB: giữ DTR/RTS thả, không reset board
    ser.open()
    rows = []
    plan = [True] * scenes + [False] * scenes
    for i, expect in enumerate(plan, 1):
        label = "CÓ người" if expect else "KHÔNG có người"
        input(f"[{i}/{len(plan)}] Cảnh {label}: đặt camera rồi nhấn Enter... ")
        scores = read_scores(ser, frames, timeout=frames * 2 + 5)
        if not scores:
            print("  Không đọc được dòng nào, kiểm tra cổng/firmware. Bỏ qua cảnh này.")
            continue
        score = statistics.median(scores)
        print(f"  person {score:.0%}  ({', '.join(f'{s:.0%}' for s in scores)})")
        rows.append({"scene": i, "person": int(expect), "score": score})
    ser.close()
    return rows


def report(rows):
    pos = [r["score"] for r in rows if r["person"]]
    neg = [r["score"] for r in rows if not r["person"]]
    print(f"\n{len(pos)} cảnh có người, {len(neg)} cảnh không có người")
    print("Ngưỡng | Đúng      | Độ chính xác | Bỏ sót (FN) | Báo nhầm (FP)")
    for t in THRESHOLDS:
        fn = sum(s < t for s in pos)
        fp = sum(s >= t for s in neg)
        correct = len(rows) - fn - fp
        print(f" {t:.2f}  | {correct:2d}/{len(rows):<6d} | {correct / len(rows):11.0%} | {fn:11d} | {fp:13d}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("port", nargs="?", help="cổng serial, ví dụ COM5")
    p.add_argument("--scenes", type=int, default=20, help="số cảnh mỗi loại (mặc định 20)")
    p.add_argument("--frames", type=int, default=3, help="số khung lấy cho mỗi cảnh (mặc định 3)")
    p.add_argument("--csv", help="lưu kết quả từng cảnh ra file CSV")
    p.add_argument("--from-csv", help="đọc kết quả đã lưu thay vì đo lại")
    a = p.parse_args()

    if a.from_csv:
        with open(a.from_csv, encoding="utf-8") as f:
            rows = [{"scene": int(r["scene"]), "person": int(r["person"]), "score": float(r["score"])}
                    for r in csv.DictReader(f)]
    elif a.port:
        rows = collect(a.port, a.scenes, a.frames)
        if a.csv:
            with open(a.csv, "w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(f, fieldnames=["scene", "person", "score"])
                w.writeheader()
                w.writerows(rows)
    else:
        p.error("cần cổng serial hoặc --from-csv")
    if not rows:
        sys.exit("Không có dữ liệu")
    report(rows)


if __name__ == "__main__":
    main()
