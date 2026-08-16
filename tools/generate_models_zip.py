#!/usr/bin/env python3
import os
import sys
import zipfile
from io import BytesIO

models_dir = "assets/models"
out_header = "src/models_zip_data.h"

if not os.path.isdir(models_dir):
    print(f"Error: {models_dir} not found")
    sys.exit(1)

# Create zip in memory
zip_buffer = BytesIO()
with zipfile.ZipFile(zip_buffer, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, _, files in os.walk(models_dir):
        for file in files:
            full_path = os.path.join(root, file)
            arcname = os.path.relpath(full_path, start=os.path.dirname(models_dir))
            zf.write(full_path, arcname)

data = zip_buffer.getvalue()
hex_bytes = ", ".join(f"0x{b:02x}" for b in data)

header = f"""// Auto-generated from {models_dir} – do not edit
#pragma once
namespace Embedded {{
    extern const unsigned char models_zip_data[] = {{
        {hex_bytes}
    }};
    extern const unsigned int models_zip_size = {len(data)};
}}
"""

with open(out_header, "w") as f:
    f.write(header)

print(f"Generated {out_header} ({len(data)} bytes from {models_dir})")
