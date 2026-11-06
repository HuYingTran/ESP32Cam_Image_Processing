"""Tạo trang QR thử cho bài #5: nội dung ngắn/dài, mức sửa lỗi L/H, khổ 2/4/6 cm.

Kết quả là một file HTML, mở bằng trình duyệt rồi in ở tỉ lệ 100% (không "Fit to page")
để QR đúng kích thước. Kiểm tra lại khổ in bằng thước.

    pip install segno
    python tools/qr_test_sheet.py                   # ghi ra qr_test_sheet.html
    python tools/qr_test_sheet.py -o sheet.html --open-text "ESP32-CAM-OPEN"
"""

import argparse
import html
import sys

try:
    import segno
except ImportError:
    sys.exit("Thiếu thư viện segno: pip install segno")

SHORT = "HELLO-ESP32"  # 11 ký tự
LONG = "https://github.com/espressif/esp32-camera ESP32-CAM QR test, 100 characters long: "
LONG = (LONG + "abcdefghijklmnopqrstuvwxyz0123456789")[:100]
SIZES_CM = [2, 4, 6]


def qr_svg(text, error):
    # boost_error=False: giữ đúng mức sửa lỗi đã chọn để so sánh công bằng
    qr = segno.make_qr(text, error=error, boost_error=False)
    # border=4: vùng trắng quanh mã theo chuẩn, thiếu thì dễ không tìm thấy mã
    svg = qr.svg_inline(scale=1, border=4)
    return qr, svg


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--output", default="qr_test_sheet.html")
    parser.add_argument("--open-text", default="ESP32-CAM-OPEN",
                        help="Nội dung QR 'mở cửa', trùng QR_EXPECTED_TEXT trong lesson_config.h")
    args = parser.parse_args()

    cases = [("Ngắn", SHORT, "L"), ("Ngắn", SHORT, "H"), ("Dài", LONG, "L"), ("Dài", LONG, "H")]
    rows = []
    for name, text, error in cases:
        cells = []
        for cm in SIZES_CM:
            qr, svg = qr_svg(text, error)
            modules = qr.symbol_size(border=0)[0]
            cells.append(
                f'<td><div class="qr" style="width:{cm}cm;height:{cm}cm">{svg}</div>'
                f'<div class="cap">{cm} cm · {modules}×{modules} module · {cm * 10 / (modules + 8):.2f} mm/module</div></td>')
        rows.append(f'<tr><th>{name} ({len(text)} ký tự)<br>ECC {error} · version {qr.version}</th>{"".join(cells)}</tr>')

    open_qr, open_svg = qr_svg(args.open_text, "M")
    page = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>QR test sheet</title>
<style>
body{{font-family:system-ui,sans-serif;margin:1cm}}
table{{border-collapse:collapse}}td,th{{border:1px solid #ccc;padding:4mm;vertical-align:top;text-align:left}}
.qr svg{{width:100%;height:100%;display:block}}
.cap{{font-size:9pt;color:#555;margin-top:2mm}}
@media print{{.noprint{{display:none}}}}
</style></head><body>
<h2>Bộ QR thử - bài #5</h2>
<p class="noprint">In ở tỉ lệ <b>100%</b>, kiểm tra lại khổ bằng thước. Khổ đã gồm viền trắng 4 module.</p>
<table><tr><th></th>{"".join(f"<th>{cm} cm</th>" for cm in SIZES_CM)}</tr>
{"".join(rows)}</table>
<p>Ngắn: <code>{html.escape(SHORT)}</code><br>Dài: <code>{html.escape(LONG)}</code></p>
<h3>QR mở cửa: <code>{html.escape(args.open_text)}</code></h3>
<div class="qr" style="width:4cm;height:4cm">{open_svg}</div>
</body></html>"""
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"Đã ghi {args.output}")


if __name__ == "__main__":
    main()
