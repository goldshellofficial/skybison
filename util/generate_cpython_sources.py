#!/usr/bin/env python3
import argparse
import collections
import os
import re
import sys


# Tuple that specifies a regex and the position where a symbol will be found
SymbolRegex = collections.namedtuple("SymbolRegex", ["regex", "pos"])


# For sources where only the symbol is required
SYMBOL_REGEX = {
    "typedef": SymbolRegex(
        regex=re.compile("^typedef.*;", re.MULTILINE), pos=2
    ),
    "multiline_typedef": SymbolRegex(
        regex=re.compile("^} .*;", re.MULTILINE), pos=0
    ),
    "struct": SymbolRegex(regex=re.compile("^struct.*{", re.MULTILINE), pos=1),
    "macro": SymbolRegex(
        regex=re.compile("^#define.*[^\\\\]\n", re.MULTILINE), pos=1
    ),
    "multiline_macro": SymbolRegex(
        regex=re.compile("^#define.*\\\\", re.MULTILINE), pos=1
    ),
    "pytypeobject": SymbolRegex(
        regex=re.compile('^extern "C".*PyTypeObject.*_Type', re.MULTILINE),
        pos=3,
    ),
    "pytypeobject_macro": SymbolRegex(
        regex=re.compile("^#define.*_Type ", re.MULTILINE), pos=1
    ),
    "pyfunction": SymbolRegex(
        regex=re.compile('^extern "C".*', re.MULTILINE), pos=3
    ),
}


# For sources where the entire definition match is required
DEFINITIONS_REGEX = {
    "typedef": SymbolRegex(
        regex=re.compile("^typedef.*;.*\n", re.MULTILINE), pos=2
    ),
    "multiline_typedef": SymbolRegex(
        regex=re.compile("^typedef.*{(.|\n)*?}.*;.*\n", re.MULTILINE), pos=-1
    ),
    "struct": SymbolRegex(
        regex=re.compile("^struct(.|\n)*?};.*\n", re.MULTILINE), pos=1
    ),
    "macro": SymbolRegex(
        regex=re.compile("^#define.*[^\\\\]\n", re.MULTILINE), pos=1
    ),
    "multiline_macro": SymbolRegex(
        regex=re.compile("^#define.*\\\\(\n.*\\\\)*\n.*\n", re.MULTILINE), pos=1
    ),
    "pytypeobject": SymbolRegex(
        regex=re.compile("^PyTypeObject.*= {(.|\n)*?};.*\n", re.MULTILINE),
        pos=1,
    ),
    "pytypeobject_macro": SymbolRegex(
        regex=re.compile("^PyAPI_DATA\(.*;.*\n", re.MULTILINE), pos=2
    ),
    # This regex looks for function declarations. The pattern is that they
    # start with either an a-zA-Z character from either the static or the
    # return type. Then, match all the way to the '{' which opens the function
    # scpoe. Finally, the regex matches all the way up to the closing scope '}'
    "pyfunction": SymbolRegex(
        regex=re.compile("^[a-zA-Z](.|.\n)*?{(.|\n)*?}.*\n", re.MULTILINE),
        pos=1,
    ),
}


# Given a source file, find the matched patterns
def find_symbols_in_file(symbols_dict, lines):
    symbols_dict = {x: [] for x in SYMBOL_REGEX.keys()}
    special_chars_regex = re.compile("[\*|,|;|{|}|\(|\)|]")
    for symbol_type, sr in SYMBOL_REGEX.items():
        matches = re.findall(sr.regex, lines)
        for match in matches:
            # Modify typedefs with the following signature:
            # type (*name)(variables) -> type (*name variables)
            modified_match = re.sub("\)\(", " ", match)
            # Remove extra characters to standardize symbol location
            # type (*name variables...) -> type name variables
            modified_match = re.sub(special_chars_regex, " ", modified_match)
            # Split and locate symbol based on its position
            modified_match = modified_match.split()
            if len(modified_match) > sr.pos:
                symbols_dict[symbol_type].append(modified_match[sr.pos])
    return symbols_dict


# Given a list of files, find all the defined symbols
def create_symbols_dict(modified_source_paths):
    symbols_dict = {x: [] for x in SYMBOL_REGEX.keys()}
    for path in modified_source_paths:
        lines = open(path, "r", encoding="utf-8").read()
        symbols_found = find_symbols_in_file(symbols_dict, lines)
        for symbol_type, symbols in symbols_found.items():
            symbols_dict[symbol_type].extend(symbols)
    return symbols_dict


# The set of heuristics to determine if a substitution should be performed
def replace_definition_if(match, symbol, pos):
    static_function_regex = re.compile("^static ", re.MULTILINE)
    special_chars_regex = re.compile("[\*|,|;|\(|\)|]")
    original_match = match.group(0)
    # Remove extra characters to standardize symbol location
    modified_match = re.sub(special_chars_regex, " ", original_match)
    # Offset the position by one when dealing with static functions.
    # For example:
    # static type foo() vs type foo().
    # The static qualifier offsets the position by one.
    if re.search(static_function_regex, modified_match):
        pos += 1
    # Verify that this is indeed the definition for symbol
    if symbol == modified_match.split()[pos]:
        return ""
    return original_match


# Given a source file, replace the matched patterns
def modify_file(lines, symbols_dict):
    # Iterate dictionary of symbol types (i.e. macro, typedef, etc.)
    for symbol_type, symbols in symbols_dict.items():
        sr = DEFINITIONS_REGEX[symbol_type]
        # Iterate the symbols that will be replaced
        for symbol in symbols:
            lines = re.sub(
                sr.regex,
                lambda m: replace_definition_if(m, symbol, sr.pos),
                lines,
            )
    return lines


# Given a list of sources files, modify the patterns that were annotated
def create_output_file_dict(source_paths, symbols_dict):
    output_file_dict = {}
    # Iterate all source files
    for path in source_paths:
        lines = open(path, "r", encoding="utf-8").read()
        output_file_dict[path] = modify_file(lines, symbols_dict)
    return output_file_dict


# Given a dictionary of files, write the files to the output directory
def output_files_to_directory(output_directory, output_file_dict):
    for file_path in output_file_dict.keys():
        # Create directory
        output_path = f"{output_directory}/gen"
        file_name = file_path.split("/")[-1]
        if file_name.endswith(".h"):
            output_path = f"{output_path}/include/{file_name}"
        else:
            output_path = f"{output_path}/lib/{file_name}"
        if not os.path.exists(os.path.dirname(output_path)):
            os.makedirs(os.path.dirname(output_path))

        # Write files
        with open(output_path, "w+", encoding="utf-8") as f:
            f.writelines(output_file_dict[file_path])


def main(args):
    parser = argparse.ArgumentParser(
        description="This strips out the unneeded definitions in the CPython Extension Subsystem"
    )
    parser.add_argument(
        "-sources",
        nargs="*",
        required=True,
        help="A list of sources from the CPython Extension Subsystem",
    )
    parser.add_argument(
        "-modified",
        nargs="*",
        required=True,
        help="A list of sources that modify the CPython Extension Subsystem",
    )
    parser.add_argument(
        "-output_dir",
        required=True,
        help="The directory in which to output the processed sources",
    )
    args = parser.parse_args()

    symbols_dict = create_symbols_dict(args.modified)
    output_file_dict = create_output_file_dict(args.sources, symbols_dict)
    output_files_to_directory(args.output_dir, output_file_dict)


if __name__ == "__main__":
    main(sys.argv)
