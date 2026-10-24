"""Conservative, versioned migration of shipped layouts (never gameplay preferences)."""
import copy
import json
import math

# Re-run for installations that already migrated their photo placement but
# retained serialized mask fallbacks and the former shipped HUD defaults.
VERSION = 3
CONFIG = 'config/mm3d-local/presentation.json'
BASELINE = 'setup/layout-baseline.json'
RESOURCE = 'layout-migration.json'
GROUPS = {
    'songs': ('song_', 'ocarina_prompt', 'ocarina_icon_scale'),
    'file_select': ('file_select_',),
    'item_selection': ('item_present_',),
    'gear_photo': ('gear_6_',),
    'mask_selection': ('mask_present_1_', 'mask_present_3_', 'mask_present_4_',
                       'mask_present_5_', 'mask_present_help_', 'mask_present_native'),
    'mask_description': ('mask_present_6_',),
    'magic_height': ('magic_bar_height',),
}


def close(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return type(a) is type(b) and a == b
    if isinstance(a, (float, int)) and isinstance(b, (float, int)):
        return math.isfinite(a) and math.isfinite(b) and math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-5)
    return a == b


def policy(resources):
    data = json.loads((resources/RESOURCE).read_text())
    current = json.loads((resources/'defaults/presentation.json').read_text())
    if data['version'] != VERSION or data['current'] != current:
        raise ValueError('Layout defaults changed without an updated migration policy')
    # Every profile participates in the release gate, even those with no migration.
    import hashlib
    actual = {p.relative_to(resources/'defaults').as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in (resources/'defaults').rglob('*') if p.is_file()}
    if actual != data['profile_hashes']:
        raise ValueError('Layout profiles changed without an updated migration policy')
    return data


def migrate(raw, data):
    if len(raw)>1024*1024:
        raise ValueError('Presentation settings exceed the supported size; original retained')
    def invalid_constant(value):
        raise ValueError('Invalid non-finite setting: '+value)
    obj = json.loads(raw, parse_constant=invalid_constant)
    if not isinstance(obj, dict) or type(obj.get('version')) is not int or obj['version'] != 1:
        raise ValueError('Unsupported presentation settings; original retained')
    original = copy.deepcopy(obj)
    old, current = data['previous'], data['current']
    retained = []
    for name, prefixes in GROUPS.items():
        keys = [k for k in current if k.startswith(prefixes)]
        # These newly introduced values can have been serialized by beta.2's
        # old loader before the first migration. Match the whole shipped group,
        # not isolated fields of a customized layout.
        expected = dict(old)
        expected.update(data['legacy_fallbacks'])
        # Older loaders serialized inherited item/dialogue settings into the
        # new mask keys. Recognize only documented complete fallback groups.
        candidates = [expected, *data.get('legacy_group_defaults', {}).get(name, [])]
        if any(all(k not in obj or close(obj[k], candidate.get(k, current[k])) or
                   close(obj[k], current[k]) for k in keys) for candidate in candidates):
            for k in keys:
                obj[k] = current[k]
        else:
            retained.append(name)
    # Update only the exact previously shipped fade rules. A user may have
    # deliberately removed a rule or added their own; preserve that whole list.
    previous_rules = data.get('previous_fullscreen_draws')
    if isinstance(previous_rules, list) and obj.get('fullscreen_draws') == previous_rules:
        obj['fullscreen_draws'] = copy.deepcopy(current['fullscreen_draws'])
    # Absent keys receive release defaults, except preferences whose legacy
    # default must differ from a new installation (such as stick-click controls).
    # Existing choices, unknown keys and ambiguous layout groups stay unchanged.
    held_prefixes = tuple(p for name in retained for p in GROUPS[name])
    for k, v in current.items():
        if k not in obj and k not in data.get('fresh_install_only', []) and not (held_prefixes and k.startswith(held_prefixes)):
            obj[k] = data.get('existing_install_defaults', {}).get(k, v)
    return (raw if obj == original else (json.dumps(obj, indent=2)+'\n').encode()), retained


def baseline(data, retained=()):
    return (json.dumps({'version': VERSION, 'defaults': data['current'],
                        'preserved_custom_groups': list(retained)}, indent=2)+'\n').encode()
