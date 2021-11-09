#!/usr/bin/env python3
"""Generate CMakeLists.txt for an nginx module build

TODO command line argument

Write a CMakeLists.txt file to standard output that describes a static
("object") build of an nginx module.

Read JSON from standard input that contains the source files and include
directories relevant to the build.
"""

import json
from pathlib import Path
import sys

target_name = sys.argv[1]
build_info = json.load(sys.stdin)
include_directories, c_sources = build_info['include_directories'], build_info['c_sources']

# Prefix the paths with the path to the nginx repository directory (./nginx).
nginx = Path('nginx')
include_directories = [str(nginx / path) for path in include_directories]
c_sources = [str(nginx / path) for path in c_sources]

template = """\
# This file is generated by bin/generate_cmakelists.py

cmake_minimum_required(VERSION 3.12)

project({target_name})

add_library({target_name} OBJECT)
set_property(TARGET {target_name} PROPERTY POSITION_INDEPENDENT_CODE ON)

target_sources({target_name}
    PRIVATE
{sources_indent}{nginx}/objs/ngx_http_datadog_module_modules.c
)

include_directories(
    SYSTEM
{includes_indent}{includes}
)
"""

sources_indent = ' ' * 8
includes_indent = sources_indent

print(
    template.format(target_name=target_name,
                    sources_indent=sources_indent,
                    nginx=str(nginx),
                    includes_indent=includes_indent,
                    includes=('\n' + includes_indent).join(include_directories)))
