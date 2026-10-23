import contextlib
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import upgrade_game_resources as upgrade
import setup_game_folder
import build_release_assets


class ResourceUpgradeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.user = self.root/'user'
        self.rom = self.root/'game.3ds'
        self.rom.write_bytes(b'game')
        self.rom_hash = json.loads((setup_game_folder.RESOURCES/'supported-rom.json').read_text())['main_partition_sha256']
        self.put(upgrade.READY, json.dumps({'version': 1, 'rom_sha256': self.rom_hash, 'extra': 'keep'}).encode())
        # Realistic user trees: every byte must survive, including save states,
        # classic saves, customized HUD/layout files and texture packs.
        self.keep = {
            'sdmc/Nintendo 3DS/profile/title/save.bin': b'precious save',
            'nand/system.dat': b'nand', 'states/test.cst': b'state',
            'config/qt-config.ini': b'[Renderer]\nresolution_factor=7\n',
            'config/mm3d-local/presentation.json': json.dumps(dict(json.loads((setup_game_folder.RESOURCES/'defaults/presentation.json').read_text()), song_list_x=72.5)).encode(),
            'load/textures/'+upgrade.TITLE+'/custom.png': b'texture',
            'load/mods/'+upgrade.TITLE+'/romfs/layout/Joker.Main.New.gar': b'custom layout',
            'classic-save-test.bin': b'cycle/owl data',
        }
        for path, data in self.keep.items(): self.put(path, data)
        self.old = {name: b'original '+name.encode() for name in upgrade.MESSAGE_NAMES}
        self.new = {name: b'expanded '+name.encode() for name in upgrade.MESSAGE_NAMES}

    def put(self, relative, data):
        p = self.user/relative
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        return p

    def snapshot(self):
        return {p.relative_to(self.user).as_posix(): p.read_bytes()
                for p in self.user.rglob('*') if p.is_file() and not p.is_symlink()}

    def assert_kept(self):
        for path, data in self.keep.items():
            self.assertEqual((self.user/path).read_bytes(), data, path)
        self.assertEqual(self.rom.read_bytes(), b'game')

    @contextlib.contextmanager
    def builders(self):
        def extract(rom, output):
            for name, data in self.old.items():
                p = output/'romfs/message/us'/name
                p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
        def build(original, restoration, output):
            for name, data in self.new.items():
                p = output/'romfs/message/us'/name
                p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)
        with patch.object(upgrade, 'MESSAGE_HASHES', {n: upgrade.digest(d) for n,d in self.new.items()}), \
             patch.object(setup_game_folder, 'validate') as validate, \
             patch.object(setup_game_folder, 'extract', side_effect=extract) as extract_mock, \
             patch.object(build_release_assets, 'build', side_effect=build):
            yield validate, extract_mock

    def run_upgrade(self):
        upgrade.upgrade(self.root, self.rom, self.root/'restoration')

    def test_upgrade_preserves_user_data_and_is_idempotent(self):
        existing = upgrade.MESSAGE_PATHS[0]
        self.put(existing, self.old['use.gmsg'])
        with self.builders() as (_, extract):
            self.run_upgrade()
            self.assertEqual(extract.call_count, 1)
            before = self.snapshot()
            self.run_upgrade()
            self.assertEqual(extract.call_count, 1)
            self.assertEqual(before, self.snapshot())
        self.assert_kept()
        ready = json.loads((self.user/upgrade.READY).read_bytes())
        self.assertEqual(ready['resource_version'], 2)
        self.assertEqual(ready['extra'], 'keep')
        for name, relative in zip(upgrade.MESSAGE_NAMES, upgrade.MESSAGE_PATHS):
            self.assertEqual((self.user/relative).read_bytes(), self.new[name])
        backup, = (self.user/'setup').glob('resource-upgrade-backup-*')
        self.assertEqual((backup/'backup'/existing).read_bytes(), self.old['use.gmsg'])
        self.assertNotIn('resource_version', json.loads((backup/'backup'/upgrade.READY).read_bytes()))

    def test_retired_sky_never_triggers_extraction(self):
        with self.builders() as (_, extract):
            self.run_upgrade()
            extract.reset_mock()
            ready = json.loads((self.user/upgrade.READY).read_bytes())
            ready['sky_version'] = 1
            self.put(upgrade.READY, json.dumps(ready).encode())
            for data in (None, b'corrupt obsolete sky'):
                if data is not None:
                    self.put(upgrade.LEGACY_SKY, data)
                before = self.snapshot()
                self.run_upgrade()
                extract.assert_not_called()
                self.assertEqual(before, self.snapshot())
        self.assert_kept()

    def test_upgrade_preserves_legacy_notebook_config(self):
        config = {'source_archive': '/old/user/setup/original/romfs/layout/Joker.Main.Play.SubMenu.gar',
                  'fade_source': 'C:\\old\\user\\setup\\original\\romfs\\layout\\Joker.gar',
                  'scale': 1.23, 'other': 'preserve'}
        for name in ('Joker.Main.Play.SubMenu.gar', 'Joker.gar'):
            self.put('setup/original/romfs/layout/'+name, b'original layout')
        self.put(upgrade.NOTEBOOK, json.dumps(config).encode())
        with self.builders(): self.run_upgrade()
        result = json.loads((self.user/upgrade.NOTEBOOK).read_bytes())
        self.assertEqual(result['scale'], 1.23)
        self.assertEqual(result['other'], 'preserve')
        self.assertEqual(result, config)  # Runtime resolves old paths; updater need not edit them.
        self.assert_kept()

    def test_custom_dialogue_conflict_is_untouched(self):
        self.put(upgrade.MESSAGE_PATHS[1], b'custom translation')
        before = self.snapshot()
        with self.builders(), self.assertRaisesRegex(ValueError, 'Custom dialogue'):
            self.run_upgrade()
        after = self.snapshot(); after.pop('setup/resource-upgrade.lock', None)
        self.assertEqual(before, after)
        self.assertFalse((self.user/upgrade.PENDING).exists())

    def test_failed_build_changes_no_live_files(self):
        before = self.snapshot()
        with self.builders(), patch.object(build_release_assets, 'build', side_effect=OSError('disk full')), \
             self.assertRaisesRegex(OSError, 'disk full'):
            self.run_upgrade()
        after = self.snapshot(); after.pop('setup/resource-upgrade.lock', None)
        self.assertEqual(before, after)

    def test_interrupt_after_each_live_write_resumes_before_ready(self):
        real_write = upgrade.atomic_write
        # Interrupt before each of the three resource writes and the ready marker.
        for stop in range(4):
            with self.subTest(stop=stop):
                for relative in upgrade.MESSAGE_PATHS:
                    (self.user/relative).unlink(missing_ok=True)
                self.put(upgrade.READY, json.dumps({'version': 1, 'rom_sha256': self.rom_hash}).encode())
                count = 0
                def interrupted(path, data):
                    nonlocal count
                    if path in [self.user/p for p in (*upgrade.MESSAGE_PATHS, upgrade.READY)]:
                        count += 1
                        if count == stop+1: raise KeyboardInterrupt('simulated process stop')
                    real_write(path, data)
                with self.builders(), patch.object(upgrade, 'atomic_write', side_effect=interrupted), \
                     self.assertRaises(KeyboardInterrupt): self.run_upgrade()
                self.assertTrue((self.user/upgrade.PENDING).exists())
                self.assertNotIn('resource_version', json.loads((self.user/upgrade.READY).read_bytes()))
                self.assert_kept()
                with self.builders() as (_, extract):
                    self.run_upgrade()
                    extract.assert_not_called()
                self.assertFalse((self.user/upgrade.PENDING).exists())
                self.assertEqual(json.loads((self.user/upgrade.READY).read_bytes())['resource_version'], 2)
                self.assert_kept()

    def test_ready_written_but_cleanup_interrupted(self):
        real_rename = Path.rename
        def interrupted(path, target):
            if path == self.user/upgrade.PENDING: raise OSError('interrupted final rename')
            return real_rename(path, target)
        with self.builders(), patch.object(Path, 'rename', interrupted), self.assertRaises(OSError):
            self.run_upgrade()
        self.assertTrue((self.user/upgrade.PENDING).exists())
        with self.builders(): self.run_upgrade()
        self.assertFalse((self.user/upgrade.PENDING).exists())
        self.assert_kept()

    def test_corrupt_stage_and_user_changes_block_resume(self):
        with self.builders(), patch.object(upgrade, 'resume', side_effect=[None, OSError('stop')]), self.assertRaises(OSError):
            self.run_upgrade()
        stage = self.user/upgrade.PENDING/'new'/upgrade.MESSAGE_PATHS[0]
        original = stage.read_bytes(); stage.write_bytes(b'corrupt')
        with self.builders(), self.assertRaisesRegex(ValueError, 'checksum mismatch'): self.run_upgrade()
        stage.write_bytes(original)
        self.put(upgrade.MESSAGE_PATHS[0], b'new custom edit')
        with self.builders(), self.assertRaisesRegex(ValueError, 'File changed'): self.run_upgrade()
        self.assert_kept()

    def test_symlink_targets_rejected(self):
        outside = self.root/'outside'; outside.mkdir()
        target = self.user/'load/mods'/upgrade.TITLE/'romfs/message'
        try: target.symlink_to(outside, target_is_directory=True)
        except OSError: self.skipTest('Symlinks unavailable')
        with self.builders(), self.assertRaisesRegex(ValueError, 'symbolic link'): self.run_upgrade()
        self.assertEqual(list(outside.iterdir()), [])
        self.assert_kept()

    def test_unknown_version_and_wrong_game_rejected(self):
        for state in ({'version': 1, 'rom_sha256': self.rom_hash, 'resource_version': 99},
                      {'version': 1, 'rom_sha256': 'wrong'}, {'version': 99, 'rom_sha256': self.rom_hash}):
            self.put(upgrade.READY, json.dumps(state).encode())
            with self.builders() as (_, extract), self.assertRaises(ValueError): self.run_upgrade()
            extract.assert_not_called()
            self.assert_kept()

    def test_missing_current_resource_is_repaired(self):
        with self.builders():
            self.run_upgrade()
            (self.user/upgrade.MESSAGE_PATHS[1]).unlink()
            self.run_upgrade()
        self.assertEqual((self.user/upgrade.MESSAGE_PATHS[1]).read_bytes(), self.new['usf.gmsg'])
        self.assert_kept()

    def test_unknown_current_resource_is_not_overwritten(self):
        with self.builders():
            self.run_upgrade()
            self.put(upgrade.MESSAGE_PATHS[0], b'custom edit')
            with self.assertRaisesRegex(ValueError, 'modified or damaged'): self.run_upgrade()
        self.assertEqual((self.user/upgrade.MESSAGE_PATHS[0]).read_bytes(), b'custom edit')
        self.assert_kept()

    def test_generated_checksum_mismatch_is_not_installed(self):
        with self.builders(), patch.object(upgrade, 'MESSAGE_HASHES', dict.fromkeys(upgrade.MESSAGE_NAMES, 'bad')), \
             self.assertRaisesRegex(ValueError, 'Generated dialogue checksum'): self.run_upgrade()
        self.assertFalse((self.user/upgrade.PENDING).exists())
        self.assert_kept()

    def test_failed_backup_does_not_change_live_files(self):
        self.put(upgrade.MESSAGE_PATHS[0], self.old['use.gmsg'])
        before = self.snapshot()
        real_write = upgrade.atomic_write
        def disk_full(path, data):
            if 'backup' in path.parts: raise OSError('disk full while backing up')
            real_write(path, data)
        with self.builders(), patch.object(upgrade, 'atomic_write', side_effect=disk_full), self.assertRaises(OSError):
            self.run_upgrade()
        after = self.snapshot(); after.pop('setup/resource-upgrade.lock', None)
        self.assertEqual(before, after)
        self.assertFalse((self.user/upgrade.PENDING).exists())

    def test_atomic_replace_failure_retains_previous_file(self):
        target = self.put('atomic-test', b'old')
        with patch.object(upgrade.os, 'replace', side_effect=OSError('disk error')), self.assertRaises(OSError):
            upgrade.atomic_write(target, b'new')
        self.assertEqual(target.read_bytes(), b'old')

    def test_missing_metadata_rejected(self):
        (self.user/upgrade.READY).unlink()
        with self.builders(), self.assertRaises(OSError): self.run_upgrade()
        self.assert_kept()

    @unittest.skipIf(__import__('os').name == 'nt', 'POSIX lock contention test')
    def test_second_upgrader_cannot_write(self):
        with upgrade.locked(self.user), self.builders(), self.assertRaisesRegex(ValueError, 'Another Dekui'):
            self.run_upgrade()
        self.assert_kept()

    def test_deck_upgrade_and_backup_preserve_saves(self):
        fixture=Path(__file__).parent/'fixtures/beta2-upgraded-deck.json'
        raw=fixture.read_bytes();self.put(upgrade.layout_migration.CONFIG,raw)
        self.keep.pop(upgrade.layout_migration.CONFIG)
        with self.builders(): self.run_upgrade()
        actual=json.loads((self.user/upgrade.layout_migration.CONFIG).read_bytes())
        self.assertAlmostEqual(actual['ocarina_prompt_x'],3.1,places=4)
        self.assertEqual(actual['damage_multiplier'],4)
        self.assertTrue(actual['player_hud_hide_minimap'])
        backups=list((self.user/'setup').glob('resource-upgrade-backup-*'))
        self.assertEqual((backups[0]/'backup'/upgrade.layout_migration.CONFIG).read_bytes(),raw)
        before=self.snapshot()
        with self.builders():self.run_upgrade()
        self.assertEqual(before,self.snapshot());self.assert_kept()

    def test_version_one_install_upgrades_photo_without_rebuilding_resources(self):
        # The photo defaults changed after layout version 1 had already shipped.
        with self.builders() as (_, extract):
            self.run_upgrade()
            extract.reset_mock()
            ready = json.loads((self.user/upgrade.READY).read_bytes())
            ready['layout_version'] = 1
            self.put(upgrade.READY, json.dumps(ready).encode())
            original = json.loads((self.user/upgrade.layout_migration.CONFIG).read_bytes())
            original.update(gear_6_x=0, gear_6_y=0, gear_6_scale=1)
            raw = json.dumps(original).encode()
            self.put(upgrade.layout_migration.CONFIG, raw)
            self.run_upgrade()
            actual = json.loads((self.user/upgrade.layout_migration.CONFIG).read_bytes())
            defaults = json.loads((setup_game_folder.RESOURCES/'defaults/presentation.json').read_text())
            expected = dict(original)
            for key in ('gear_6_x', 'gear_6_y', 'gear_6_scale'):
                expected[key] = defaults[key]
            self.assertEqual(actual, expected)
            self.assertEqual(extract.call_count, 0)
            self.assertEqual(json.loads((self.user/upgrade.READY).read_bytes())['layout_version'],
                             upgrade.layout_migration.VERSION)
            backups = (self.user/'setup').glob('resource-upgrade-backup-*')
            self.assertTrue(any((p/'backup'/upgrade.layout_migration.CONFIG).read_bytes() == raw
                                for p in backups if (p/'backup'/upgrade.layout_migration.CONFIG).exists()))
            before = self.snapshot()
            self.run_upgrade()
            self.assertEqual(before, self.snapshot())
        self.keep.pop(upgrade.layout_migration.CONFIG)
        self.assert_kept()

    def test_stock_profiles_retired_custom_profiles_preserved(self):
        policy=upgrade.layout_migration.policy(setup_game_folder.RESOURCES)
        stock='config/mm3d-local/hud/hearts.json';custom='config/mm3d-local/hud/loading.json'
        raw=b'old disabled profile';self.put(stock,raw);self.put(custom,b'custom enabled profile')
        policy['retired_profiles'][stock]=upgrade.digest(raw)
        with self.builders(),patch.object(upgrade.layout_migration,'policy',return_value=policy):self.run_upgrade()
        self.assertFalse((self.user/stock).exists())
        self.assertEqual((self.user/custom).read_bytes(),b'custom enabled profile')
        backup,=(self.user/'setup').glob('resource-upgrade-backup-*')
        self.assertEqual((backup/'backup'/stock).read_bytes(),raw)
        self.assert_kept()

    def test_existing_layout_revisions_upgrade_mask_and_hud_with_backup(self):
        defaults = json.loads((setup_game_folder.RESOURCES/'defaults/presentation.json').read_text())
        policy = upgrade.layout_migration.policy(setup_game_folder.RESOURCES)
        with self.builders() as (_, extract):
            self.run_upgrade()
            for revision in (1, 2):
                with self.subTest(revision=revision):
                    ready = json.loads((self.user/upgrade.READY).read_bytes())
                    ready['layout_version'] = revision
                    self.put(upgrade.READY, json.dumps(ready).encode())
                    original = dict(defaults, gear_6_x=0, gear_6_y=0, gear_6_scale=1,
                                    mask_present_6_x=0,
                                    mask_present_6_y=policy['previous']['dialogue_y'],
                                    mask_present_6_scale=policy['previous']['dialogue_scale'],
                                    magic_bar_height=policy['previous']['magic_bar_height'],
                                    minimap_opacity=policy['previous']['minimap_opacity'],
                                    mask_present_1_x=123, damage_multiplier=4,
                                    stick_click_controls=True, song_list_x=72.5)
                    raw = json.dumps(original).encode()
                    self.put(upgrade.layout_migration.CONFIG, raw)
                    extract.reset_mock()
                    self.run_upgrade()
                    actual = json.loads((self.user/upgrade.layout_migration.CONFIG).read_bytes())
                    expected = dict(original)
                    for key in ('gear_6_x', 'gear_6_y', 'gear_6_scale', 'mask_present_6_x',
                                'mask_present_6_y', 'mask_present_6_scale',
                                'magic_bar_height', 'minimap_opacity'):
                        expected[key] = defaults[key]
                    self.assertEqual(actual, expected)
                    self.assertEqual(extract.call_count, 0)
                    self.assertEqual(json.loads((self.user/upgrade.READY).read_bytes())['layout_version'],
                                     upgrade.layout_migration.VERSION)
                    backups = (self.user/'setup').glob('resource-upgrade-backup-*')
                    self.assertTrue(any((p/'backup'/upgrade.layout_migration.CONFIG).read_bytes() == raw
                                        for p in backups if (p/'backup'/upgrade.layout_migration.CONFIG).exists()))
                    before = self.snapshot()
                    self.run_upgrade()
                    self.assertEqual(before, self.snapshot())
        self.keep.pop(upgrade.layout_migration.CONFIG)
        self.assert_kept()

    def test_bad_config_or_future_migration_changes_nothing(self):
        for key in ('layout_version',):
            self.put(upgrade.READY,json.dumps({'version':1,'rom_sha256':self.rom_hash,key:99}).encode())
            with self.builders(),self.assertRaisesRegex(ValueError,'Unsupported'):self.run_upgrade()
            self.assert_kept()
        self.put(upgrade.READY,json.dumps({'version':1,'rom_sha256':self.rom_hash}).encode())
        self.put(upgrade.layout_migration.CONFIG,b'{bad')
        before=self.snapshot()
        with self.builders(),self.assertRaises(ValueError):self.run_upgrade()
        self.assertEqual(before,self.snapshot())

    def test_resume_retirement_and_new_files_after_each_interruption(self):
        # Recover journals from older releases, including their retired sky write.
        # Include both deletion and writes; all originals have durable backups.
        stock='config/mm3d-local/hud/hearts.json'
        real_write=upgrade.atomic_write;real_unlink=Path.unlink
        for stop in range(4):
            self.put(stock,b'legacy');self.put(upgrade.layout_migration.CONFIG,b'old config')
            ready=b'{"version":1}';self.put(upgrade.READY,ready)
            (self.user/upgrade.LEGACY_SKY).unlink(missing_ok=True)
            changes={upgrade.layout_migration.CONFIG:b'new config',stock:None,upgrade.LEGACY_SKY:b'new sky',upgrade.READY:b'{"version":1,"layout_version":1}'}
            count=0
            def checkpoint(path):
                nonlocal count
                if path in [self.user/p for p in changes]:
                    count+=1
                    if count==stop+1:raise KeyboardInterrupt()
            def write(path,data):checkpoint(path);real_write(path,data)
            def unlink(path,*args,**kwargs):checkpoint(path);return real_unlink(path,*args,**kwargs)
            with patch.object(upgrade,'atomic_write',side_effect=write),patch.object(Path,'unlink',unlink),self.assertRaises(KeyboardInterrupt):upgrade.publish(self.user,changes)
            upgrade.resume(self.user)
            for name,data in changes.items():
                if data is None:self.assertFalse((self.user/name).exists())
                else:self.assertEqual((self.user/name).read_bytes(),data)
            self.assertFalse((self.user/upgrade.PENDING).exists())

    def test_no_nonlegacy_deletion_allowed(self):
        with self.assertRaisesRegex(ValueError,'non-legacy'):
            upgrade.publish(self.user,{upgrade.layout_migration.CONFIG:None,upgrade.READY:b'{}'})
        self.assert_kept()


if __name__ == '__main__': unittest.main()
