#!/usr/bin/env bash
# Downloads Espressif's QEMU fork, which has the esp32c3 machine that upstream
# QEMU lacks, into ~/.kaizen/qemu/<tag>. Does nothing if it is already there.
#
# Prints the export line for ESP_QEMU, which tools/qemu_test.py reads. In
# GitHub Actions it also appends ESP_QEMU to $GITHUB_ENV for later steps.

set -euo pipefail

QEMU_TAG=esp-develop-9.2.2-20260417
QEMU_VERSION=esp_develop_9.2.2_20260417

case "$(uname -s)/$(uname -m)" in
  Darwin/arm64) triple=aarch64-apple-darwin ;;
  Linux/x86_64) triple=x86_64-linux-gnu ;;
  *)
    echo "setup_qemu.sh: unsupported platform $(uname -s)/$(uname -m)," \
         "only Darwin/arm64 and Linux/x86_64 are supported" >&2
    exit 1
    ;;
esac

asset="qemu-riscv32-softmmu-${QEMU_VERSION}-${triple}.tar.xz"
url="https://github.com/espressif/qemu/releases/download/${QEMU_TAG}/${asset}"
dest="${HOME}/.kaizen/qemu/${QEMU_TAG}"
binary="${dest}/qemu/bin/qemu-system-riscv32"

if [ ! -x "$binary" ]; then
  echo "Downloading ${asset}" >&2
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  curl -fsSL "$url" -o "${tmp}/${asset}"
  mkdir -p "$dest"
  tar -xJf "${tmp}/${asset}" -C "$dest"
fi

echo "export ESP_QEMU=${binary}"

if [ -n "${GITHUB_ENV:-}" ]; then
  echo "ESP_QEMU=${binary}" >> "$GITHUB_ENV"
fi