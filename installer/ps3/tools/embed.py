"""A file as a C array, for linking into the PS3 app: python3 embed.py FILE NAME OUT.c.

The array is aligned to 128 bytes, as sysSpuImageImport wants an SPU program's ELF to be.
"""
import sys


def main() -> None:
    source, name, out = sys.argv[1:4]
    data = open(source, "rb").read()
    lines = [", ".join(f"0x{byte:02x}" for byte in data[at:at + 16]) for at in range(0, len(data), 16)]
    with open(out, "w", encoding="ascii", newline="\n") as file:
        file.write(f"/* {source}, embedded by tools/embed.py */\n")
        file.write(f"const unsigned char {name}[{len(data)}] __attribute__((aligned(128))) = {{\n")
        file.write(",\n".join(f"    {line}" for line in lines))
        file.write("\n};\n")


if __name__ == "__main__":
    main()
