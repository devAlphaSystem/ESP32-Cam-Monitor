Import("env")

upload_flags = list(env.get("UPLOADERFLAGS", []))

for index in range(len(upload_flags) - 1):
    if str(upload_flags[index]).lower() != "0xe000":
        continue

    image = str(upload_flags[index + 1]).replace("\\", "/")
    if not image.endswith("/boot_app0.bin"):
        raise RuntimeError("Unexpected image at 0xE000; upload would overwrite NVS")

    del upload_flags[index:index + 2]
    env.Replace(UPLOADERFLAGS=upload_flags)
    break
