"""Runs a PlatformIO test build inside Espressif's QEMU (emulated ESP32-C3).

Called by `pio test -e test_c3 --without-uploading` through
test_testing_command. Merges the build into a 4 MB flash image, boots it in
QEMU and echoes the serial output for PlatformIO's Unity parser. QEMU is
killed once Unity prints OK or FAIL, or after TIMEOUT_S if it never does.

Usage: qemu_test.py <build_dir> <packages_dir>
Needs ESP_QEMU set to qemu-system-riscv32, see tools/setup_qemu.sh.
"""

import os
import subprocess
import sys
import threading

TIMEOUT_S = 60


def merge_image(build_dir, packages_dir):
    """Merges bootloader, partitions, boot_app0 and firmware into one image.

    Args:
        build_dir: The env's build directory.
        packages_dir: PlatformIO's packages directory.

    Returns:
        Path of the merged flash image.
    """
    fw = os.path.join(packages_dir, "framework-arduinoespressif32")
    image = os.path.join(build_dir, "flash_qemu.bin")
    subprocess.run(
        ["pio", "pkg", "exec", "-p", "tool-esptoolpy", "--",
         "esptool.py", "--chip", "esp32c3", "merge_bin", "--fill-flash-size", "4MB",
         "-o", image, "--flash_mode", "dio", "--flash_size", "4MB",
         "0x0", os.path.join(build_dir, "bootloader.bin"),
         "0x8000", os.path.join(build_dir, "partitions.bin"),
         "0xe000", os.path.join(fw, "tools/partitions/boot_app0.bin"),
         "0x10000", os.path.join(build_dir, "firmware.bin")],
        check=True, stdout=subprocess.DEVNULL)
    return image


def main():
    """Boots the test image and streams its output until Unity finishes.

    Returns:
        0 once Unity's summary line is seen, 1 on a timeout or early exit.
    """
    if len(sys.argv) != 3:
        print("usage: qemu_test.py <build_dir> <packages_dir>", file=sys.stderr)
        return 1
    qemu = os.environ.get("ESP_QEMU")
    if not qemu:
        print("qemu_test.py: ESP_QEMU is not set. Run tools/setup_qemu.sh "
              "and export the line it prints.", file=sys.stderr)
        return 1

    image = merge_image(sys.argv[1], sys.argv[2])
    proc = subprocess.Popen(
        [qemu, "-nographic", "-machine", "esp32c3",
         "-drive", f"file={image},if=mtd,format=raw", "-serial", "mon:stdio"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, text=True, errors="replace")

    # Reading stdout blocks, so a timer kills QEMU if the firmware hangs
    timed_out = threading.Event()

    def on_timeout():
        timed_out.set()
        proc.kill()

    timer = threading.Timer(TIMEOUT_S, on_timeout)
    timer.start()
    finished = False
    try:
        for line in proc.stdout:
            print(line, end="", flush=True)
            if line.strip() in ("OK", "FAIL"):
                finished = True
                break
    finally:
        timer.cancel()
        proc.kill()
        proc.wait()

    if finished:
        return 0
    if timed_out.is_set():
        print(f"\nqemu_test.py: no Unity result after {TIMEOUT_S} s, "
              "the firmware crashed or hung. QEMU killed.", flush=True)
    else:
        print("\nqemu_test.py: QEMU exited before Unity printed a result.", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())