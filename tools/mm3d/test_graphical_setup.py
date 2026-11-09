import tempfile
import hashlib
import json
import unittest
from pathlib import Path
from unittest.mock import patch
from graphical_setup import prepare
import graphical_setup
from setup_game_folder import setup, supported_roms


class GraphicalSetupTests(unittest.TestCase):
    def test_existing_saves_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'user').mkdir();save=root/'user/save';save.write_bytes(b'keep')
            with self.assertRaisesRegex(ValueError,'already has user data'):
                prepare(root,root/'rom',root/'patch',root/'header',root/'hud')
            self.assertEqual(save.read_bytes(),b'keep')

    def test_bundled_inputs_are_verified_and_staged(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);resources=root/'resources';resources.mkdir()
            bundled=resources/'project-restoration';bundled.mkdir()
            hud=bundled/'romfs/layout';hud.mkdir(parents=True);rom=root/'game.3ds';rom.write_bytes(b'game')
            files=[]
            for name in ('code.bps','exheader.bin','Joker.Main.New.gar','Joker.Main.Play.gar','Joker.mfpj'):
                data=name.encode();path=(bundled if name in ('code.bps','exheader.bin') else hud)/name;path.write_bytes(data)
                files.append({'input_root':'restoration','input':name if path.parent==bundled else 'romfs/layout/'+name,
                              'input_sha256':hashlib.sha256(data).hexdigest()})
            (resources/'setup-fingerprints.json').write_text(json.dumps({'files':files}))
            def check_stage(app,stage,game,copy_game=True):
                self.assertEqual(app,root);self.assertEqual(game,rom)
                for entry in files:self.assertEqual((stage/entry['input']).read_bytes(),Path(entry['input']).name.encode())
            with patch.object(graphical_setup,'RESOURCES',resources),patch('graphical_setup.validate'),patch('graphical_setup.setup',side_effect=check_stage) as setup:
                prepare(root,rom);setup.assert_called_once()
                self.assertFalse(list(root.glob('.mm3d-inputs-*')))
                layout=hud/'Joker.Main.New.gar';original=layout.read_bytes();layout.write_bytes(b'corrupt')
                with self.assertRaisesRegex(ValueError,'not the supported version'):prepare(root,rom)
                layout.unlink()
                with self.assertRaisesRegex(ValueError,'must contain exactly one'):prepare(root,rom)
                layout.write_bytes(original)
                (bundled/'code.bps').write_bytes(b'wrong version')
                with self.assertRaisesRegex(ValueError,'not the supported version'):prepare(root,rom)
                (bundled/'code.bps').unlink()
                with self.assertRaisesRegex(ValueError,'missing'):prepare(root,rom)
                self.assertEqual(setup.call_count,1)

    def test_publish_failure_removes_only_new_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);rom=root/'original.cci';rom.write_bytes(b'original')
            with patch('setup_game_folder.validate',return_value=supported_roms()[0]), patch('setup_game_folder.extract',side_effect=lambda rom,out,entry=None:out.mkdir(parents=True)), patch('setup_game_folder.build'), patch.object(Path,'rename',side_effect=OSError('disk failure')):
                with self.assertRaisesRegex(OSError,'disk failure'):
                    setup(root,root,rom)
            self.assertEqual(rom.read_bytes(),b'original')
            self.assertFalse((root/'game/mm3d.cci').exists())
            self.assertFalse((root/'user').exists())
            self.assertFalse(list(root.glob('.mm3d-setup-*')))

    def test_in_place_setup_never_copies_or_moves_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);rom=root/'My Game.3ds';rom.write_bytes(b'keep')
            with patch('setup_game_folder.validate',return_value=supported_roms()[0]),patch('setup_game_folder.extract',side_effect=lambda rom,out,entry=None:out.mkdir(parents=True)),patch('setup_game_folder.build'):
                setup(root,root,rom,copy_game=False)
            self.assertEqual(rom.read_bytes(),b'keep')
            self.assertFalse((root/'game').exists())
            self.assertEqual(json.loads((root/'user/setup/ready.json').read_text())['resource_version'], 2)
            notebook=json.loads((root/'user/config/mm3d-local/notebook.json').read_text())
            self.assertEqual(notebook['source_archive'], 'setup/original/romfs/layout/Joker.Main.Play.SubMenu.gar')
            self.assertEqual(notebook['fade_source'], 'setup/original/romfs/layout/Joker.gar')
            self.assertFalse(list(root.glob('.mm3d-setup-*')))

    def test_existing_game_survives_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'game').mkdir();rom=root/'game/mm3d.cci';rom.write_bytes(b'keep')
            with patch('setup_game_folder.validate'), patch('setup_game_folder.extract',side_effect=ValueError('invalid')):
                with self.assertRaises(ValueError):setup(root,root,rom)
            self.assertEqual(rom.read_bytes(),b'keep')


if __name__=='__main__':unittest.main()
