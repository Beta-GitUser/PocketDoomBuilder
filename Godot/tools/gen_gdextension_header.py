#!/usr/bin/env python3
"""Port of Godot's gdextension_interface_header_generator.cpp (4.7).

Generates gdextension_interface.h from gdextension_interface.json so the
GDExtension can be compiled against the real Godot 4.7 ABI without needing
the Godot binary locally. Logic mirrors the upstream C++ generator exactly.
"""
import json
import sys

FILE_HEADER = (
    "/**************************************************************************/\n"
    "/*  gdextension_interface.h                                               */\n"
)

INTRO = (
    "\n"
    "#pragma once\n"
    "\n"
    "/* This is a C class header, you can copy it and use it directly in your own binders.\n"
    " * Together with the `extension_api.json` file, you should be able to generate any binder.\n"
    " */\n"
    "\n"
    "#ifndef __cplusplus\n"
    "#include <stddef.h>\n"
    "#include <stdint.h>\n"
    "\n"
    "typedef uint32_t char32_t;\n"
    "typedef uint16_t char16_t;\n"
    "#else\n"
    "#include <cstddef>\n"
    "#include <cstdint>\n"
    "\n"
    "extern \"C\" {\n"
    "#endif\n"
    "\n"
)

OUTRO = (
    "#ifdef __cplusplus\n"
    "}\n"
    "#endif\n"
)


def format_type_and_name(type_str, name):
    ret = type_str
    is_pointer = False
    if ret.endswith("*"):
        # Move the trailing star to after a space: "void*" -> "void *".
        ret = ret[:-1].rstrip() + " *"
        is_pointer = True
    if name:
        if is_pointer:
            ret = ret + name
        else:
            ret = ret + " " + name
    return ret


def make_deprecated_message(data):
    parts = ["Deprecated in Godot %s." % data["since"]]
    if "message" in data:
        parts.append(data["message"])
    if "replace_with" in data:
        parts.append("Use `%s` instead." % data["replace_with"])
    return " ".join(parts)


def make_deprecated_comment_for_type(type_data):
    if "deprecated" not in type_data:
        return ""
    return " /* %s */" % make_deprecated_message(type_data["deprecated"])


def make_args_text(args):
    combined = []
    for arg in args:
        combined.append(format_type_and_name(arg["type"], arg.get("name", "")))
    return ", ".join(combined)


def write_doc(lines, doc, indent=""):
    if len(doc) == 1:
        lines.append("%s/* %s */\n" % (indent, doc[0]))
        return
    first = True
    for line in doc:
        if first:
            lines.append(indent + "/*")
            first = False
        else:
            lines.append(indent + " *")
        if line == "":
            lines.append("\n")
        else:
            lines.append(" %s\n" % line)
    lines.append(indent + " */\n")


def write_simple_type(lines, t):
    lines.append("typedef %s;%s\n" % (
        format_type_and_name(t["type"], t["name"]),
        make_deprecated_comment_for_type(t),
    ))


def write_enum_type(lines, e):
    lines.append("typedef enum {\n")
    for v in e["values"]:
        if "description" in v:
            write_doc(lines, v["description"], "\t")
        lines.append("\t%s = %d,\n" % (v["name"], v["value"]))
    lines.append("} %s;%s\n\n" % (e["name"], make_deprecated_comment_for_type(e)))


def write_function_type(lines, func):
    args_text = make_args_text(func["arguments"]) if "arguments" in func else ""
    name_and_args = "(*%s)(%s)" % (func["name"], args_text)
    return_type = func["return_value"]["type"] if "return_value" in func else "void"
    lines.append("typedef %s;%s\n" % (
        format_type_and_name(return_type, name_and_args),
        make_deprecated_comment_for_type(func),
    ))


def write_struct_type(lines, s):
    lines.append("typedef struct {\n")
    for m in s["members"]:
        if "description" in m:
            write_doc(lines, m["description"], "\t")
        lines.append("\t%s;\n" % format_type_and_name(m["type"], m["name"]))
    lines.append("} %s;%s\n\n" % (s["name"], make_deprecated_comment_for_type(s)))


def write_interface(lines, iface):
    doc = []
    doc.append("@name " + iface["name"])
    doc.append("@since " + iface["since"])
    if "deprecated" in iface:
        doc.append("@deprecated " + make_deprecated_message(iface["deprecated"]))
    for i, d in enumerate(iface["description"]):
        if i <= 1:
            doc.append("")
        doc.append(d)
    if "arguments" in iface:
        args = iface["arguments"]
        if len(args) > 0:
            doc.append("")
            for arg in args:
                arg_string = "@param " + arg["name"]
                if "description" in arg:
                    for d in arg["description"]:
                        arg_string += " " + d
                doc.append(arg_string)
    if "return_value" in iface:
        ret = iface["return_value"]
        ret_string = "@return"
        if "description" in ret:
            for d in ret["description"]:
                ret_string += " " + d
        doc.append("")
        doc.append(ret_string)
    if "see" in iface:
        see_array = iface["see"]
        if len(see_array) > 0:
            doc.append("")
            for see in see_array:
                doc.append("@see " + see)

    lines.append("/**\n")
    for d in doc:
        if d == "":
            lines.append(" *\n")
        else:
            lines.append(" * %s\n" % d)
    lines.append(" */\n")

    func = dict(iface)
    func.pop("deprecated", None)
    if "legacy_type_name" in iface:
        func["name"] = iface["legacy_type_name"]
    else:
        words = iface["name"].split("_")
        words = [w[0].upper() + w[1:] for w in words]
        func["name"] = "GDExtensionInterface" + "".join(words)
    write_function_type(lines, func)
    lines.append("\n")


def main():
    data = json.load(open(sys.argv[1]))
    out = [FILE_HEADER]
    for line in data["_copyright"]:
        out.append(line + "\n")
    out.append(INTRO)
    for t in data["types"]:
        kind = t["kind"]
        if "description" in t:
            write_doc(out, t["description"])
        if kind == "handle":
            t["type"] = "const void*" if t.get("is_const", False) else "void*"
            write_simple_type(out, t)
        elif kind == "alias":
            write_simple_type(out, t)
        elif kind == "enum":
            write_enum_type(out, t)
        elif kind == "function":
            write_function_type(out, t)
        elif kind == "struct":
            write_struct_type(out, t)
    for iface in data["interface"]:
        write_interface(out, iface)
    out.append(OUTRO)
    sys.stdout.write("".join(out))


if __name__ == "__main__":
    main()
