# Installation

## What you need

A supported decrypted USA v1.1 game-card dump in `.cci` or `.3ds` format, from a card with v1.1 preinstalled. The filename can be anything. Keep exactly one game file beside the application; multiple candidates or unsupported files block startup.

Project Restoration and the required HUD layouts are included. Optional custom texture packs are separate.

## Windows

1. Extract `Dekui-1.1.0-Windows-x64.zip` into a writable location and open the `dekui` folder.
2. Keep `Dekui.exe`, its DLLs, and the `plugins`, `tools`, `resources`, and `runtime` folders together.
3. Put your game beside `Dekui.exe`, then open it.

Windows 10/11 x64 and a Vulkan-capable graphics driver are required. Python is bundled privately for setup; you do not need to install it or modify PATH. The application is unsigned.

## Linux

1. Extract `Dekui-1.1.0-Linux-x86_64.tar.gz` and open the `dekui` folder. If downloading a build artifact from Actions, extract its outer ZIP first.
2. Put the extracted folder somewhere writable under your home directory. Keep `Dekui.AppImage`, `tools`, and `resources` together.
3. Put your game beside `Dekui.AppImage` and make the AppImage executable if needed.
4. Launch `Dekui.AppImage`.

Python 3.9 or newer and a Vulkan-capable graphics driver are required. Keep Python installed for startup resource checks and upgrades.

If FUSE is unavailable, launch from a terminal with:

```sh
APPIMAGE_EXTRACT_AND_RUN=1 ./Dekui.AppImage
```

## macOS (Apple Silicon)

1. Extract `Dekui-1.1.0-macOS-arm64.zip`. If downloading a build artifact from Actions, extract its outer ZIP first.
2. Move the entire `dekui` folder with Finder to a writable location, such as Documents. Keep `Dekui.app`, `tools`, and `resources` together; do not move only the app.
3. Put your game beside `Dekui.app`, then open it.
4. If macOS blocks the app because the developer cannot be verified, open **System Settings → Privacy & Security → Open Anyway**, then confirm **Open**.

An Apple Silicon Mac (M1 or newer) running macOS 13.4 or newer is required. This package does not support Intel Macs. Python and app dependencies are bundled; Homebrew and developer tools are not required. The app is ad-hoc signed and is not Apple-notarized.

If the app cannot find its adjacent files after downloading, quit it, move the entire extracted folder with Finder, and reopen it.

## First launch and later launches

First launch silently checks the game and builds the required mod files in `user/` before opening the game. Allow extra time on first launch. There are no file-selection forms or preparation progress windows; an error is shown if setup cannot finish.

The game stays in place; setup does not move or duplicate it. Later launches check for required resource upgrades and use the prepared files. Keep the game in the application folder: missing or unsupported games block gameplay.

## Settings

Press **F1** (or **Fn–F1** on a Mac using media keys) for General, Graphics, HUD, Difficulty, Restoration, QOL, Audio, Controls, and About. Game touchscreen input is disabled. Controller and keyboard mappings can be changed under Controls.

Fullscreen defaults to **F11** on Windows/Linux and **Control–Command–F** on macOS. Change the shortcut under **Controls → Fullscreen shortcut**. **Esc** leaves fullscreen. New profiles start fullscreen; your chosen windowed/fullscreen mode is remembered for later launches.

By default, hold **ZR** and press **SELECT** during gameplay to hide or show the minimap. The full map remains available from the inventory Map tab.

### Optional stick-click controls

Enable **Controls → Use stick-click controls** to use this layout:

| Input | Action |
| --- | --- |
| Left-stick click | Hide or show the minimap |
| Right-stick click | Enter first-person view or use the Pictograph Box |
| ZL | Equip and use Item I |
| ZR | Equip and use Item II |

This option is off by default. English, French, and Spanish instructions and button prompts follow the selected layout. Turning it off restores the previous controls and instructions. Existing custom controller bindings are preserved; check them under **Controls → Configure controller / keyboard** if a stick click does not respond.

### Pictograph photos

**Graphics → HD Pictograph photos (restart required)** is enabled by default. New HD photos use widescreen framing and the game's rendering resolution. If taking or viewing photos causes crashes or stalls, disable this option and restart the game. Native photos will be used, and HD capture, loading, and saving will be bypassed. Previously saved HD files remain on disk. Existing low-resolution photos cannot gain detail by enabling HD.

### Audio

**Audio → Audio mode** offers **Balanced** and **Accurate HLE**. Balanced corrects the identified rupee samples, including other effects using those same samples, while retaining standard processing for other sounds. Accurate HLE applies the broader audio-accuracy behavior.

New installations default to Balanced. When upgrading, previous audio selections migrate to Balanced once. Choosing Accurate HLE afterward is remembered.

### Fan translations

Under **Controls → Fan translation**, choose **Import translation folder…** and select the pack's `romfs` folder or its parent. Importing enables a validated pack; restart the game to apply it. Uncheck **Use imported translation (restart required)** and restart to return to the standard resources.

Translation support is off by default and has been tested with the Portuguese pack. Packs must use USA-compatible message archives. Supported fonts and menu textures are imported; code and layout patches are not. Messages changed by enabled DekUI features use English fallbacks, including control instructions, Classic Saving, Classic Double Time, and multiplied shopping fees. Other messages keep their translated text. Translation files are supplied separately by the user.

### Custom textures

For an optional texture pack, use **Graphics → Open texture folder**, install a compatible pack, enable **Use custom textures**, and restart Dekui.

## Updates and saves

1. Close Dekui and back up your entire `user` folder.
2. Extract the complete new release into a separate writable folder.
3. Copy your existing `user` folder and game file beside the new application.
4. Launch the new version.

Saves, custom HUD settings, graphics preferences, custom textures, and unrelated mods are preserved, except for the one-time audio migration to Balanced described above. On first launch, recognized older menu layouts update automatically with backups; customized layouts are kept. This includes the Gear Pictograph and Moon mask-selection positions. Old stock magic-bar height and minimap opacity values also receive the updated defaults; custom values are preserved.

To restore the current Songs, file-select, item-selection, Moon mask-selection, and Gear Pictograph layouts manually, use **General → Reset menu layouts**. This action backs up the previous settings and keeps gameplay, controls, and HUD preferences. Keep the app with its bundled `tools`, `resources`, and any runtime folders.

If the upgrade fails or is interrupted, the game will not open; relaunch to retry. Resource backups are retained under `user/setup/resource-upgrade-backup-*` after success, or `user/setup/resource-upgrade-pending/backup` while an upgrade is pending. Do not delete your `user` folder or these backups to troubleshoot.
