"""
SBA (Simple Binary Archive) Specification
-----------------------------------------
Endianness: Little-Endian (LE)
Alignment:  8-byte boundary for all index entries and data blocks.

1. GLOBAL HEADER (8 Bytes)
   - [0x00] Magic Number: char[4] = 'SBA '
   - [0x04] File Count:  uint32

2. INDEX AREA (Variable Length)
   Fixed-size fields precede the variable-length path for easier parsing.
   - [0x00] Data Offset:  uint64 (Absolute position)
   - [0x08] Data Size:    uint64 (Bytes)
   - [0x10] Timestamp:    uint64 (Unix epoch)
   - [0x18] CRC32:        uint32 (Checksum)
   - [0x1C] Path Length:  uint32 (N)
   - [0x20] Path String:  char[N] (UTF-8)
   - [Next] Padding:      uint8[0-7] (Aligns the NEXT entry to 8-byte boundary)

3. DATA AREA
   - File contents concatenated.
   - Requirement: Every data block MUST start at an 8-byte aligned offset.
"""

import os
import sys
import struct
import zlib
from datetime import datetime

MAGIC = b'SBA '

def format_size(size_bytes):
    if size_bytes < 1024: return f"{size_bytes} B"
    elif size_bytes < 1024**2: return f"{size_bytes / 1024:.2f} KB"
    elif size_bytes < 1024**3: return f"{size_bytes / 1024**2:.2f} MB"
    else: return f"{size_bytes / 1024**3:.2f} GB"

def get_crc32(path_or_file, is_stream=False, length=0):
    crc = 0
    if not is_stream:
        with open(path_or_file, 'rb') as f:
            while chunk := f.read(65536):
                crc = zlib.crc32(chunk, crc)
    else:
        remaining = length
        while remaining > 0:
            to_read = min(remaining, 65536)
            chunk = path_or_file.read(to_read)
            if not chunk: break
            crc = zlib.crc32(chunk, crc)
            remaining -= len(chunk)
    return crc & 0xFFFFFFFF

def pack_directory(source_dir):
    source_dir = os.path.normpath(os.path.abspath(source_dir))
    output_file = os.path.basename(source_dir) + ".bin"
    file_entries = []
    
    for root, dirs, files in os.walk(source_dir):
        dirs[:] = [d for d in dirs if d != '.git']
        for filename in files:
            if filename == '.git': continue
            full_path = os.path.join(root, filename)
            rel_path = os.path.relpath(full_path, source_dir).replace("\\", "/").encode('utf-8')
            stat = os.stat(full_path)
            
            file_entries.append({
                'rel': rel_path, 
                'full': full_path, 
                'size': stat.st_size, 
                'mtime': int(stat.st_mtime),
                'crc': get_crc32(full_path)
            })

    if not file_entries:
        print("Error: No files found.")
        return

    # 1. Calculate Index Size and Data Offsets
    # Header(8) + Entries
    current_pos = 8
    for e in file_entries:
        # Fixed part (32) + Path(N)
        current_pos += 32 + len(e['rel'])
        # Align next entry to 8
        current_pos = (current_pos + 7) & ~7

    header_index_size = current_pos
    current_data_ptr = header_index_size
    for e in file_entries:
        current_data_ptr = (current_data_ptr + 7) & ~7 
        e['offset'] = current_data_ptr
        current_data_ptr += e['size']

    with open(output_file, 'wb') as f:
        # Write Header
        f.write(struct.pack('<4sI', MAGIC, len(file_entries)))

        # Write Index
        for e in file_entries:
            # Fixed fields first
            f.write(struct.pack('<QQQII', e['offset'], e['size'], e['mtime'], e['crc'], len(e['rel'])))
            f.write(e['rel'])
            # Padding for 8-byte alignment of the next record
            f.write(b'\x00' * ((8 - (f.tell() % 8)) % 8))

        # Write Data
        for e in file_entries:
            f.write(b'\x00' * ((8 - (f.tell() % 8)) % 8))
            with open(e['full'], 'rb') as src:
                f.write(src.read())
    
    print(f"Archive created: {output_file}")

def list_contents(bin_file):
    if not os.path.exists(bin_file):
        print(f"Error: {bin_file} not found.")
        return

    with open(bin_file, 'rb') as f:
        header = f.read(8)
        if len(header) < 8: return
        magic, count = struct.unpack('<4sI', header)
        if magic.strip() != b'SBA':
            print("Error: Not a valid SBA archive.")
            return
        
        print(f"Archive: {bin_file} | Total Files: {count}")
        fmt = "{:<80} | {:>10} | {:>8} | {:^7} | {:<16}"
        h_text = fmt.format("Path", "Size", "CRC32", "Integr", "Timestamp")
        print(h_text)
        print("-" * len(h_text))
        
        total_size = 0
        for _ in range(count):
            # Read fixed block
            fixed_block = f.read(32)
            offset, size, mtime, crc_stored, path_len = struct.unpack('<QQQII', fixed_block)
            
            # Read variable path
            path = f.read(path_len).decode('utf-8')
            
            # Skip padding to reach next 8-byte aligned entry
            f.read((8 - (f.tell() % 8)) % 8)
            
            # Integrity Check
            current_ptr = f.tell()
            f.seek(offset)
            crc_calc = get_crc32(f, is_stream=True, length=size)
            integrity = "PASS" if crc_calc == crc_stored else "FAIL"
            f.seek(current_ptr)
            
            total_size += size
            dt = datetime.fromtimestamp(mtime).strftime('%Y-%m-%d %H:%M')
            
            display_path = (path[:77] + '..') if len(path) > 80 else path
            print(fmt.format(display_path, format_size(size), f"{crc_stored:08X}", integrity, dt))

        print("-" * len(h_text))
        print(f"Accumulated Payload: {format_size(total_size)}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python sba.py <directory | archive.bin>")
        sys.exit(1)
    
    target = sys.argv[1]
    if os.path.isdir(target):
        pack_directory(target)
    elif os.path.isfile(target) and target.lower().endswith('.bin'):
        list_contents(target)
    else:
        print("Error: Invalid argument.")