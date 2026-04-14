"""
SBA2 (Simple Binary Archive) Specification v2.2
-----------------------------------------------
Endianness: Little-Endian (LE)
Alignment:  8-byte boundary for all metadata fields and data blocks.

1. GLOBAL HEADER (8 Bytes)
   - [0x00] Magic Number: char[4] = 'SBA2'
   - [0x04] File Count:  uint32

2. INDEX AREA (Variable Length)
   Repeated 'File Count' times. Each entry follows this layout:
   - Path Length:  uint32 (N)
   - Path String:  char[N] (UTF-8, no null-terminator)
   - Padding:      uint8[0-7] (Aligns following fields to 8-byte boundary)
   - Data Offset:  uint64 (Absolute byte position in file)
   - Data Size:    uint64 (Actual bytes of file data)
   - Timestamp:    uint64 (Unix epoch in seconds)
   - CRC32:        uint32 (Checksum of the uncompressed data)
   - Reserved:     uint32 (Padding to keep index entry size predictable)

3. DATA AREA
   - File contents concatenated.
   - Requirement: Every data block MUST start at an 8-byte aligned offset.
   - Padding bytes (0x00) between blocks are ignored.
"""

import os
import sys
import struct
import zlib
from datetime import datetime

MAGIC = b'SBA2'

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
        print("Fehler: Keine Dateien gefunden.")
        return

    # Header + Index Offset Berechnung
    current_pos = 8
    for e in file_entries:
        current_pos += 4 + len(e['rel'])
        current_pos += (8 - (current_pos % 8)) % 8
        current_pos += 32 

    header_index_size = current_pos
    current_data_ptr = header_index_size
    for e in file_entries:
        current_data_ptr = (current_data_ptr + 7) & ~7 
        e['offset'] = current_data_ptr
        current_data_ptr += e['size']

    with open(output_file, 'wb') as f:
        f.write(struct.pack('<4sI', MAGIC, len(file_entries)))
        for e in file_entries:
            f.write(struct.pack('<I', len(e['rel'])))
            f.write(e['rel'])
            f.write(b'\x00' * ((8 - (f.tell() % 8)) % 8))
            f.write(struct.pack('<QQQII', e['offset'], e['size'], e['mtime'], e['crc'], 0))

        for e in file_entries:
            f.write(b'\x00' * ((8 - (f.tell() % 8)) % 8))
            with open(e['full'], 'rb') as src:
                f.write(src.read())
    
    print(f"Archiv erstellt: {output_file}")

def list_contents(bin_file):
    if not os.path.exists(bin_file):
        print(f"Fehler: {bin_file} nicht gefunden.")
        return

    with open(bin_file, 'rb') as f:
        data = f.read(8)
        if len(data) < 8: return
        magic, count = struct.unpack('<4sI', data)
        if magic != MAGIC:
            print("Fehler: Ungültiges SBA2-Archiv.")
            return
        
        print(f"Archiv: {bin_file} | Dateien: {count}")
        # Breite Spaltenkonfiguration
        fmt = "{:<80} | {:>10} | {:>8} | {:^7} | {:<16}"
        header_text = fmt.format("Dateipfad", "Größe", "CRC32", "Check", "Zeitstempel")
        print(header_text)
        print("-" * len(header_text))
        
        total_size = 0
        index_pos = 8

        for _ in range(count):
            f.seek(index_pos)
            path_len = struct.unpack('<I', f.read(4))[0]
            path = f.read(path_len).decode('utf-8')
            f.read((8 - (f.tell() % 8)) % 8)
            
            offset, size, mtime, crc_stored, _ = struct.unpack('<QQQII', f.read(32))
            index_pos = f.tell()

            # Integritätsprüfung
            f.seek(offset)
            crc_calc = get_crc32(f, is_stream=True, length=size)
            integrity = "PASS" if crc_calc == crc_stored else "FAIL"
            
            total_size += size
            dt = datetime.fromtimestamp(mtime).strftime('%Y-%m-%d %H:%M')
            
            # Pfad wird bei extremer Überlänge gekürzt, Spalte ist aber auf 80 Zeichen gesetzt
            display_path = (path[:77] + '..') if len(path) > 80 else path
            print(fmt.format(display_path, format_size(size), f"{crc_stored:08X}", integrity, dt))

        print("-" * len(header_text))
        print(f"Gesamte Nutzlast: {format_size(total_size)}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Nutzung: python sba2.py <verzeichnis | archiv.bin>")
        sys.exit(1)
    
    target = sys.argv[1]
    if os.path.isdir(target):
        pack_directory(target)
    elif os.path.isfile(target) and target.lower().endswith('.bin'):
        list_contents(target)
    else:
        print("Fehler: Ungültiges Argument.")