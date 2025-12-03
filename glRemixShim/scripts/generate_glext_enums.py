#!/usr/bin/env python3
"""
Generate OpenGL enum definitions from gl.xml for specific extensions.
"""

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path
import textwrap
from typing import Dict, List


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-x", "--xml", type=Path, required=True,
                        help="Path to Khronos gl.xml")
    parser.add_argument("-o", "--output", type=Path, required=True,
                        help="Output .inl file for generated enum #defines")
    parser.add_argument(
        "-e", "--extensions",
        nargs="+",
        default=["GL_ARB_multitexture"],
        help="List of extensions whose enums to include",
    )
    return parser.parse_args()


def collect_global_enum_definitions(root: ET.Element) -> Dict[str, str]:
    """
    Build a mapping: enum_name -> value (as string, e.g. '0x84E0')
    from all top-level <enums><enum> entries in gl.xml.
    """
    enum_values: Dict[str, str] = {}

    for enums_block in root.findall("enums"):
        for enum_elem in enums_block.findall("enum"):
            name = enum_elem.get("name")
            value = enum_elem.get("value")

            if not name:
                continue

            # Some enums may not have a direct 'value' but an 'alias'.
            # For our purposes, ARB_multitexture enums all have values.
            if value:
                enum_values[name] = value

    return enum_values


def collect_extension_enums(
    root: ET.Element,
    enum_values: Dict[str, str],
    extensions: List[str],
) -> Dict[str, str]:
    """
    From the <extensions> section, find all <enum name="...">
    entries for the requested extensions, and map each to its
    numeric value using 'enum_values'.
    """
    result: Dict[str, str] = {}

    exts_block = root.find("extensions")
    if exts_block is None:
        raise RuntimeError("gl.xml missing <extensions> section")

    wanted = set(extensions)

    for ext in exts_block.findall("extension"):
        ext_name = ext.get("name")
        if ext_name not in wanted:
            continue

        for require in ext.findall("require"):
            for enum_elem in require.findall("enum"):
                name = enum_elem.get("name")
                if not name:
                    continue

                value = enum_values.get(name)
                if value is None:
                    # It can happen for some alias-style enums, but for
                    # GL_ARB_multitexture everything should be present.
                    continue

                result[name] = value

    return result


def write_header(output: Path, enums: Dict[str, str], extensions: List[str]) -> None:
    header = textwrap.dedent(
        f"""\
        // Auto-generated from gl.xml. Do not edit manually.
        // Extensions included: {", ".join(extensions)}
        """
    )

    lines: List[str] = [header.strip(), ""]

    for name, value in sorted(enums.items()):
        lines.append(f"#ifndef {name}")
        lines.append(f"#define {name} {value}")
        lines.append(f"#endif // {name}")
        lines.append("")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    args = parse_args()

    tree = ET.parse(args.xml)
    root = tree.getroot()

    enum_values = collect_global_enum_definitions(root)
    ext_enums = collect_extension_enums(root, enum_values, args.extensions)

    if not ext_enums:
        raise RuntimeError(
            f"No enums found for extensions: {', '.join(args.extensions)}"
        )

    write_header(args.output, ext_enums, args.extensions)


if __name__ == "__main__":
    main()