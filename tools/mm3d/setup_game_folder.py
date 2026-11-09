#!/usr/bin/env python3
"""Prepare a fresh portable user directory from game/mm3d.cci and supplied Restoration.
Never replaces an existing user directory. No keys, ROM or Restoration assets bundled.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import tempfile
from probe_rom import probe
from build_release_assets import build, RESOURCES
from release_setup import write
from upgrade_game_resources import RESOURCE_VERSION
import layout_migration


KEYS = ('program_id', 'remaster_version', 'main_partition_bytes', 'main_partition_sha256')


def supported_roms():
    return json.loads((RESOURCES / 'supported-rom.json').read_text())['roms']


def find_supported(sha256):
    for entry in supported_roms():
        if entry['main_partition_sha256'] == sha256:
            return entry
    return None


def validate(rom):
    """Return the supported-ROM entry matching this dump (USA or Europe Rev 1)."""
    actual = probe(rom)
    for entry in supported_roms():
        if all(actual[key] == entry[key] for key in KEYS):
            return entry
    raise ValueError('Unsupported ROM revision/content; expected a verified decrypted USA or Europe Rev 1 dump')


def extract(rom, output, entry=None):
    # NCSD -> main NCCH -> standard IVFC level 3. Only required layout and message files are emitted.
    manifest = json.loads((RESOURCES / 'setup-fingerprints.json').read_text())
    wanted = {e['input'][6:] for e in manifest['files'] if e['input_root'] == 'original'}
    wanted.add('layout/Joker.Main.Play.gar')
    if entry is None:
        entry = validate(rom)
    if entry['message_dir']:
        wanted.update('message/' + entry['message_dir'] + '/' + name + '.gmsg' for name in entry['messages'])
    with rom.open('rb') as stream:
        size = rom.stat().st_size
        def read(at, count):
            if at < 0 or count < 0 or at + count > size:
                raise ValueError('ROM bounds error')
            stream.seek(at); data = stream.read(count)
            if len(data) != count: raise ValueError('Short ROM read')
            return data
        base = struct.unpack('<I', read(0x120,4))[0] * 512
        rb, rn = struct.unpack('<II',read(base+0x1b0,8))
        start, length = base + rb*512 + 0x1000, rn*512-0x1000
        if length <= 0: raise ValueError('Invalid RomFS')
        def rr(at, count):
            if at < 0 or at+count > length: raise ValueError('RomFS bounds error')
            return read(start+at,count)
        h = struct.unpack('<10I',rr(0,40))
        if h[0] != 40: raise ValueError('Unsupported RomFS layout')
        dt, ft, payload = h[3], h[7], h[9]
        visited = set(); found = set()
        def directory(offset, parent, depth=0):
            if depth > 32 or offset in visited: raise ValueError('Cyclic/deep RomFS')
            visited.add(offset)
            _, sibling, child, file, _, n = struct.unpack('<6I',rr(dt+offset,24))
            if n > 1024: raise ValueError('Invalid directory name')
            name = rr(dt+offset+24,n).decode('utf-16le')
            path = parent + (name+'/' if name else '')
            seen_files = set()
            while file != 0xffffffff:
                if file in seen_files: raise ValueError('Cyclic file list')
                seen_files.add(file)
                _, nxt, data_at, data_len, _, n = struct.unpack('<IIQQII',rr(ft+file,32))
                if n > 1024: raise ValueError('Invalid file name')
                filename = path+rr(ft+file+32,n).decode('utf-16le')
                if filename in wanted:
                    if filename in found or data_len > 16*1024*1024: raise ValueError('Invalid asset')
                    write(output,'romfs/'+filename,rr(payload+data_at,data_len));found.add(filename)
                file = nxt
            while child != 0xffffffff:
                child = directory(child,path,depth+1)
            return sibling
        directory(0,'')
        if found != wanted: raise ValueError('Required game layouts missing')


def setup(root, restoration, selected_rom=None, copy_game=True):
    rom = selected_rom or root/'game/mm3d.cci'
    if not copy_game and rom.resolve().parent != root.resolve() and rom.resolve() != (root/'game/mm3d.cci').resolve():
        raise ValueError('Place the game beside the application before starting setup.')
    layout_policy = layout_migration.policy(RESOURCES)
    entry = validate(rom)
    title = entry['program_id']
    output = root/'user'
    if output.exists(): raise ValueError('user directory already exists; refusing to replace settings or saves')
    destination = root/'game/mm3d.cci'
    if copy_game and destination.exists() and destination.resolve() != rom.resolve():
        validate(destination)  # Never replace an existing game file.
    stage = Path(tempfile.mkdtemp(prefix='.mm3d-setup-',dir=root))
    installed_rom = False
    try:
        original = stage/'setup/original'
        extract(rom,original,entry)
        mod_root=stage/('load/mods/'+title)
        build(original,restoration,mod_root,with_messages=bool(entry['message_dir']))
        write(stage, layout_migration.BASELINE, layout_migration.baseline(layout_policy))
        (stage/('load/textures/'+title)).mkdir(parents=True)
        shutil.copytree(RESOURCES/'defaults',stage/'config/mm3d-local')
        write(stage, 'config/qt-config.ini', b'[Layout]\nlayout_option=1\nlayout_option\\default=false\n[Renderer]\nuse_hw_shader=false\nuse_hw_shader\\default=false\nasync_shader_compilation=false\nasync_shader_compilation\\default=false\nuse_vsync=true\nuse_vsync\\default=true\nresolution_factor=4\nresolution_factor\\default=false\ngraphics_api=2\ngraphics_api\\default=false\n')
        p=stage/'config/mm3d-local/notebook.json'; obj=json.loads(p.read_text())
        obj['source_archive']='setup/original/romfs/layout/Joker.Main.Play.SubMenu.gar'
        obj['fade_source']='setup/original/romfs/layout/Joker.gar'
        p.write_text(json.dumps(obj,indent=2)+'\n')
        (stage/'setup/ready.json').write_text(json.dumps({'version':1,'resource_version':RESOURCE_VERSION,'layout_version':layout_migration.VERSION,'rom_sha256':entry['main_partition_sha256']})+'\n')
        if copy_game and not destination.exists():
            shutil.copyfile(rom, stage/'game-copy.cci')
            validate(stage/'game-copy.cci')
            destination.parent.mkdir(parents=True, exist_ok=True)
            # Exclusive creation prevents an overlapping setup from replacing a ROM.
            with destination.open('xb') as target:
                installed_rom = True
                with (stage/'game-copy.cci').open('rb') as source:
                    shutil.copyfileobj(source, target)
            (stage/'game-copy.cci').unlink()
        stage.rename(output)
    except BaseException:
        if installed_rom:
            destination.unlink(missing_ok=True)
        raise
    finally:
        if stage.exists(): shutil.rmtree(stage)
    return output


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--restoration',type=Path,required=True)
    a=p.parse_args()
    try: print('Prepared:',setup(a.root,a.restoration))
    except (ValueError,OSError) as error: p.exit(1,str(error)+'\n')
