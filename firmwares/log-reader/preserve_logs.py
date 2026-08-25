Import("env")

import csv
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path


LOG_OFFSET = 0x310000
LOG_SIZE = 0x0F0000
ERASE_OFFSET = 0x000000
ERASE_SIZE = LOG_OFFSET
EXPECTED_FLASH_SIZE = "4MB"
STATE_VERSION = 1


class PreservationError(RuntimeError):
    pass


def project_dir():
    return Path(env.subst("$PROJECT_DIR")).resolve()


def state_path():
    return project_dir() / ".pio" / "log-reader-preservation.json"


def validate_partition_layout():
    partition_path = project_dir() / "partitions.csv"
    if not partition_path.is_file():
        raise PreservationError("partitions.csv nao foi encontrado")

    log_rows = []
    parsed_partitions = []
    try:
        with partition_path.open("r", encoding="utf-8-sig", newline="") as source:
            for row in csv.reader(source):
                if not row or not row[0].strip() or row[0].lstrip().startswith("#"):
                    continue
                fields = [field.strip() for field in row]
                if len(fields) < 5:
                    raise PreservationError("linha incompleta em partitions.csv")

                try:
                    offset = int(fields[3], 0)
                    size = int(fields[4], 0)
                except ValueError:
                    raise PreservationError(
                        "offset ou tamanho invalido em partitions.csv"
                    )

                partition = {
                    "name": fields[0],
                    "type": fields[1].lower(),
                    "subtype": fields[2].lower(),
                    "offset": offset,
                    "size": size,
                }
                parsed_partitions.append(partition)
                if partition["name"] == "logs":
                    log_rows.append(partition)
    except (OSError, UnicodeError, csv.Error) as error:
        raise PreservationError(
            "nao foi possivel ler partitions.csv: {0}".format(error)
        )

    if len(log_rows) != 1:
        raise PreservationError(
            "partitions.csv deve conter exatamente uma particao chamada logs"
        )

    log_partition = log_rows[0]
    if (
        log_partition["type"] != "data"
        or log_partition["subtype"] != "spiffs"
        or log_partition["offset"] != LOG_OFFSET
        or log_partition["size"] != LOG_SIZE
        or log_partition["offset"] + log_partition["size"] != 0x400000
    ):
        raise PreservationError(
            "particao logs deve ser data/spiffs em 0x310000, tamanho 0xF0000, terminando em 0x400000"
        )

    for partition in parsed_partitions:
        if partition is log_partition:
            continue
        partition_end = partition["offset"] + partition["size"]
        if partition["offset"] < 0x400000 and partition_end > LOG_OFFSET:
            raise PreservationError(
                "a particao {0} invade a faixa reservada aos logs".format(
                    partition["name"]
                )
            )

    layout_sha256 = sha256_file(partition_path)
    print("LOG_LAYOUT_OK=0x310000:0x0F0000:0x400000")
    return layout_sha256


def selected_port():
    port = str(env.subst("$UPLOAD_PORT")).strip().upper()
    if not re.fullmatch(r"COM[1-9][0-9]*", port):
        raise PreservationError(
            "porta serial ausente ou invalida; informe a porta com --upload-port"
        )
    return port


def upload_speed():
    speed = str(env.subst("$UPLOAD_SPEED")).strip()
    return speed if speed.isdigit() else "230400"


def esptool_path():
    package_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    if not package_dir:
        raise PreservationError("pacote tool-esptoolpy nao encontrado")

    path = Path(package_dir) / "esptool.py"
    if not path.is_file():
        raise PreservationError("esptool.py nao encontrado no PlatformIO")
    return path


def run_esptool(arguments, after="no_reset"):
    command = [
        sys.executable,
        str(esptool_path()),
        "--chip",
        "esp32",
        "--port",
        selected_port(),
        "--baud",
        upload_speed(),
        "--before",
        "default_reset",
        "--after",
        after,
    ] + [str(argument) for argument in arguments]

    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        check=False,
    )

    if result.returncode != 0:
        output = result.stdout.strip()
        if output:
            print(output)
        raise PreservationError(
            "esptool terminou com o codigo {0}".format(result.returncode)
        )

    return result.stdout


def probe_device(expected_mac=None):
    output = run_esptool(["flash_id"], after="no_reset")
    size_match = re.search(
        r"Detected flash size:\s*([^\s]+)", output, flags=re.IGNORECASE
    )
    mac_match = re.search(
        r"MAC:\s*([0-9a-f]{2}(?::[0-9a-f]{2}){5})",
        output,
        flags=re.IGNORECASE,
    )

    if not size_match:
        raise PreservationError("nao foi possivel confirmar o tamanho da flash")

    flash_size = size_match.group(1).upper()
    if flash_size != EXPECTED_FLASH_SIZE.upper():
        raise PreservationError(
            "flash incompativel: esperado {0}, detectado {1}".format(
                EXPECTED_FLASH_SIZE, flash_size
            )
        )

    if not mac_match:
        raise PreservationError("nao foi possivel identificar o MAC do ESP32")

    mac = mac_match.group(1).upper()
    if expected_mac and mac != expected_mac.upper():
        raise PreservationError(
            "o ESP32 conectado mudou durante a instalacao: esperado {0}, detectado {1}".format(
                expected_mac.upper(), mac
            )
        )

    print("LOG_DEVICE_OK={0}".format(mac))
    print("LOG_FLASH_SIZE_OK={0}".format(EXPECTED_FLASH_SIZE))
    return mac


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(65536)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def files_are_identical(first_path, second_path):
    if first_path.stat().st_size != second_path.stat().st_size:
        return False

    with first_path.open("rb") as first, second_path.open("rb") as second:
        while True:
            first_block = first.read(65536)
            second_block = second.read(65536)
            if first_block != second_block:
                return False
            if not first_block:
                return True


def unique_backup_path(port):
    backup_dir = project_dir() / "backups"
    backup_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    for sequence in range(1000):
        suffix = timestamp if sequence == 0 else "{0}-{1}".format(timestamp, sequence)
        candidate = backup_dir / "watchdog-logs-{0}-{1}.bin".format(port, suffix)
        if not candidate.exists() and not candidate.with_suffix(".partial").exists():
            return candidate
    raise PreservationError("nao foi possivel criar um nome exclusivo para o backup")


def write_state(state):
    path = state_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = path.with_suffix(".tmp")
    with temporary_path.open("w", encoding="utf-8", newline="\n") as destination:
        json.dump(state, destination, indent=2, sort_keys=True)
        destination.write("\n")
    os.replace(str(temporary_path), str(path))


def load_state(expected_stage, layout_sha256):
    path = state_path()
    if not path.is_file():
        raise PreservationError(
            "estado do backup nao encontrado; reinicie o instalador desde o inicio"
        )

    try:
        with path.open("r", encoding="utf-8") as source:
            state = json.load(source)
    except (OSError, ValueError) as error:
        raise PreservationError("estado do backup invalido: {0}".format(error))

    if not isinstance(state, dict):
        raise PreservationError("estado do backup deve ser um objeto JSON")

    required_values = {
        "version": STATE_VERSION,
        "port": selected_port(),
        "offset": LOG_OFFSET,
        "size": LOG_SIZE,
        "stage": expected_stage,
        "partition_table_sha256": layout_sha256,
    }
    for key, expected_value in required_values.items():
        if state.get(key) != expected_value:
            raise PreservationError(
                "estado do backup incompativel em {0}; reinicie o instalador".format(
                    key
                )
            )

    mac = str(state.get("mac", "")).upper()
    if not re.fullmatch(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}", mac):
        raise PreservationError("MAC salvo no estado do backup e invalido")

    saved_sha256 = str(state.get("sha256", "")).lower()
    if not re.fullmatch(r"[0-9a-f]{64}", saved_sha256):
        raise PreservationError("SHA256 salvo no estado do backup e invalido")

    backup_path = Path(str(state.get("backup", "")))
    if not backup_path.is_absolute():
        raise PreservationError("caminho do backup invalido")
    if backup_path.parent.resolve() != (project_dir() / "backups").resolve():
        raise PreservationError("o backup informado esta fora da pasta protegida")
    if not backup_path.is_file():
        raise PreservationError("arquivo de backup nao encontrado: {0}".format(backup_path))
    if backup_path.stat().st_size != LOG_SIZE:
        raise PreservationError("o backup nao possui o tamanho esperado")

    backup_sha256 = sha256_file(backup_path)
    if backup_sha256.lower() != saved_sha256:
        raise PreservationError("o arquivo de backup foi alterado ou corrompido")

    return state, backup_path


def read_log_partition(destination, after):
    run_esptool(
        [
            "read_flash",
            "--no-progress",
            hex(LOG_OFFSET),
            hex(LOG_SIZE),
            str(destination),
        ],
        after=after,
    )

    if not destination.is_file() or destination.stat().st_size != LOG_SIZE:
        raise PreservationError("a leitura da particao de logs ficou incompleta")


def backup_logs(source, target, env):
    partial_path = None
    try:
        layout_sha256 = validate_partition_layout()
        port = selected_port()
        write_state(
            {
                "version": STATE_VERSION,
                "port": port,
                "offset": LOG_OFFSET,
                "size": LOG_SIZE,
                "stage": "backup_in_progress",
                "partition_table_sha256": layout_sha256,
                "created_at": datetime.now().isoformat(timespec="seconds"),
            }
        )
        mac = probe_device()
        backup_path = unique_backup_path(port)
        partial_path = backup_path.with_suffix(".partial")

        print("LOG_BACKUP_STARTED")
        read_log_partition(partial_path, after="no_reset")
        if backup_path.exists():
            raise PreservationError("o destino exclusivo do backup ja existe")
        partial_path.rename(backup_path)
        partial_path = None

        backup_sha256 = sha256_file(backup_path)
        state = {
            "version": STATE_VERSION,
            "port": port,
            "mac": mac,
            "offset": LOG_OFFSET,
            "size": LOG_SIZE,
            "backup": str(backup_path),
            "sha256": backup_sha256,
            "stage": "backed_up",
            "partition_table_sha256": layout_sha256,
            "created_at": datetime.now().isoformat(timespec="seconds"),
        }
        write_state(state)

        print("LOG_BACKUP_SAVED={0}".format(backup_path))
        print("LOG_BACKUP_SHA256={0}".format(backup_sha256))
    except (OSError, PreservationError) as error:
        print("LOG_ERROR={0}".format(error))
        env.Exit(1)
    finally:
        if partial_path and partial_path.exists():
            try:
                partial_path.unlink()
            except OSError:
                pass


def erase_except_logs(source, target, env):
    try:
        layout_sha256 = validate_partition_layout()
        state, unused_backup_path = load_state("backed_up", layout_sha256)
        probe_device(expected_mac=state["mac"])

        print("LOG_PRESERVE_ERASE_START")
        run_esptool(
            ["erase_region", hex(ERASE_OFFSET), hex(ERASE_SIZE)],
            after="no_reset",
        )

        state["stage"] = "erased"
        state["updated_at"] = datetime.now().isoformat(timespec="seconds")
        write_state(state)
        print("LOG_PRESERVE_ERASE_DONE")
    except (OSError, PreservationError) as error:
        print("LOG_ERROR={0}".format(error))
        env.Exit(1)


def verify_logs(expected_stage, next_stage, marker_stage, final_reset):
    layout_sha256 = validate_partition_layout()
    state, backup_path = load_state(expected_stage, layout_sha256)
    probe_device(expected_mac=state["mac"])
    verification_path = backup_path.with_name(
        ".{0}.{1}.verification.tmp".format(backup_path.name, marker_stage)
    )

    try:
        print("LOG_VERIFY_STARTED={0}".format(marker_stage))
        read_log_partition(
            verification_path,
            after="hard_reset" if final_reset else "no_reset",
        )
        verification_sha256 = sha256_file(verification_path)
        if verification_sha256.lower() != state["sha256"].lower():
            raise PreservationError(
                "o SHA256 da particao difere do backup preservado"
            )
        if not files_are_identical(backup_path, verification_path):
            raise PreservationError(
                "a comparacao binaria da particao difere do backup preservado"
            )

        state["stage"] = next_stage
        state["updated_at"] = datetime.now().isoformat(timespec="seconds")
        write_state(state)
        print("LOG_VERIFY_OK={0}".format(marker_stage))
    finally:
        if verification_path.exists():
            try:
                verification_path.unlink()
            except OSError:
                pass


def verify_logs_after_erase(source, target, env):
    try:
        verify_logs(
            expected_stage="erased",
            next_stage="verified_after_erase",
            marker_stage="after-erase",
            final_reset=False,
        )
    except (OSError, PreservationError) as error:
        print("LOG_ERROR={0}".format(error))
        env.Exit(1)


def verify_logs_after_upload(source, target, env):
    try:
        verify_logs(
            expected_stage="verified_after_erase",
            next_stage="verified_after_upload",
            marker_stage="after-upload",
            final_reset=True,
        )
    except (OSError, PreservationError) as error:
        print("LOG_ERROR={0}".format(error))
        env.Exit(1)


env.AddCustomTarget(
    name="backup_logs",
    dependencies=None,
    actions=[backup_logs],
    title="Backup forense dos logs",
    description="Le a particao de logs sem altera-la e salva uma copia RAW",
    always_build=True,
)

env.AddCustomTarget(
    name="erase_except_logs",
    dependencies=None,
    actions=[erase_except_logs],
    title="Apagar exceto logs",
    description="Apaga somente a faixa anterior a particao de logs",
    always_build=True,
)

env.AddCustomTarget(
    name="verify_logs_after_erase",
    dependencies=None,
    actions=[verify_logs_after_erase],
    title="Verificar logs depois do apagamento",
    description="Compara a particao preservada com o backup RAW",
    always_build=True,
)

env.AddCustomTarget(
    name="verify_logs_after_upload",
    dependencies=None,
    actions=[verify_logs_after_upload],
    title="Verificar logs depois da gravacao",
    description="Confirma que a gravacao do leitor nao alterou os logs",
    always_build=True,
)
