"""Strict, hardware-free validation and assembly of the three T41 boot slots."""
from pathlib import Path
import hashlib
import os
import struct
import subprocess
import tempfile

SLOTS = {"T41_audio": 0x60000000, "T41": 0x60100000, "T41_MTP": 0x60200000}
SLOT_SIZE = 0x100000
BOOT_ENTRY = 0x60001000


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_hex(path):
    """Accept the Intel HEX records emitted by this pinned ARM toolchain."""
    data, upper, entry, ended = {}, 0, None, False
    for number, line in enumerate(Path(path).read_text(encoding="ascii").splitlines(), 1):
        if not line or ended or not line.startswith(":"):
            raise ValueError(f"{path}:{number}: invalid record or data after EOF")
        try:
            raw = bytes.fromhex(line[1:])
        except ValueError as error:
            raise ValueError(f"{path}:{number}: malformed hex") from error
        if len(line) != 1 + 2 * len(raw) or len(raw) < 5 or len(raw) != raw[0] + 5 or sum(raw) & 255:
            raise ValueError(f"{path}:{number}: length/checksum mismatch")
        count, address, kind = raw[0], int.from_bytes(raw[1:3], "big"), raw[3]
        payload = raw[4:-1]
        if kind == 0:
            if not count or address + count > 0x10000:
                raise ValueError("empty data or record crossing a 64 KiB boundary")
            for offset, value in enumerate(payload):
                absolute = upper + address + offset
                if absolute in data:
                    raise ValueError(f"duplicate/overlapping data at {absolute:#x}")
                data[absolute] = value
        elif kind == 1 and count == 0 and address == 0:
            ended = True
        elif kind == 4 and count == 2 and address == 0:
            upper = int.from_bytes(payload, "big") << 16
        elif kind == 5 and count == 4 and address == 0 and entry is None:
            entry = int.from_bytes(payload, "big")
        else:
            raise ValueError(f"{path}:{number}: unsupported or repeated metadata record {kind}")
    if not ended or not data or entry is None:
        raise ValueError(f"{path}: missing EOF, data, or execution-start record")
    return data, entry


def words(data, address, count):
    try:
        return struct.unpack("<" + "I" * count, bytes(data[address + i] for i in range(count * 4)))
    except KeyError as error:
        raise ValueError(f"missing boot-header byte at {address:#x}") from error


def validate_slot(profile, path):
    base = SLOTS[profile]
    data, entry = read_hex(path)
    if min(data) < base or max(data) >= base + SLOT_SIZE:
        raise ValueError(f"{profile}: data outside its assigned 1 MiB slot")
    if entry != base + 0x1000:
        raise ValueError(f"{profile}: wrong execution-start address")
    if words(data, base, 1)[0] != 0x42464346:
        raise ValueError(f"{profile}: missing FlexSPI configuration signature")
    header, reset, reserved, dcd, boot, self_address, csf, reserved2 = words(data, base + 0x1000, 8)
    # Match the pinned Teensy core's bootdata.c, not a generic i.MX RT template.
    if (header != 0x432000D1 or self_address != base + 0x1000 or
            boot != base + 0x1020 or reserved or reserved2 or dcd):
        raise ValueError(f"{profile}: invalid/unsupported ROM image vector table")
    if not reset & 1 or (reset & ~1) not in data or (reset & ~1) < base + 0x1400:
        raise ValueError(f"{profile}: invalid reset entry")
    start, length, plugin = words(data, boot, 3)
    # The core fixes BootData.start at the physical flash base in ALL slots.
    # Alternate slots are entered by Main.cpp's explicit ResetHandler jump.
    if start != 0x60000000 or plugin or length > SLOT_SIZE or length != max(data) - base + 1:
        raise ValueError(f"{profile}: invalid ROM boot-data extent")
    if csf < base + 0x1400 or csf + 3072 != base + length or any(words(data, csf, 768)):
        raise ValueError(f"{profile}: invalid unsigned HAB placeholder")
    return data, {"profile": profile, "path": str(Path(path).resolve()),
                  "sha256": sha256(path), "dataBytes": len(data),
                  "firstAddress": hex(min(data)), "lastAddress": hex(max(data)),
                  "entry": hex(entry), "reset": hex(reset), "bootImageBytes": length}


def validate_suite(inputs, output):
    if set(inputs) != set(SLOTS):
        raise ValueError("exactly T41_audio, T41 and T41_MTP are required")
    union, profiles = {}, []
    for profile in SLOTS:
        data, details = validate_slot(profile, inputs[profile])
        if union.keys() & data.keys():
            raise ValueError("overlapping boot slots")
        union.update(data)
        profiles.append(details)
    actual, entry = read_hex(output)
    if actual != union:
        raise ValueError("assembled bytes differ from the exact three-slot union")
    if entry != BOOT_ENTRY:
        raise ValueError("assembled entry must select the slot-0 multiboot image")
    return {"profiles": profiles, "image": {"path": str(Path(output).resolve()),
            "sha256": sha256(output), "fileBytes": Path(output).stat().st_size,
            "dataBytes": len(actual), "entry": hex(entry)},
            "validation": "STRUCTURAL_PASS_ONLY", "hardwareValidated": False}


def assemble(tool, inputs, output):
    output = Path(output).resolve()
    if set(inputs) != set(SLOTS):
        raise ValueError("exactly three boot profiles are required")
    if output in [Path(path).resolve() for path in inputs.values()]:
        raise ValueError("output must not overwrite an input")
    for profile in SLOTS:
        validate_slot(profile, inputs[profile])
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish only after successful execution AND independent byte validation.
    with tempfile.TemporaryDirectory(prefix=".suite-assembly-", dir=output.parent) as temporary:
        temporary = Path(temporary)
        args = [str(tool)]
        for profile in SLOTS:
            path = Path(inputs[profile])
            if profile != "T41_audio":
                # Keep all data; only slot 0 supplies the combined start record.
                normalized = temporary / f"{profile}.hex"
                normalized.write_text("".join(line + "\n" for line in path.read_text().splitlines()
                                              if line[7:9] != "05"), encoding="ascii")
                path = normalized
            args.extend([str(path), "-Intel"])
        candidate = temporary / "candidate.hex"
        subprocess.run(args + ["-o", str(candidate), "-Intel"], check=True)
        details = validate_suite(inputs, candidate)
        os.replace(candidate, output)
    details["image"]["path"] = str(output)
    return details
