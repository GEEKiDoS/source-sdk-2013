"""Exercise the production hlight_pak_metadata_probe executable; never load malformed BSPs."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

def archive(names, comment=b'', xzp=False, pad=0, fields=False):
    local = bytearray()
    central = bytearray()
    for name in names:
        offset = len(local)
        local += struct.pack('<I5H3I2H', 0x04034b50, 20, 0, 0, 0, 0, 0, 0, 0, len(name), 0) + name
        extra, note = (b'EXTRA123', b'comment') if fields else (b'', b'')
        central += struct.pack('<I6H3I5H2I', 0x02014b50, 20, 20, 0, 0, 0, 0, 0, 0, 0,
                               len(name), len(extra), len(note), 0, 0, 0, offset) + name
        if not xzp:
            central += extra + note
    central += b'\0' * pad
    end = struct.pack('<I4H2IH', 0x06054b50, 0, 0, len(names), len(names), len(central), len(local), len(comment))
    return bytes(local + central + end + comment)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--dll-dir', type=Path, action='append', default=[])
    options = parser.parse_args()
    folder = Path(tempfile.mkdtemp(prefix='source-hlight-pak-metadata-'))
    ordinary = archive([b'materials/control.txt'])
    cases = []
    def add(name, data, valid, found=False, args=()):
        cases.append((name, data, valid, found, args))
    add('empty_lump', b'', True)
    add('empty_zip', archive([]), True)
    add('ordinary', ordinary, True)
    add('orphan_asset', archive([b'maps/a.hlight']), True, True)
    add('case_insensitive', archive([b'MAPS/A.HLIGHT']), True, True)
    add('not_asset_extension', archive([b'maps/a.hlight.bak']), True)
    add('not_asset_directory', archive([b'other/a.hlight']), True)
    add('standard_extras', archive([b'materials/control.txt', b'maps/a.hlight'], fields=True), True, True)
    add('xzp2_omitted_extras', archive([b'materials/control.txt', b'maps/a.hlight'], b'XZP2', True, 32, True), True, True)
    add('aligned_directory_tail', archive([b'materials/control.txt'], b'XZP2', True, 32), True)
    add('maximum_comment', archive([b'maps/a.hlight'], b'x'*65535), True, True)
    add('maximum_asset_path', archive([b'maps/'+b'a'*243+b'.hlight']), True, True)
    add('nul_hidden_asset', archive([b'maps/a.hlight\0suffix']), False)
    add('long_nul_hidden_asset', archive([b'maps/a.hlight\0'+b'x'*300]), False)
    add('short_lump', b'x'*21, False)
    add('compressed_outer_lump', ordinary, False, args=('64', str(len(ordinary)), '1'))
    add('offset_overflow', ordinary, False, args=('0xfffffff0', str(len(ordinary))))
    add('read_past_input', ordinary, False, args=('64', str(len(ordinary)+1)))
    end = ordinary.rfind(b'PK\x05\x06')
    central = struct.unpack_from('<I', ordinary, end+16)[0]
    for name, offset, fmt, value in [
        ('bad_end_signature', end, '<I', 0),
        ('bad_end_comment_length', end+20, '<H', 1),
        ('multiple_disks', end+4, '<H', 1),
        ('directory_other_disk', end+6, '<H', 1),
        ('entry_count_disagreement', end+8, '<H', 2),
        ('directory_past_end', end+16, '<I', 0xfffffff0),
        ('directory_size_past_end', end+12, '<I', 0xfffffff0),
        ('short_central_header', end+12, '<I', 45),
        ('bad_entry_signature', central, '<I', 0),
        ('entry_other_disk', central+34, '<H', 1),
        ('name_out_of_bounds', central+28, '<H', 65535),
        ('extra_out_of_bounds', central+30, '<H', 65535),
        ('comment_out_of_bounds', central+32, '<H', 65535),
    ]:
        data = bytearray(ordinary)
        struct.pack_into(fmt, data, offset, value)
        add(name, bytes(data), False)
    data = bytearray(ordinary)
    struct.pack_into('<2H', data, end+8, 2, 2)
    add('missing_second_entry', bytes(data), False)
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(p) for p in [ROOT/'../game/bin/x64', *options.dll_dir]) + os.pathsep + env['PATH']
    results = []
    for name, data, valid, found, args in cases:
        path = folder/(name+'.bin')
        with path.open('xb') as stream:
            stream.write(b'\0'*64+data)
        result = subprocess.run([str(options.probe.resolve()), str(path), *args], env=env, capture_output=True, text=True)
        match = re.search(r'valid=(\d) found=(\d) reads=(\d+) bytes=(\d+)', result.stdout)
        observed = (bool(int(match[1])), bool(int(match[2]))) if match else None
        passed = result.returncode == 0 and observed is not None and observed[0] == valid and (not valid or observed[1] == found)
        row = dict(case=name, expected=[valid, found], observed=observed, passed=passed, exit_code=result.returncode, stdout=result.stdout.strip(), stderr=result.stderr.strip())
        results.append(row)
        print(json.dumps(row))
    report = dict(scope='Production FindPakAsset metadata/orphan admission only; no asset inflation, no malformed native map loads.', folder=str(folder), cases=results, passed=all(row['passed'] for row in results))
    (folder/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(passed=report['passed'], cases=len(results), report=str(folder/'report.json'))))
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
