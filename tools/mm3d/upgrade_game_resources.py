"""Versioned, resumable upgrade of owned resources; never replaces saves or HUD settings."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import uuid
import layout_migration

# Recovery-only path: old interrupted journals may contain this retired asset.
LEGACY_SKY = 'setup/file_select_sky.rgba'
RESOURCE_VERSION = 2
TITLE = '0004000000125500'
MESSAGE_NAMES = ('use.gmsg', 'usf.gmsg', 'uss.gmsg')
# Outputs verified against the compiled English/French/Spanish dialogue storage.
MESSAGE_HASHES = {'use.gmsg': '70bd72f961a6ff168604249b4c76f893f1cad6fbd9da0290e85f49cd319a1314', 'usf.gmsg': 'b848188634a5bf6c9f2cd837102420dbe0a3275d7fd17bbe0057e8dcbd173a77', 'uss.gmsg': '3310a03cf6d67a4ce7c054d7fff2b31fd309fcf966e91610f83ffdd92281a688'}
MESSAGE_PATHS = tuple('load/mods/'+TITLE+'/romfs/message/us/'+n for n in MESSAGE_NAMES)
# Retain this journal path only to recover transactions staged by earlier builds.
NOTEBOOK = 'config/mm3d-local/notebook.json'
READY = 'setup/ready.json'
PENDING = 'setup/resource-upgrade-pending'
RETIRED = frozenset('config/mm3d-local/'+p for p in (
    'hud/clock.json','hud/hearts.json','hud/hide_hud_signal.json','hud/loading.json',
    'notebook/event-notes-desc.json','notebook/event_notes_signal.json','notebook/notebook_bottom_bar.json',
    'notebook/notebook_events_context.json','notebook/notebook_open_signal.json',
    'title-screen/copyright.json','title-screen/press_start.json','title-screen/title_logo.json',
    'title-screen/title_logo_1.json','title-screen/version.json'))
ALLOWED = frozenset((*MESSAGE_PATHS, NOTEBOOK, READY, layout_migration.CONFIG, layout_migration.BASELINE, LEGACY_SKY, *RETIRED))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked(user, relative):
    # Reject even internal symlinks: installation ownership must be unambiguous.
    if user.is_symlink() or not user.is_dir():
        raise ValueError('The user folder must be a real directory; it was not changed.')
    path = user
    for part in relative.split('/'):
        if part in ('', '.', '..') or '\\' in part or ':' in part:
            raise ValueError('Invalid upgrade path')
        path = path/part
        if path.is_symlink():
            raise ValueError('Upgrade refuses a symbolic link: '+relative)
    return path


def flush_directory(path):
    if os.name != 'nt':
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.dekui-write-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
        flush_directory(path.parent)
    finally:
        Path(name).unlink(missing_ok=True)


@contextlib.contextmanager
def locked(user):
    path = checked(user, 'setup/resource-upgrade.lock')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        if os.name == 'nt':
            import msvcrt
            if path.stat().st_size == 0:
                stream.write(b'0'); stream.flush()
            stream.seek(0)
            # Nonblocking: another startup must not launch against partial data.
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise ValueError('Another Dekui resource upgrade is running. Close it and retry.') from error
        else:
            import fcntl
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise ValueError('Another Dekui resource upgrade is running. Close it and retry.') from error
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0); msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def resume(user):
    pending = checked(user, PENDING)
    if not pending.exists():
        return
    journal = json.loads(checked(user, PENDING+'/journal.json').read_bytes())
    if journal.get('version') != RESOURCE_VERSION or journal.get('root') != str(user.resolve()):
        raise ValueError('An interrupted upgrade belongs to a different location/version. Restore the folder to its previous location and retry; backups were retained.')
    entries = journal.get('files', [])
    names = [row['path'] for row in entries]
    if not names or len(names) != len(set(names)) or not set(names) <= ALLOWED or names[-1] != READY:
        raise ValueError('Invalid upgrade journal; backups were retained.')
    # Preflight EVERY entry before writing any. A retry permits only old/new bytes.
    prepared = []
    for row in entries:
        name = row['path']
        target = checked(user, name)
        new = checked(user, PENDING+'/new/'+name).read_bytes() if row['new'] is not None else None
        if new is None and name not in RETIRED:
            raise ValueError('Upgrade refuses to remove a non-legacy file')
        if new is not None and digest(new) != row['new']:
            raise ValueError('Staged upgrade checksum mismatch; backups were retained: '+name)
        if row['old'] is not None:
            old = checked(user, PENDING+'/backup/'+name).read_bytes()
            if digest(old) != row['old']:
                raise ValueError('Upgrade backup checksum mismatch: '+name)
        current = digest(target.read_bytes()) if target.exists() else None
        if current not in (row['old'], row['new']):
            raise ValueError('File changed during upgrade; refusing to overwrite: '+name)
        prepared.append((target, new))
    for target, new in prepared:
        if new is None:
            target.unlink(missing_ok=True)
            flush_directory(target.parent)
        elif not target.exists() or target.read_bytes() != new:
            atomic_write(target, new)
    # Ready marker is written last, and startup also checks pending is gone.
    backup = pending.parent/('resource-upgrade-backup-'+uuid.uuid4().hex)
    pending.rename(backup)
    flush_directory(backup.parent)


def publish(user, changes):
    # Create complete durable backups and staged data before changing live files.
    stage = Path(tempfile.mkdtemp(prefix='.resource-upgrade-', dir=user/'setup'))
    try:
        entries = []
        for name, new in changes.items():
            if name not in ALLOWED:
                raise ValueError('Upgrade attempted an unowned path: '+name)
            target = checked(user, name)
            old = target.read_bytes() if target.exists() else None
            if old is not None:
                atomic_write(stage/'backup'/name, old)
            if new is None and name not in RETIRED:
                raise ValueError('Upgrade refuses to remove a non-legacy file')
            if new is not None:
                atomic_write(stage/'new'/name, new)
            entries.append({'path': name, 'old': digest(old) if old is not None else None,
                            'new': digest(new) if new is not None else None})
        if not entries or entries[-1]['path'] != READY:
            raise ValueError('Upgrade must publish readiness last')
        atomic_write(stage/'journal.json', (json.dumps({'version': RESOURCE_VERSION,
                     'root': str(user.resolve()), 'files': entries}, indent=2)+'\n').encode())
        pending = checked(user, PENDING)
        if pending.exists():
            raise ValueError('An upgrade is already pending')
        stage.rename(pending)
        flush_directory(pending.parent)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    resume(user)


def upgrade(root, rom, restoration):
    # Imports here avoid a setup/upgrader import cycle.
    from setup_game_folder import validate, extract, RESOURCES
    from build_release_assets import build
    user = root/'user'
    with locked(user):
        expected_rom = json.loads((RESOURCES/'supported-rom.json').read_text())['main_partition_sha256']
        ready = json.loads(checked(user, READY).read_bytes())
        if ready.get('version') != 1 or ready.get('rom_sha256') != expected_rom:
            raise ValueError('Unrecognized game setup. Existing user data was not replaced.')
        version = ready.get('resource_version', 1)
        if type(version) is not int or version not in (1, RESOURCE_VERSION):
            raise ValueError('Unsupported resource version; refusing to downgrade or replace user data.')
        layout_policy = layout_migration.policy(RESOURCES)
        for key, maximum in (('layout_version', layout_migration.VERSION),):
            v = ready.get(key, 0)
            if type(v) is not int or not 0 <= v <= maximum:
                raise ValueError('Unsupported '+key+'; refusing to downgrade user data')
        validate(rom)
        resume(user)
        ready = json.loads(checked(user, READY).read_bytes())
        changes = {}
        if ready.get('layout_version', 0) < layout_migration.VERSION:
            config = checked(user, layout_migration.CONFIG)
            if config.exists():
                old = config.read_bytes()
                new, retained = layout_migration.migrate(old, layout_policy)
                if new != old:
                    changes[layout_migration.CONFIG] = new
            else:
                changes[layout_migration.CONFIG], retained = layout_migration.migrate(
                    b'{"version":1}', layout_policy)
            changes[layout_migration.BASELINE] = layout_migration.baseline(layout_policy, retained)
            for relative, sha in layout_policy['retired_profiles'].items():
                target = checked(user, relative)
                if target.is_file() and digest(target.read_bytes().replace(b'\r\n', b'\n')) == sha:
                    changes[relative] = None  # Exact disabled stock profiles only; backed up first.
            ready['layout_version'] = layout_migration.VERSION
        missing = any(not checked(user, p).is_file() for p in MESSAGE_PATHS)
        if ready.get('resource_version', 1) < RESOURCE_VERSION or missing:
            with tempfile.TemporaryDirectory(prefix='.resource-build-', dir=user/'setup') as tmp:
                stage = Path(tmp)
                extract(rom, stage/'original')
                build(stage/'original', restoration, stage/'mods')
                for name, relative in zip(MESSAGE_NAMES, MESSAGE_PATHS):
                    new = (stage/'mods/romfs/message/us'/name).read_bytes()
                    if digest(new) != MESSAGE_HASHES[name]:
                        raise ValueError('Generated dialogue checksum mismatch: '+name)
                    original = (stage/'original/romfs/message/us'/name).read_bytes()
                    target = checked(user, relative)
                    if target.exists() and target.read_bytes() not in (original, new):
                        raise ValueError('Custom dialogue resource conflicts with this update: '+str(target)+'. Back it up and move that file out of the mods folder, then retry. Saves and settings were not replaced.')
                    changes[relative] = new
        else:
            for name, relative in zip(MESSAGE_NAMES, MESSAGE_PATHS):
                if digest(checked(user, relative).read_bytes()) != MESSAGE_HASHES[name]:
                    raise ValueError('Installed dialogue is modified or damaged: '+relative+'. Back up and move that file out of the mods folder, then retry. It was not overwritten.')
        if changes:
            ready['resource_version'] = RESOURCE_VERSION
            changes.pop(READY, None)  # Readiness must be the final transaction entry.
            changes[READY] = (json.dumps(ready, indent=2)+'\n').encode()
            publish(user, changes)
