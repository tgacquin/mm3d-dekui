# Fan translations

In Controls, choose **Import translation folder...** and select the pack's `romfs`
folder (or its parent). **Use imported translation** enables the imported pack
on the next game restart. Turning it off restores the standard resource layers
on the next restart. Importing or toggling does not change an active dialogue or
purchase. Existing installations default to off.

The desktop importer accepts any language in USA-compatible MM3D GMSG v1 archives:
`message/us/use.gmsg`, `usf.gmsg`, and/or `uss.gmsg`. A pack can replace one or more
slots; the game's selected language determines which slot is read. The importer
does not detect the written language or translate text. Packs for incompatible
regions, changed message IDs, or changed gameplay attributes need conversion by
their authors first. A filename/header slot mismatch is normalized locally.

All unrelated translation text is retained. Only messages changed by enabled
DekUI features fall back to English:

- Single-screen instructions, including the selected stick-click controls.
- Classic Saving and its return notice.
- Classic Double Time questions.
- The 147 shopping/fee messages covered by 2× and 3× prices. Message price fields
  use the same existing price tables as standard DekUI, including the 500-rupee
  cap. Gameplay charge hooks are unchanged.

Disabling an individual feature restores that message's imported text. Runtime
writes recognize only the imported original and generated variants, validate all
text and attributes, and preflight all destinations. Unrecognized data is left
alone. This is an English fallback, not localized DekUI dialogue; a translation's
custom font must still support the English glyphs.

Imported fonts (`message/**/*.gzf`) and menu textures (`menu/**/*.ctxb`) are kept
with the pack. Backups and unrelated files such as code/layout patches are not
imported. Translation files are supplied by the user and never shipped in DekUI.

## Storage and validation

Imports live under the existing configuration directory's
`mm3d-local/translations/`, separately from standard `load/mods` assets and
`presentation.json`. Each import gets a new UUID directory with original source
files, a prepared RomFS layer, and a SHA-256 manifest. `selection.json` selects and
enables the pack only after validation succeeds. A failed import leaves the
previous selection intact. Older packs are retained.

At game load, DekUI validates the original hashes, regenerates the expected
adaptations, and compares every prepared file before mounting the extra layer.
Tampering, missing files, or an adaptation change in a later release causes
fallback to standard resources and a log error; import again to regenerate it.
Standard updater checksums are unchanged. This layer only applies to the supported
USA MM3D title and its update, with LayeredFS enabled. Native game resources,
saves, controller bindings, and existing settings are not rewritten.

The folder picker is desktop-only. Android builds retain the runtime support,
but importing from Android storage has not been implemented or tested.

## Checks

Build `mm3d_translation_tests` and run it without arguments for synthetic tests.
Optionally pass a local translation folder to validate it without installing it
in the user's configuration. Tests import into temporary storage, exercise every
runtime variant and restoration, verify unrelated bytes, corrupt payloads and
attributes, test partial slots, failed-import rollback, tamper rejection, and
restart-only enable/disable behavior. No game or translation assets are committed.
