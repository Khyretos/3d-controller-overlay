#!/usr/bin/env python3
import os
import sys

input_file = "assets/gamecontrollerdb.txt"
output_header = "src/gamecontrollerdb_data.h"

if not os.path.isfile(input_file):
    print(
        f"Warning: {input_file} not found – building with an empty mapping.",
        file=sys.stderr,
    )
    content = ""
else:
    with open(input_file, "r") as f:
        content = f.read()

# Escape backslashes, quotes, and newlines for a C string literal
escaped = content.replace("\\", "\\\\").replace('"', '\\"').replace("\n", '\\n"\n"')

header = f"""// Auto-generated from {input_file} – do not edit
#pragma once
#include <cstddef>

namespace Embedded {{
    static const char gamecontrollerdb_data[] =
        "{escaped}";
    static constexpr std::size_t gamecontrollerdb_size = {len(content)};
}}
"""

with open(output_header, "w") as f:
    f.write(header)

print(f"Generated {output_header} ({len(content)} bytes)")
