"""Thống kê log nhận dạng khuôn mặt của bài #2.

Đọc các dòng do firmware in ra:
    Match Face ID: 0, similarity 0.78
    No Match Found, best similarity 0.31 < 0.55

Mỗi dòng ứng với một khung hình đã nhận dạng. Dùng một file log cho mỗi người
được thử, rồi chỉ định người đó là ai:

    python tools/face_log_stats.py log_A.log --expect 0
    python tools/face_log_stats.py log_nguoi_la.log --expect none
    python tools/face_log_stats.py log_A.log --expect 0 --threshold 0.6
"""

import argparse
import re
import statistics
import sys

MATCH_RE = re.compile(r"Match Face ID: (\d+), similarity (-?[\d.]+)")
NO_MATCH_RE = re.compile(r"No Match Found, best similarity (-?[\d.]+)")


def parse(path):
    """Trả về danh sách (id hoặc None, similarity) theo thứ tự trong log."""
    results = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = MATCH_RE.search(line)
            if m:
                results.append((int(m.group(1)), float(m.group(2))))
                continue
            m = NO_MATCH_RE.search(line)
            if m:
                results.append((None, float(m.group(1))))
    return results


def main():
    # Console Windows mặc định cp1252, không in được tiếng Việt
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", help="File log Serial")
    parser.add_argument("--expect", required=True,
                        help="ID đúng của người trong log, hoặc 'none' nếu là người lạ")
    parser.add_argument("--threshold", type=float,
                        help="Tính lại kết quả với ngưỡng khác (chỉ chính xác khi log có đúng 1 ID đã enroll "
                             "hoặc với người lạ)")
    args = parser.parse_args()

    expect = None if args.expect.lower() == "none" else int(args.expect)
    results = parse(args.log)
    if not results:
        sys.exit("Không thấy dòng 'Match Face ID' / 'No Match Found' nào trong log")

    if args.threshold is not None:
        # Log chỉ có similarity của ID tốt nhất, nên chỉ tính lại được đúng/sai theo ngưỡng mới
        results = [(i if (i is not None and s >= args.threshold) else None, s) for i, s in results]

    sims = [s for _, s in results]
    correct = sum(1 for i, _ in results if i == expect)
    wrong_id = sum(1 for i, _ in results if i is not None and i != expect)
    rejected = sum(1 for i, _ in results if i is None)
    n = len(results)

    who = "người lạ" if expect is None else f"ID {expect}"
    print(f"File: {args.log}  |  Người thử: {who}  |  Số khung: {n}")
    print(f"  Đúng            : {correct}/{n} ({100 * correct / n:.0f}%)")
    if expect is None:
        print(f"  Nhận nhầm (FAR) : {n - correct}/{n} ({100 * (n - correct) / n:.0f}%)")
    else:
        print(f"  Từ chối nhầm    : {rejected}/{n} ({100 * rejected / n:.0f}%)")
        print(f"  Nhầm sang ID khác: {wrong_id}/{n}")
    print(f"  Similarity      : min {min(sims):.2f}  trung bình {statistics.mean(sims):.2f}  max {max(sims):.2f}")


if __name__ == "__main__":
    main()
