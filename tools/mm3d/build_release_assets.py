#!/usr/bin/env python3
"""Reconstruct the frozen release baseline from user-supplied assets, without private recipes.
No game data is embedded. Inputs and every output must match the public fingerprint manifest.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import tempfile
from release_setup import digest, safe, write
from restore_action_flips import patch as restore_actions
from stop_map_slides import patch as stop_slides
from classic_owl_text import prepare as prepare_owl
from classic_localized_text import build as prepare_localized

RESOURCES = Path(__file__).resolve().parents[2] / 'resources/mm3d'


def floats(data, values):
    result = bytearray(data)
    for offset, value in values.items():
        struct.pack_into('<f', result, offset, value)
    return bytes(result)


def transform(name, source, original_play):
    if name == 'Joker.Main.Play.gar':
        return restore_actions(original_play, source)
    if name == 'Joker.Main.Play.SubMenu.gar':
        result = bytearray(stop_slides(source)[0])
        # UpQuestlist null_plate07/08 object flags: enable default visibility
        # (0x0e -> 0x0f), retaining native animation and all other flags.
        for offset in (0x9eec, 0x9f74):
            if struct.unpack_from('<H', result, offset)[0] != 14:
                raise ValueError('Unexpected notebook row visibility flags')
            struct.pack_into('<H', result, offset, 15)
        return bytes(result)
    if name == 'Joker.Main.New.gar':
        # Frozen installed HUD poses, preserving the editor's C++ float arithmetic.
        margin = struct.unpack('<f', struct.pack('<f', 240 * 16 / 9))[0]
        margin = (margin - 400) / 2
        values = {}
        for offset, x, shift in ((0x1388,0,margin),(0x13cc,373,margin),
            (0x1498,20,-margin),(0x1c4c,8,-margin),(0x1e28,8,-margin),
            (0x2994,0,-margin),(0x6ee4,0,margin)):
            values[offset] = x + shift
        values.update({0x149c:53.6, 0x2f70:-23.4, 0x2f7c:2,
                       0x6ae8:15-margin, 0x6aec:49.6})
        return floats(source, values)
    if name == 'Joker.Main.gar':
        # Installed SubMenuTitle poses predate currently saved header slider values.
        # Do not reapply defaults: that would change the captured appearance.
        values = {}
        for base, x, y in ((0xb368,-13.209999084472656,.5199999809265137),
                           (0xba0c,-13.209999084472656,.5199999809265137),
                           (0xba94,-12.949999809265137,.5199999809265137),
                           (0xbb1c,66.54999542236328,-.570000171661377)):
            values.update({base:x,base+4:y,base+12:.87,base+16:.87})
        values.update({0xc3e4:-40,0xc3f0:480,0xc414:-50,0xc420:500})
        return floats(source, values)
    if name == 'Joker.Main.gar.mm3d-header-source':
        # Historical Telop pose present in header source baseline only.
        return floats(source, {0xd03c:10,0xd054:1.3,0xd058:1.3,
                               0xd094:-40/3,0xd0a0:400+80/3,0xd0a4:24 * struct.unpack("<f", struct.pack("<f", 1.3))[0]})
    if name == 'Joker.gar':
        return floats(source, {0x590:400+80/3})
    return source  # Required clean baselines and unchanged Restoration inputs.


def build(original, restoration, output, with_messages=True):
    manifest = json.loads((RESOURCES / 'setup-fingerprints.json').read_text())
    roots = {'original': original, 'restoration': restoration}
    if output.exists():
        raise ValueError('Output exists; refusing to overwrite')
    # Validate every input before creating output. Action tracks come from original ROM too.
    sources = {}
    for entry in manifest['files']:
        data = safe(roots[entry['input_root']], entry['input']).read_bytes()
        if digest(data) != entry['input_sha256']:
            raise ValueError('Unsupported input: ' + entry['input'])
        sources[entry['output']] = data
    play = (original / 'romfs/layout/Joker.Main.Play.gar').read_bytes()
    if digest(play) != manifest['original_action_sha256']:
        raise ValueError('Unsupported original action archive')
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.mm3d-assets-', dir=output.parent))
    try:
        for entry in manifest['files']:
            data = transform(Path(entry['output']).name, sources[entry['output']], play)
            if len(data) != entry['size'] or digest(data) != entry['output_sha256']:
                raise ValueError('Reconstruction mismatch: ' + entry['output'])
            write(stage, entry['output'], data)
        # Expand native message storage from the user's verified game. Runtime
        # hooks select classic wording only when the corresponding option is on.
        # Classic dialogue storage is USA-only; Europe keeps its native messages.
        if with_messages:
            message_source = original / 'romfs/message/us'
            english, _, _ = prepare_owl((message_source / 'use.gmsg').read_bytes())
            localized, _ = prepare_localized(message_source)
            for name, data in {'use.gmsg': english, **localized}.items():
                write(stage, 'romfs/message/us/' + name, data)
        stage.rename(output)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return len(manifest['files'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'restoration', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    print('Verified assets:', build(args.original, args.restoration, args.output))
