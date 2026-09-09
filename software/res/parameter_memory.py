"""Fail T41 applet builds if parameter metadata/control escapes its boot slot.

This inspects the final linked ELF. It does not establish runtime IRQ latency
or hardware acceptance. Run standalone tests without loading SCons.
"""
import re
import shutil
import subprocess

FLASH = {"T41": (0x60100000, 0x60200000),
         "T41_audio": (0x60000000, 0x60100000)}
REQUIRED = ("Owner::Find(", "Owner::Describe(", "Owner::DescribeParameter(",
            "Owner::ReadParameter(", "Owner::SetParameter(", "NativeAllowed(")


def validate(text, target):
    start, end = FLASH[target]
    symbols = []
    for line in text.splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)", line)
        if match:
            address, size, kind, name = match.groups()
            symbols.append((int(address, 16), int(size, 16), kind, name))
    metadata = [s for s in symbols if re.search(
        r"::Parameters\(\) const::(?:entries|monoModes|stereoModes)$|(?:^|::)CvSourceOptions$", s[3])]
    tables = [s for s in metadata if s[3].endswith("::entries")]
    control = [s for s in symbols if s[2] in "tTwW" and (re.match(
        r"phz::parameters::(?:Owner::|NativeAllowed\(|Field<|CvMapping<)", s[3])
        or re.search(r"::Parameters\(\) const(?: \[clone[^\]]*\])?$", s[3]))]
    if len(tables) < 8 or not any(s[3].endswith("CvSourceOptions") for s in metadata):
        raise ValueError("Missing parameter declaration tables or shared CV options")
    getters = {re.sub(r" \[clone[^\]]*\]$", "", s[3]) for s in control}
    for table in tables:
        if table[3].removesuffix("::entries") not in getters:
            raise ValueError("Missing schema getter: " + table[3])
    for required in REQUIRED:
        if not any(s[3].startswith("phz::parameters::" + required) for s in control):
            raise ValueError("Missing control function: " + required)
    names = set()
    for address, size, kind, name in metadata + control:
        if name in names:
            raise ValueError("Duplicate parameter symbol: " + name)
        names.add(name)
        if size <= 0 or address < start or address + size > end:
            raise ValueError("Parameter symbol outside active flash slot: " + name)
    if any(s[2] not in "rR" for s in metadata):
        raise ValueError("Parameter metadata is not read-only flash")
    return (len(tables), sum(s[1] for s in metadata),
            len(control), sum(s[1] for s in control))


def check_after_link(source, target, env):
    nm = shutil.which("arm-none-eabi-nm", path=str(env["ENV"]["PATH"]))
    if nm is None:
        raise RuntimeError("Cannot locate the pinned ARM symbol inspector")
    symbols = subprocess.check_output([nm, "-C", "-S", str(target[0])], text=True)
    tables, data_bytes, functions, code_bytes = validate(symbols, env["PIOENV"])
    print(f"Parameter memory PASS: {tables} tables / {data_bytes} metadata bytes; "
          f"{functions} control functions / {code_bytes} code bytes in active flash slot")


if "Import" in globals():
    Import("env")
    if env["PIOENV"] in FLASH:
        env.Depends("$BUILD_DIR/${PROGNAME}.elf", "$PROJECT_DIR/res/parameter_memory.py")
        env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_after_link)
