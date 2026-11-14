import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

if len(sys.argv) != 4:
    sys.exit("Usage: make_update_manifest.py <firmware.bin> <version> <output-dir>")

firmware = Path(sys.argv[1])
version = sys.argv[2]
output = Path(sys.argv[3])

# Same format the dashboard accepts in parseFirmwareManifest (src/dashboard_page.cpp).
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
    sys.exit(f"Invalid version '{version}'; use digits separated by dots, such as 0.26.0")

firmware_bytes = firmware.read_bytes()
published_name = f"firmware-{version}.bin"

output.mkdir(parents=True, exist_ok=True)
shutil.copyfile(firmware, output / published_name)

manifest = {
    "version": version,
    "file": published_name,
    "sha256": hashlib.sha256(firmware_bytes).hexdigest(),
    "size": len(firmware_bytes),
}

(output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(json.dumps(manifest, indent=2))
