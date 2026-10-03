# Source and builds

Each release must provide the complete corresponding source alongside its Windows and Steam Deck downloads. The source archive must match the revision used to build those binaries.

Release source snapshots include the application code, setup and packaging scripts, build definitions, tests, and pinned submodule sources. Bundled Project Restoration has its own matching source archive and notices under `resources/mm3d/project-restoration/`. Build dependencies downloaded by the build scripts remain identified there.

Snapshots do not need a Git checkout to inspect the code. They do not include internal working notes or the development repository’s Git history. Copyright notices and dependency licenses remain intact.

## Build reference

The source archive contains the platform workflows and scripts used by the builds:

- Windows: `.github/workflows/mm3d-windows-test.yml` and `tools/mm3d/windows/build-ci.sh`.
- Steam Deck: `.github/workflows/mm3d-deck-test.yml` and `tools/mm3d/deck/build-ci.sh`.

Use the toolchain and dependency versions/configuration in those workflows. Windows uses MSYS2 UCRT64; Deck uses the build environment selected by its workflow. Both enable `ENABLE_MM3D_PRESENTATION` and `ENABLE_MM3D_GAME_FOLDER` and disable `ENABLE_BUILTIN_KEYBLOB`.

The scripts build the app, run tests, and prepare portable packages. Source-archive creation from Git is a separate maintainer step; an extracted source snapshot already contains the archived submodule sources.

## Release contents

A published release should contain:

- The Windows ZIP and Steam Deck package.
- Matching complete source archive(s) for those binaries.
- SHA-256 checksums for the downloadable files.
- Release notes describing changes and known limitations.

A binary release should not be published without its matching source and required notices. This repository is a release hub, not a claim that its documentation alone is the application’s source.
