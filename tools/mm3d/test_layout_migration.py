import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import layout_migration as migration
from setup_game_folder import RESOURCES


class LayoutMigrationTests(unittest.TestCase):
    def setUp(self):
        self.policy=migration.policy(RESOURCES)

    def apply(self, obj, revision=0):
        raw,held=migration.migrate((json.dumps(obj)+'\n').encode(),self.policy,revision)
        return json.loads(raw),held

    def test_previous_defaults_upgrade_groups_preserve_hud(self):
        old=self.policy['previous'];new,held=self.apply(old)
        self.assertEqual(held,[])
        for k,v in self.policy['current'].items():
            if k in self.policy.get('fresh_install_only', []) and k not in old:
                self.assertNotIn(k, new)
                continue
            expected = old[k] if k in old and k in self.policy['preserve_existing'] else self.policy.get('existing_install_defaults', {}).get(k, v)
            self.assertEqual(new[k],expected,k)

    def test_reported_deck_case_preserves_all_nonlayout_preferences(self):
        old=json.loads((Path(__file__).parent/'fixtures/beta2-upgraded-deck.json').read_text())
        new,held=self.apply(old);self.assertEqual(held,[])
        prefixes=tuple(p for group in migration.GROUPS.values() for p in group)
        for k,v in old.items():
            shipped_fades = k == 'fullscreen_draws' and v == self.policy.get('previous_fullscreen_draws')
            self.assertEqual(new[k],self.policy['current'][k] if shipped_fades or ((k.startswith(prefixes) or any(k in g for g in self.policy['legacy_dungeon_widgets'].values())) and k in self.policy['current']) else v,k)
        again,held=self.apply(new);self.assertEqual(again,new)

    def test_custom_group_is_not_partially_repositioned(self):
        old=dict(self.policy['previous'],song_list_x=72.5,ocarina_prompt_x=-230,unknown_user_key='keep')
        new,held=self.apply(old);self.assertEqual(held,['songs'])
        for k,v in old.items():
            if k.startswith(migration.GROUPS['songs']) or k=='unknown_user_key':self.assertEqual(new[k],v)
        self.assertNotIn('song_banner_x',new)
        self.assertEqual(new['file_select_0_y'],self.policy['current']['file_select_0_y'])

    def test_current_customizations_not_reset(self):
        old=dict(self.policy['current'],song_panel_left_x=-50,file_select_1_x=123,item_present_1_x=77,gear_6_x=42,
                 mask_present_1_x=123,mask_present_6_x=42,magic_bar_height=.54,minimap_opacity=.54)
        new,held=self.apply(old);self.assertEqual(new,old)
        self.assertEqual(set(held),set(migration.GROUPS))

    def test_fresh_and_current_are_idempotent(self):
        obj=self.policy['current'];new,held=self.apply(obj)
        self.assertEqual(new,obj);self.assertEqual(held,[])

    def test_reject_corrupt_and_future_config(self):
        for raw in (b'{',b'[]',b'{"version":999}',b'{"version":true}',b'{"version":1,"song_list_x":NaN}'):
            with self.assertRaises(ValueError):migration.migrate(raw,self.policy)

    def test_release_gate_rejects_unreviewed_defaults_and_profiles(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);shutil.copytree(RESOURCES/'defaults',root/'defaults')
            shutil.copy2(RESOURCES/migration.RESOURCE,root/migration.RESOURCE)
            migration.policy(root)
            p=root/'defaults/presentation.json';raw=p.read_bytes()
            p.write_text(json.dumps(dict(self.policy['current'],song_list_x=999)))
            with self.assertRaises(ValueError):migration.policy(root)
            p.write_bytes(raw);(root/'defaults/rogue.json').write_text('{}')
            with self.assertRaises(ValueError):migration.policy(root)

    def test_every_changed_existing_default_has_explicit_policy(self):
        prefixes=tuple(p for group in migration.GROUPS.values() for p in group)
        for k,v in self.policy['previous'].items():
            if self.policy['current'].get(k)!=v:
                self.assertTrue(k.startswith(prefixes) or any(k in g for g in self.policy['legacy_dungeon_widgets'].values()) or k in self.policy['preserve_existing'] or (k == 'fullscreen_draws' and v == self.policy.get('previous_fullscreen_draws')),k)

    def test_shipped_fades_upgrade_but_custom_fades_remain(self):
        previous = self.policy['previous_fullscreen_draws']
        old = dict(self.policy['current'], fullscreen_draws=previous)
        new, _ = self.apply(old)
        self.assertEqual(new['fullscreen_draws'], self.policy['current']['fullscreen_draws'])
        self.assertGreater(len(new['fullscreen_draws']), len(previous))
        for custom in ([], previous[:-1], previous + [dict(previous[0], vertices=123)]):
            old['fullscreen_draws'] = custom
            new, _ = self.apply(old)
            self.assertEqual(new['fullscreen_draws'], custom)

    def test_mask_defaults_preserve_user_placement_and_gameplay(self):
        old = dict(self.policy['previous'], damage_multiplier=4, actor_draw_distance=2,
                   mask_present_1_x=123, mask_present_help_scale=.9)
        new, _ = self.apply(old)
        for key in ('damage_multiplier', 'actor_draw_distance', 'mask_present_1_x', 'mask_present_help_scale'):
            self.assertEqual(new[key], old[key])
        self.assertNotIn('mask_present_3_x', new)  # Keep customized mask layout together.
        self.assertEqual(new['mask_present_6_x'], self.policy['current']['mask_present_6_x'])
        self.assertEqual(self.policy['current']['damage_multiplier'], 1)
        self.assertEqual(self.policy['current']['actor_draw_distance'], 1)

    def test_photo_default_upgrade_preserves_custom_placement(self):
        old = dict(self.policy['current'], gear_6_x=0, gear_6_y=0, gear_6_scale=1)
        new, held = self.apply(old)
        for key in ('gear_6_x', 'gear_6_y', 'gear_6_scale'):
            self.assertEqual(new[key], self.policy['current'][key])
        self.assertNotIn('gear_photo', held)
        old['gear_6_x'] = 42
        new, held = self.apply(old)
        self.assertEqual(new, old)
        self.assertIn('gear_photo', held)

    def test_optional_stick_controls_preserve_existing_preference(self):
        old=dict(self.policy['previous'])
        new,_=self.apply(old)
        self.assertFalse(new['stick_click_controls'])
        self.assertTrue(self.policy['current']['stick_click_controls'])
        for enabled in (False, True):
            old['stick_click_controls']=enabled
            new,_=self.apply(old)
            self.assertEqual(new['stick_click_controls'],enabled)
            again,_=self.apply(new)
            self.assertEqual(again,new)

    def test_serialized_centered_mask_description_upgrades(self):
        for source in (self.policy['previous'], self.policy['current'], {}):
            old = dict(self.policy['current'], mask_present_6_x=0,
                       mask_present_6_y=source.get('dialogue_y', 0),
                       mask_present_6_scale=source.get('dialogue_scale', 1))
            new, _ = self.apply(old)
            for key in ('mask_present_6_x', 'mask_present_6_y', 'mask_present_6_scale'):
                self.assertEqual(new[key], self.policy['current'][key], key)

    def test_serialized_inherited_mask_panel_upgrades(self):
        for source in (self.policy['previous'], self.policy['current']):
            old = dict(self.policy['current'])
            for key in old:
                if key.startswith(('mask_present_1_', 'mask_present_3_', 'mask_present_help_')):
                    old[key] = source[key.replace('mask_present_', 'item_present_')]
            new, _ = self.apply(old)
            self.assertEqual(new, self.policy['current'])

    def test_custom_mask_panel_does_not_block_stock_description_upgrade(self):
        old = dict(self.policy['current'], mask_present_1_x=123,
                   mask_present_6_x=0, mask_present_6_y=self.policy['previous']['dialogue_y'],
                   mask_present_6_scale=self.policy['previous']['dialogue_scale'])
        new, held = self.apply(old)
        self.assertEqual(new['mask_present_1_x'], 123)
        self.assertEqual(new['mask_present_6_x'], self.policy['current']['mask_present_6_x'])
        self.assertIn('mask_selection', held)
        custom = dict(old, mask_present_6_x=42)
        new, held = self.apply(custom)
        self.assertEqual(new, custom)
        self.assertIn('mask_description', held)

    def test_old_hud_defaults_upgrade_but_custom_values_stay(self):
        for key in ('magic_bar_height',):
            old = dict(self.policy['current'])
            old[key] = self.policy['previous'][key]
            new, _ = self.apply(old)
            self.assertEqual(new[key], self.policy['current'][key], key)
            old[key] = .54321
            new, _ = self.apply(old)
            self.assertEqual(new, old)

    def test_arranged_defaults_preserve_hud_and_custom_map_widgets(self):
        keys = tuple(self.policy['existing_install_defaults'])
        for customized in (False, True):
            old = dict(self.policy['previous'])
            old.update({k: v for k, v in self.policy['existing_install_defaults'].items()})
            if customized:
                old.update(player_hud_hearts_x=47, player_hud_heart_spacing=12,
                           player_hud_magic_width=1.7, player_hud_base_scale=-2,
                           minimap_opacity=.31, dungeon_map_2_y=81,
                           dungeon_map_prompts_scale=1.23, dungeon_map_prompts_spacing=31,
                           dungeon_map_arrow_up_y=17, notebook_buttons_scale=1.6,
                           notebook_prompt_y=9, notebook_map_prompt_y=-29)
            new, _ = self.apply(old)
            for key in keys:
                self.assertEqual(new[key], old[key], key)
            expected=dict(old)
            for group in self.policy['legacy_dungeon_widgets'].values():
                if all(k in old and migration.close(old[k],v) for k,v in group.items()):
                    expected.update({k:self.policy['current'][k] for k in group})
            for key in old:
                if key.startswith(('dungeon_map_', 'notebook_')):
                    self.assertEqual(new[key], expected[key], key)
            for key in self.policy['fresh_install_only']:
                if key not in old:
                    self.assertNotIn(key, new)  # Keep legacy Notebook inheritance.
            again, _ = self.apply(new)
            self.assertEqual(again, new)

    def test_absent_hud_fields_use_legacy_values(self):
        new, _ = self.apply({'version': 1})
        for key, value in self.policy['existing_install_defaults'].items():
            self.assertEqual(new[key], value, key)
        for key in self.policy['fresh_install_only']:
            self.assertNotIn(key, new)
        for opacity in (.85, .75, .54):
            new, _ = self.apply(dict(self.policy['previous'], minimap_opacity=opacity))
            self.assertEqual(new['minimap_opacity'], opacity)

    def test_hd_photo_fallback_preference_survives_upgrade(self):
        new, _ = self.apply(dict(self.policy['previous']))
        self.assertTrue(new['hd_pictograph_photos'])
        new, _ = self.apply(dict(self.policy['previous'], hd_pictograph_photos=False))
        self.assertFalse(new['hd_pictograph_photos'])

    def test_previous_arranged_hud_keeps_values_and_reference_version(self):
        previous = dict(self.policy['current'], player_hud_layout_version=1,
                        player_hud_hearts_scale=4, player_hud_heart_spacing=-17,
                        player_hud_magic_height=-4, player_hud_magic_y=5,
                        player_hud_rupees_scale=2, player_hud_mirror_item_buttons=False)
        for revision in (0, 1, 2, 3):
            for customized in (False, True):
                old = dict(previous)
                if customized:
                    old.update(player_hud_hearts_scale=7, player_hud_magic_y=19,
                               player_hud_left_padding=-17, player_hud_right_padding=23,
                               player_hud_mirror_item_buttons=True)
                new, _ = self.apply(old, revision)
                self.assertEqual(new, old)

    def test_revision_three_upgrades_only_stock_dungeon_layout(self):
        old=dict(self.policy['current'], gear_6_x=0, gear_6_y=0, gear_6_scale=1,
                 magic_bar_height=self.policy['previous']['magic_bar_height'],
                 minimap_opacity=.31, player_hud_heart_spacing=21, player_hud_hearts_scale=7,
                 notebook_buttons_scale=1.2, notebook_map_prompt_y=-31,
                 dungeon_map_prompts_scale=1.7, dungeon_map_prompts_spacing=29)
        old.update(self.policy['legacy_arrow_defaults'])
        for group in self.policy['legacy_dungeon_widgets'].values():old.update(group)
        new,held=self.apply(old,3)
        self.assertEqual(held,[])
        expected=dict(old)
        for key in self.policy['legacy_arrow_defaults']:expected[key]=self.policy['current'][key]
        for group in self.policy['legacy_dungeon_widgets'].values():
            for key in group:expected[key]=self.policy['current'][key]
        self.assertEqual(new,expected)
        again,_=self.apply(new,3);self.assertEqual(again,new)

    def test_custom_arrow_group_remains_whole_and_missing_keys_stay_missing(self):
        legacy=self.policy['legacy_arrow_defaults']
        for revision in (0,3):
            for key in legacy:
                old=dict(self.policy['current']);old.update(legacy);old[key]+=0.000001
                expected=dict(old);new,held=self.apply(old,revision)
                self.assertEqual(new,expected)
                self.assertIn('dungeon_map_arrows',held)
            for overrides in ({'dungeon_map_arrow_up_y':17}, {'dungeon_map_arrows_scale':.77}):
                old=dict(self.policy['current'])
                for key in legacy:old.pop(key)
                old.update(overrides);new,held=self.apply(old,revision)
                self.assertEqual(new,old)
                self.assertIn('dungeon_map_arrows',held)
        new,_=self.apply({'version':1},3)
        self.assertEqual(new,dict({'version':1},**{k:self.policy['current'][k] for k in legacy}))

    def test_custom_or_incomplete_dungeon_widget_never_partially_moves(self):
        for name,group in self.policy['legacy_dungeon_widgets'].items():
            for revision in (0,3):
                for key in group:
                    old=dict(self.policy['current']);old.update(group);old[key]+=1
                    new,held=self.apply(old,revision)
                    self.assertEqual(new,old);self.assertIn(name,held)
                    old.pop(key);new,held=self.apply(old,revision)
                    self.assertEqual(new,old);self.assertIn(name,held)

    def test_retired_profiles_are_explicitly_allowlisted(self):
        from upgrade_game_resources import RETIRED
        self.assertEqual(set(self.policy['retired_profiles']),set(RETIRED))
        for p in (RESOURCES/'defaults').rglob('*.json'):
            self.assertNotIn('config/mm3d-local/'+p.relative_to(RESOURCES/'defaults').as_posix(),RETIRED)
