#!/usr/bin/env python3
"""Merge the three per-firmware compile_commands.json into one at the repo root.

clangd only looks for a single compilation database, but this repo builds three
separate ESP-IDF projects (two ESP32, one ESP32-C3). Merging them gives the
editor coverage of every source file. Where a shared component is compiled by
more than one firmware, the first entry wins, which is enough for navigation.

Reads the databases PlatformIO writes to firmware/<name>/.pio/build/<name>/.
Run `pio run -d firmware/<name>` for each firmware first, then run this.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIRMWARES = ["meter_node", "substation", "gateway"]

entries = []
seen = set()
missing = []

for name in FIRMWARES:
    # The PlatformIO environment in each platformio.ini shares the firmware name.
    db = ROOT / "firmware" / name / ".pio" / "build" / name / "compile_commands.json"
    if not db.exists():
        missing.append(name)
        continue
    for entry in json.loads(db.read_text()):
        path = pathlib.Path(entry["file"]).resolve()
        # Only index this project's own sources, not the whole of ESP-IDF.
        if ROOT not in path.parents:
            continue
        if path in seen:
            continue
        seen.add(path)
        entries.append(entry)

out = ROOT / "compile_commands.json"
out.write_text(json.dumps(entries, indent=2) + "\n")

print(f"wrote {out} with {len(entries)} entries")
if missing:
    print(f"note: no build directory for {', '.join(missing)}; build it and re-run",
          file=sys.stderr)
