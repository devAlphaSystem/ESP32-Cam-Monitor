# Forensic log reader

This firmware replaces the watchdog and preserves only the `logs` partition. It mounts LittleFS as read-only, never formats the partition, and serves the records on a local website.

## Installation

Run `install.bat`, enter the COM port, and confirm the operation. The installer:

1. builds the reader;
2. saves a RAW copy of 960 KiB in `backups/`;
3. checks the size and SHA-256 of the backup;
4. erases only `0x000000` through `0x30FFFF`;
5. verifies that `0x310000` through `0x3FFFFF` is still identical to the backup;
6. flashes the reader and repeats the verification.

The process stops before the erase if it cannot confirm the backup, the layout, the flash size, or the identity of the ESP32.

## Access

- Configured Wi-Fi network: `http://esp32-log-reader.local/`
- Fallback network: `ESP32-LogReader-XXXXXX`
- Fallback network password: `Logs-XXXXXX`, with the same six-character suffix
- Address on the fallback network: `http://192.168.4.1/`

## Reading and exporting

The website searches and filters by level, category, event, result, Boot ID, and Context ID. The filters apply to all valid records before pagination.

- CSV and JSONL export all valid records in chronological order, regardless of the current filters.
- RAW exports the exact 960 KiB of the partition, including LittleFS metadata, invalid records, and data the reader could not interpret.
- If LittleFS is corrupted, the website stays up and the RAW export remains available.

The backups can contain addresses, names, and operational details. Git ignores the folder, and you should treat it as sensitive material.
