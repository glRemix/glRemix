"""Generate OpenGL wrapper definitions from gl.xml."""

import argparse
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Dict, List, Set, Tuple


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-x", "--xml", type=Path, required=True, help="Path to the Khronos gl.xml registry")
    parser.add_argument("-o", "--output", type=Path, required=True, help="Path to write the generated .inl file")
    parser.add_argument(
        "-a", "--alias-output",
        type=Path,
        help="Optional path to write linker alias helper macros for the exported functions",
    )
    parser.add_argument(
        "-m", "--min-version",
        default="1.0",
        help="Lowest OpenGL core version (inclusive) to include",
    )
    parser.add_argument(
        "-M", "--max-version",
        default="1.1",
        help="Highest OpenGL core version (inclusive) to include",
    )
    return parser.parse_args()


def parse_version(value: str) -> Tuple[int, ...]:
    return tuple(int(part) for part in value.split("."))


def collect_command_elements(root: ET.Element) -> Dict[str, ET.Element]:
    command_elements: Dict[str, ET.Element] = {}
    commands = root.find("commands")
    if commands is None:
        raise RuntimeError("gl.xml missing <commands> section")

    for command in commands.findall("command"):
        proto = command.find("proto")
        if proto is None:
            continue
        name_elem = proto.find("name")
        if name_elem is None:
            continue
        name = name_elem.text
        if not name:
            continue
        command_elements[name] = command
    return command_elements


def collect_feature_commands(
    root: ET.Element,
    min_version: Tuple[int, ...],
    max_version: Tuple[int, ...],
) -> Set[str]:
    names: Set[str] = set()
    for feature in root.findall("feature"):
        api = feature.get("api")
        if api not in ("gl", "glcore"):
            continue
        version_text = feature.get("number")
        if not version_text:
            continue
        version = parse_version(version_text)
        if version < min_version or version > max_version:
            continue
        for require in feature.findall("require"):
            for command in require.findall("command"):
                name = command.get("name")
                if name:
                    names.add(name)
    return names


def normalize_whitespace(value: str) -> str:
    collapsed = " ".join(value.replace("\n", " ").replace("\t", " ").split())
    return collapsed.strip()


def extract_return_type(proto: ET.Element) -> str:
    name_elem = proto.find("name")
    proto_text = "".join(proto.itertext())
    name = name_elem.text if name_elem is not None else ""
    if name:
        # Remove only the last occurrence of the name to preserve pointer tokens before it.
        idx = proto_text.rfind(name)
        if idx != -1:
            before = proto_text[:idx]
            after = proto_text[idx + len(name) :]
            proto_text = before + after
    return normalize_whitespace(proto_text)


def extract_param_signature(param: ET.Element) -> Tuple[str, str, str]:
    name_elem = param.find("name")
    name = name_elem.text if name_elem is not None else ""
    signature = normalize_whitespace("".join(param.itertext()))
    type_only = signature
    if name:
        idx = type_only.rfind(name)
        if idx != -1:
            type_only = type_only[:idx]
    type_only = type_only.strip()
    return signature, name, type_only


def build_command_signature(command: ET.Element):
    proto = command.find("proto")
    if proto is None:
        raise RuntimeError("Command missing <proto> block")
    return_type = extract_return_type(proto)
    name_elem = proto.find("name")
    if name_elem is None or not name_elem.text:
        raise RuntimeError("Command missing <name> entry")
    command_name = name_elem.text

    param_entries = [extract_param_signature(param) for param in command.findall("param")]
    parameters: List[str] = [entry[0] for entry in param_entries]
    param_names: List[str] = [entry[1] for entry in param_entries if entry[1]]
    param_types: List[str] = [entry[2] for entry in param_entries]

    if not parameters:
        parameter_clause = "()"
    else:
        parameter_clause = f"({', '.join(parameters)})"

    call_arguments = ", ".join(param_names)

    call_expr = f"({call_arguments})" if call_arguments else "()"

    return {
        "return_type": return_type,
        "name": command_name,
        "parameter_clause": f"{parameter_clause}",
        "call_expr": call_expr,
        "param_types": param_types,
    }


def normalize_type_spelling(type_text: str) -> str:
    text = type_text.replace("*", " * ")
    parts = [part for part in text.replace(",", " ").split() if part]
    return " ".join(parts)


def parameter_stack_size(param_types: List[str]) -> int:
    total = 0
    for type_text in param_types:
        normalized = normalize_type_spelling(type_text)
        if not normalized:
            continue
        total += single_param_size(normalized)
    return total


def single_param_size(type_text: str) -> int:
    # Treat any explicit pointer or reference as a pointer-sized argument.
    if "*" in type_text or "&" in type_text or "(" in type_text:
        return 4

    qualifiers = {"const", "volatile", "struct", "enum"}
    tokens = [token for token in type_text.split() if token not in qualifiers]
    if not tokens:
        return 4

    base = " ".join(tokens)
    base = base.replace("GLAPI", "").strip()

    size_map = {
        "GLbyte": 1,
        "GLubyte": 1,
        "GLchar": 1,
        "GLcharARB": 1,
        "GLboolean": 1,
        "GLshort": 2,
        "GLushort": 2,
        "GLhalfNV": 2,
        "GLhalf": 2,
        "GLfixed": 4,
        "GLint": 4,
        "GLuint": 4,
        "GLenum": 4,
        "GLsizei": 4,
        "GLsizeiptr": 4,
        "GLsizeiptrARB": 4,
        "GLintptr": 4,
        "GLintptrARB": 4,
        "GLfloat": 4,
        "GLclampf": 4,
        "GLdouble": 8,
        "GLclampd": 8,
        "GLbitfield": 4,
        "GLsync": 4,
        "GLhalfARB": 2,
        "GLhandleARB": 4,
        "GLvdpauSurfaceNV": 4,
        "GLeglImageOES": 4,
        "GLDEBUGPROC": 4,
        "GLDEBUGPROCARB": 4,
        "GLDEBUGPROCKHR": 4,
        "GLDEBUGPROCAMD": 4,
    }

    size = size_map.get(base, 4)
    if size < 4:
        return 4
    return size


def generate_wrappers(args: argparse.Namespace) -> None:
    tree = ET.parse(args.xml)
    root = tree.getroot()

    min_version = parse_version(args.min_version)
    max_version = parse_version(args.max_version)

    command_elements = collect_command_elements(root)
    required_names = collect_feature_commands(root, min_version, max_version)

    missing: List[str] = [name for name in sorted(required_names) if name not in command_elements]
    if missing:
        raise RuntimeError(f"Commands missing from registry: {', '.join(missing)}")

    signatures = [build_command_signature(command_elements[name]) for name in sorted(required_names)]

    header = textwrap.dedent(
        """\
        // Auto-generated. Do not edit manually.
        """
    )

    lines: List[str] = [header.strip()] + [
        f"GLREMIX_GL_VOID_WRAPPER({signature['name']}, {signature['parameter_clause']}, {signature['call_expr']})"
        if signature["return_type"] == "void"
        else f"GLREMIX_GL_RETURN_WRAPPER({signature['return_type']}, {signature['name']}, {signature['parameter_clause']}, {signature['call_expr']})"
        for signature in signatures
    ]

    output_text = "\n".join(lines) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output_text, encoding="utf-8")

    if args.alias_output:
        alias_lines: List[str] = [header.strip()]

        ix86_lines = [
            f"#pragma comment(linker, \"/export:{signature['name']}=_glRemix_{signature['name']}@{parameter_stack_size(signature['param_types'])}\")"
            for signature in signatures
        ]
        generic_lines = [
            f"#pragma comment(linker, \"/export:{signature['name']}=glRemix_{signature['name']}\")"
            for signature in signatures
        ]

        alias_lines.append("#if defined(_M_IX86)")
        alias_lines.extend(ix86_lines)
        alias_lines.append("#else")
        alias_lines.extend(generic_lines)
        alias_lines.append("#endif")

        alias_text = "\n".join(alias_lines) + "\n"
        args.alias_output.parent.mkdir(parents=True, exist_ok=True)
        args.alias_output.write_text(alias_text, encoding="utf-8")


if __name__ == "__main__":
    generate_wrappers(parse_args())
