"""
SBA (Simple Binary Archive) Specification
-----------------------------------------
Endianness: Little-Endian (LE)
Alignment:  8-byte boundary for all index entries and data blocks.

1. GLOBAL HEADER (8 Bytes)
   - [0x00] Magic Number: char[4] = 'SBA '
   - [0x04] File Count:   uint32

2. INDEX AREA (Variable Length)
   Fixed-size fields precede the variable-length path for easier parsing.
   - [0x00] Data Offset:  uint64 (Absolute position)
   - [0x08] Data Size:    uint64 (Bytes)
   - [0x10] Timestamp:    uint64 (Unix epoch)
   - [0x18] GUID:         uint64 (64-bit ID)
   - [0x20] CRC32:        uint32 (Checksum)
   - [0x24] Path Length:  uint32 (N)
   - [0x28] Path String:  char[N] (UTF-8)
   - [Next] Padding:      uint8[0-7] (Aligns the NEXT entry to 8-byte boundary)

3. DATA AREA
   - File contents concatenated.
   - Requirement: Every data block MUST start at an 8-byte aligned offset.
"""

import os
import sys
import struct
import zlib
import random
import argparse
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

def load_existing_index(bin_file):
    """Loads GUIDs from an existing archive to allow for move/rename detection."""
    by_path = {}
    by_content = {} # (size, crc) -> guid
    
    if not os.path.exists(bin_file):
        return by_path, by_content
    
    with open(bin_file, 'rb') as f:
        header = f.read(8)
        if len(header) < 8: return by_path, by_content
        magic, count = struct.unpack('<4sI', header)
        if magic != MAGIC: return by_path, by_content
        
        for _ in range(count):
            fixed_block = f.read(40)
            if len(fixed_block) < 40: break
            _, size, _, guid, crc, path_len = struct.unpack('<QQQQII', fixed_block)
            path_bytes = f.read(path_len)
            f.read((8 - (f.tell() % 8)) % 8)
            
            path_str = path_bytes.decode('utf-8')
            by_path[path_str] = guid
            by_content[(size, crc)] = guid
            
    return by_path, by_content

def write_sba(output_file, file_entries):
    """Core logic to write the binary structure according to spec."""
    # 1. Calculate Offsets
    current_pos = 8 # Header
    for e in file_entries:
        current_pos += 40 + len(e['rel_bytes'])
        current_pos = (current_pos + 7) & ~7

    current_data_ptr = current_pos
    for e in file_entries:
        current_data_ptr = (current_data_ptr + 7) & ~7 
        e['offset'] = current_data_ptr
        current_data_ptr += e['size']

    # 2. Write File
    with open(output_file, 'wb') as f:
        f.write(struct.pack('<4sI', MAGIC, len(file_entries)))

        # Index
        for e in file_entries:
            f.write(struct.pack('<QQQQII', e['offset'], e['size'], e['mtime'], e['guid'], e['crc'], len(e['rel_bytes'])))
            f.write(e['rel_bytes'])
            f.write(b'\x00' * ((8 - (f.tell() % 8)) % 8))

        # Data
        for e in file_entries:
            f.write(b'\x00' * ((8 - (f.tell() % 8)) % 8))
            with open(e['full'], 'rb') as src:
                while chunk := src.read(65536):
                    f.write(chunk)
    
    print(f"Archive written: {output_file} ({len(file_entries)} files)")

def cmd_pack(source_dir, use_heuristics=False):
    source_dir = os.path.normpath(os.path.abspath(source_dir))
    output_file = os.path.basename(source_dir) + ".bin"
    
    by_path, by_content = ({}, {})
    if use_heuristics:
        by_path, by_content = load_existing_index(output_file)

    file_entries = []
    for root, dirs, files in os.walk(source_dir):
        dirs[:] = [d for d in dirs if d != '.git']
        for filename in files:
            full_path = os.path.join(root, filename)
            rel_path = os.path.relpath(full_path, source_dir).replace("\\", "/")
            rel_bytes = rel_path.encode('utf-8')
            stat = os.stat(full_path)
            size = stat.st_size
            crc = get_crc32(full_path)
            
            # GUID Preservation Logic (Heuristic)
            guid = None
            if rel_path in by_path:
                guid = by_path[rel_path]
            elif (size, crc) in by_content:
                guid = by_content[(size, crc)]
                print(f"  [Move Detected] {rel_path} (Retaining GUID)")
            else:
                guid = random.getrandbits(64)
            
            file_entries.append({
                'rel_bytes': rel_bytes, 'full': full_path, 'size': size, 
                'mtime': int(stat.st_mtime), 'guid': guid, 'crc': crc
            })

    if not file_entries:
        print("Error: No files found.")
        return

    write_sba(output_file, file_entries)

def cmd_list(bin_file):
    if not os.path.exists(bin_file):
        print(f"Error: {bin_file} not found.")
        return

    with open(bin_file, 'rb') as f:
        header = f.read(8)
        if len(header) < 8: return
        magic, count = struct.unpack('<4sI', header)
        if magic != MAGIC:
            print("Error: Not a valid SBA archive.")
            return
        
        print(f"Archive: {bin_file} | Files: {count}")
        fmt = "{:<45} | {:>16} | {:>10} | {:>8} | {:^6}"
        print(fmt.format("Path", "GUID (Hex)", "Size", "CRC32", "Check"))
        print("-" * 95)
        
        for _ in range(count):
            fixed_block = f.read(40)
            offset, size, mtime, guid, crc_stored, path_len = struct.unpack('<QQQQII', fixed_block)
            path = f.read(path_len).decode('utf-8')
            f.read((8 - (f.tell() % 8)) % 8)
            
            # Fast check: skip data verification unless needed? 
            # (Keeping it here as requested in previous version)
            curr = f.tell()
            f.seek(offset)
            crc_calc = get_crc32(f, is_stream=True, length=size)
            f.seek(curr)
            
            status = "OK" if crc_calc == crc_stored else "ERR"
            disp_path = (path[:42] + '..') if len(path) > 45 else path
            print(fmt.format(disp_path, f"{guid:016X}", format_size(size), f"{crc_stored:08X}", status))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SBA Binary Archive Tool")
    subparsers = parser.add_subparsers(dest="command", help="Sub-commands")

    # Pack: Fresh creation
    p_pack = subparsers.add_parser('pack', help="Create a new archive (new GUIDs)")
    p_pack.add_argument('dir', help="Directory to pack")

    # Update: Pack with GUID preservation
    p_upd = subparsers.add_parser('update', help="Update archive (preserves GUIDs for moves/renames)")
    p_upd.add_argument('dir', help="Directory to update")

    # List: Show contents
    p_list = subparsers.add_parser('list', help="List archive contents")
    p_list.add_argument('file', help="SBA file to inspect")

    args = parser.parse_args()

    if args.command == 'pack':
        cmd_pack(args.dir, use_heuristics=False)
    elif args.command == 'update':
        cmd_pack(args.dir, use_heuristics=True)
    elif args.command == 'list':
        cmd_list(args.file)
    else:
        parser.print_help()