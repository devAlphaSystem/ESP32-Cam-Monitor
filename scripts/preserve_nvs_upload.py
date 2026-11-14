from os.path import join

Import("env")


def otadata_offset():
    partitions_file = join(env.subst("$PROJECT_DIR"), env.BoardConfig().get("build.partitions"))

    with open(partitions_file) as partitions:
        for line in partitions:
            fields = [field.strip() for field in line.split(",")]

            if len(fields) > 3 and fields[1] == "data" and fields[2] == "ota":
                return fields[3]

    raise RuntimeError("The partition table has no otadata partition")


upload_flags = list(env.get("UPLOADERFLAGS", []))

for index in range(len(upload_flags) - 1):
    if str(upload_flags[index]).lower() != "0xe000":
        continue

    image = str(upload_flags[index + 1]).replace("\\", "/")
    if not image.endswith("/boot_app0.bin"):
        raise RuntimeError("Unexpected image at 0xE000; upload would overwrite NVS")

    upload_flags[index] = otadata_offset()
    env.Replace(UPLOADERFLAGS=upload_flags)
    break
