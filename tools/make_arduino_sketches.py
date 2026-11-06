"""Sinh sketch Arduino IDE trong arduino/ từ code PlatformIO (projects/ + common/).

Arduino IDE chỉ biên dịch file nằm trong thư mục sketch (và thư mục con src/), không có
"-I common" như platformio.ini. Script này chép vào mỗi sketch:
  - toàn bộ file của bài (main.cpp giữ nguyên tên, setup()/loop() nằm ở đó)
  - common/config.h, camera_pins.h, secrets.example.h
  - common/esp32cam/camera_init.*, wifi_init.* nếu bài có #include
  - file .ino trùng tên thư mục (chỉ có ghi chú: Arduino IDE cần file này để mở sketch)
Bài #5: chép quirc (và openmv mà quirc dùng) từ thư viện ESP32QRCodeReader vào src/quirc/,
vì thư viện này để header trong include/ nên Arduino IDE không build được nó như thư viện.

Nguồn chính vẫn là projects/ và common/: sửa ở đó rồi chạy lại script.
secrets.h người dùng tự tạo trong sketch được giữ lại khi sinh lại.

    python tools/make_arduino_sketches.py
    python tools/make_arduino_sketches.py --check   # chỉ báo sketch nào lệch với nguồn

Bài #5 cần đã build env 05_qr_reader một lần (pio run -e 05_qr_reader) để có thư viện
trong .pio/libdeps; nếu chưa có thì giữ nguyên src/quirc đang có trong sketch.
"""

import argparse
import filecmp
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "arduino"
COMMON = ROOT / "common"
QR_LIB = ROOT / ".pio" / "libdeps" / "05_qr_reader" / "ESP32QRCodeReader"

CORE_2 = "2.0.17"
CORE_1 = "1.0.6"

# tên sketch, thư mục nguồn, phiên bản core esp32, ghi chú thêm
SKETCHES = [
    ("01_face_detection", "projects/01_face_detection/src", CORE_2, []),
    ("02_face_recognition", "projects/02_face_recognition/src", CORE_1, [
        "Core 1.0.6 không có menu Partition Scheme cho board này: file partitions.csv",
        "trong sketch (có phân vùng \"fr\" lưu khuôn mặt) được dùng tự động.",
    ]),
    ("03_motion_detection", "projects/03_motion_detection/src", CORE_2, []),
    ("04_color_tracking", "projects/04_color_tracking/src", CORE_2, []),
    ("05_qr_reader", "projects/05_qr_reader/src", CORE_2, [
        "quirc (ISC) và openmv (MIT) nằm sẵn trong src/quirc/, không cần cài thêm thư viện.",
    ]),
    ("07_capture", "projects/07_image_classification/capture", CORE_2, [
        "Firmware thu ảnh train cho Edge Impulse; tải ảnh bằng tools/capture_images.py.",
    ]),
]

EXTRA_FILES = {
    "02_face_recognition": ["projects/02_face_recognition/partitions.csv"],
}

KEEP = {"secrets.h"}  # file của người dùng, không xóa khi sinh lại


def lesson_title(main_cpp: Path) -> str:
    first = main_cpp.read_text(encoding="utf-8").splitlines()[0]
    return first.lstrip("/ ").strip()


def ino_text(name: str, src: str, core: str, notes: list[str], title: str) -> str:
    lines = [
        f"// {title}",
        "//",
        f"// Sketch Arduino IDE sinh từ {src}/ bằng tools/make_arduino_sketches.py.",
        "// Code chính ở tab main.cpp; file .ino này để trống, chỉ để Arduino IDE mở được sketch.",
        "// Sửa code ở projects/ rồi chạy lại script (sinh lại sẽ ghi đè thay đổi trong thư mục này,",
        "// trừ secrets.h).",
        "//",
        "// Cài đặt:",
        f"//   Boards Manager: \"esp32\" by Espressif Systems, đúng phiên bản {core}",
        "//   Tools > Board: \"AI Thinker ESP32-CAM\"",
    ]
    if core == CORE_2:
        lines += [
            "//   Tools > Partition Scheme: \"Huge APP (3MB No OTA/1MB SPIFFS)\"",
            "//   Tools > Core Debug Level: \"Info\"",
        ]
    lines += [
        "//   Serial Monitor: 115200 baud",
        "//",
        "// WiFi: thêm tab secrets.h (chép nội dung secrets.example.h) rồi điền WIFI_SSID, WIFI_PASSWORD.",
        "// Chưa có secrets.h thì vẫn build được, board phát WiFi \"ESP32-CAM\" (mật khẩu 12345678),",
        "// vào http://192.168.4.1",
        "// Cấu hình chung: config.h · cấu hình riêng của bài: "
        + ("capture_config.h" if name == "07_capture" else "lesson_config.h"),
    ]
    if notes:
        lines.append("//")
        lines += [f"// {n}" for n in notes]
    return "\n".join(lines) + "\n"


def includes(directory: Path, header: str) -> bool:
    for f in directory.iterdir():
        if f.suffix in {".cpp", ".h", ".c"} and f'#include "{header}"' in f.read_text(encoding="utf-8"):
            return True
    return False


def build_sketch(dest: Path, name: str, src: str, core: str, notes: list[str]) -> None:
    src_dir = ROOT / src
    dest.mkdir(parents=True, exist_ok=True)

    for f in src_dir.iterdir():
        if f.is_dir():
            sys.exit(f"{src_dir}: không hỗ trợ thư mục con ({f.name})")
        shutil.copy2(f, dest / f.name)

    for f in ["config.h", "camera_pins.h", "secrets.example.h"]:
        shutil.copy2(COMMON / f, dest / f)
    for module in ["camera_init", "wifi_init"]:
        if includes(src_dir, f"{module}.h"):
            for ext in [".h", ".cpp"]:
                shutil.copy2(COMMON / "esp32cam" / (module + ext), dest / (module + ext))

    for extra in EXTRA_FILES.get(name, []):
        shutil.copy2(ROOT / extra, dest / Path(extra).name)

    if name == "05_qr_reader":
        scanner = dest / "scanner.cpp"
        text = scanner.read_text(encoding="utf-8")
        text = text.replace('#include "quirc/quirc.h"  // trong thư viện ESP32QRCodeReader',
                            '#include "src/quirc/quirc.h"  // chép từ thư viện ESP32QRCodeReader')
        if '"src/quirc/quirc.h"' not in text:
            sys.exit("scanner.cpp: không tìm thấy dòng #include quirc để sửa")
        scanner.write_text(text, encoding="utf-8", newline="")

    (dest / f"{name}.ino").write_text(ino_text(name, src, core, notes, lesson_title(src_dir / "main.cpp")),
                                      encoding="utf-8", newline="\n")


def vendor_quirc(dest: Path, previous: Path | None) -> None:
    target = dest / "src" / "quirc"
    if QR_LIB.exists():
        shutil.copytree(QR_LIB / "src" / "quirc", target)
        # identify.c có #include "openmv/fmath.h": đặt openmv cạnh quirc để đường dẫn tương đối vẫn đúng
        shutil.copytree(QR_LIB / "src" / "openmv", target / "openmv")
        shutil.copy2(QR_LIB / "LICENSE.md", target / "LICENSE.ESP32QRCodeReader.md")
    elif previous is not None and (previous / "src" / "quirc").exists():
        shutil.copytree(previous / "src" / "quirc", target)
    else:
        sys.exit(f"Không thấy {QR_LIB}: chạy 'pio run -e 05_qr_reader' một lần rồi chạy lại")


def generate(out: Path, previous: Path | None) -> None:
    for name, src, core, notes in SKETCHES:
        dest = out / name
        build_sketch(dest, name, src, core, notes)
        if name == "05_qr_reader":
            vendor_quirc(dest, previous / name if previous else None)


def diff_dirs(a: Path, b: Path, rel: str = "") -> list[str]:
    cmp = filecmp.dircmp(a, b)
    out = [f"{rel}{n}" for n in cmp.left_only + cmp.right_only + cmp.diff_files if n not in KEEP]
    for sub in cmp.common_dirs:
        out += diff_dirs(a / sub, b / sub, f"{rel}{sub}/")
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="không ghi, chỉ báo khác biệt")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        fresh = Path(tmp) / "arduino"
        generate(fresh, OUT if OUT.exists() else None)

        if args.check:
            changed = []
            for name, *_ in SKETCHES:
                if (OUT / name).exists():
                    changed += diff_dirs(fresh / name, OUT / name, f"{name}/")
                else:
                    changed.append(f"{name}/ chưa có")
            for c in changed:
                print("khác:", c)
            sys.exit(1 if changed else 0)

        # Thay từng thư mục sketch (arduino/README.md giữ nguyên), giữ secrets.h của người dùng
        for name, *_ in SKETCHES:
            for keep in KEEP:
                old = OUT / name / keep
                if old.exists():
                    shutil.copy2(old, fresh / name / keep)
            if (OUT / name).exists():
                shutil.rmtree(OUT / name)
            shutil.copytree(fresh / name, OUT / name)

    for name, src, core, _ in SKETCHES:
        print(f"arduino/{name}/  <- {src}  (esp32 core {core})")


if __name__ == "__main__":
    main()
