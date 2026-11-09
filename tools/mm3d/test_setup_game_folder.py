import configparser
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from setup_game_folder import setup, validate, extract


class GameFolderTests(unittest.TestCase):
    def test_setup_creates_empty_texture_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);rom=root/'original.cci';rom.write_bytes(b'original')
            from setup_game_folder import find_supported
            usa=find_supported('9c756279e3ec00bb9009f92ca2e7b9ec8b1fcdfcd9de057658b209caa24323da')
            with patch('setup_game_folder.validate',return_value=usa), patch('setup_game_folder.extract',
                    side_effect=lambda rom,out,entry=None:out.mkdir(parents=True)), patch('setup_game_folder.build'):
                setup(root,root,rom)
            from setup_game_folder import RESOURCES
            defaults=json.loads((RESOURCES/'defaults/presentation.json').read_text())
            actual=json.loads((root/'user/config/mm3d-local/presentation.json').read_text())
            for key,value in defaults.items():
                if key.startswith(('player_hud_', 'dungeon_map_', 'notebook_')) or key=='minimap_opacity':
                    self.assertEqual(actual[key],value,key)
            self.assertEqual(actual['player_hud_layout_version'],2)
            self.assertTrue(actual['player_hud_mirror_item_buttons'])
            self.assertEqual(actual['player_hud_scale'],0)
            textures=root/'user/load/textures/0004000000125500'
            self.assertTrue(textures.is_dir())
            self.assertEqual(list(textures.iterdir()),[])
            self.assertEqual(rom.read_bytes(),b'original')
            config = configparser.ConfigParser()
            config.read(root/'user/config/qt-config.ini')
            for key in ('use_hw_shader', 'async_shader_compilation'):
                self.assertFalse(config.getboolean('Renderer', key))
                self.assertFalse(config.getboolean('Renderer', key + '\\default'))
            self.assertEqual(config.getint('Renderer', 'graphics_api'), 2)


    def test_missing_rom(self):
        with tempfile.TemporaryDirectory() as t:
            with self.assertRaises(OSError): setup(Path(t),Path(t))
            self.assertFalse((Path(t)/'user').exists())

    def test_existing_user_preserved(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);(root/'user').mkdir();save=root/'user/save';save.write_bytes(b'keep')
            with patch('setup_game_folder.validate'), self.assertRaises(ValueError): setup(root,root)
            self.assertEqual(save.read_bytes(),b'keep')

    def test_wrong_revision(self):
        with patch('setup_game_folder.probe',return_value={'program_id':'wrong'}):
            with self.assertRaises(ValueError):validate(Path('mm3d.cci'))

    def test_supported_regions(self):
        from setup_game_folder import find_supported
        self.assertEqual(find_supported('9c756279e3ec00bb9009f92ca2e7b9ec8b1fcdfcd9de057658b209caa24323da')['program_id'],'0004000000125500')
        eur=find_supported('a26108ef34b49df421cca20eda1b4f38f596ebef9ac6b9c70a64245be055a722')
        self.assertEqual(eur['program_id'],'0004000000125600')
        self.assertFalse(eur['message_dir'])
        self.assertIsNone(find_supported('00'))

    def test_europe_setup_uses_europe_title_without_us_messages(self):
        from setup_game_folder import find_supported
        eur=find_supported('a26108ef34b49df421cca20eda1b4f38f596ebef9ac6b9c70a64245be055a722')
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);rom=root/'eur.3ds';rom.write_bytes(b'original')
            with patch('setup_game_folder.validate',return_value=eur), patch('setup_game_folder.extract',
                    side_effect=lambda rom,out,entry=None:out.mkdir(parents=True)), \
                    patch('setup_game_folder.build') as build:
                setup(root,root,rom)
            self.assertTrue((root/'user/load/textures/0004000000125600').is_dir())
            self.assertFalse((root/'user/load/textures/0004000000125500').exists())
            self.assertFalse(build.call_args.kwargs['with_messages'])
            ready=json.loads((root/'user/setup/ready.json').read_text())
            self.assertEqual(ready['rom_sha256'],eur['main_partition_sha256'])

    def test_truncated_extraction(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);rom=root/'mm3d.cci';rom.write_bytes(b'NCSD')
            with self.assertRaises(ValueError):extract(rom,root/'output')
            self.assertFalse((root/'output').exists())


if __name__=='__main__':unittest.main()
