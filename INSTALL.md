# Installation

## What you need

A supported decrypted USA v1.1 game-card dump in `.cci` or `.3ds` format, from a card with v1.1 preinstalled. The filename can be anything. Keep exactly one game file beside the application; multiple candidates or unsupported files block startup.

Project Restoration and the required HUD layouts are included. Optional custom texture packs are separate.

## Windows

1. Extract `Dekui-1.0.0-beta.1-Windows-x64.zip` into a writable location and open the `dekui` folder.
2. Keep `Dekui.exe`, its DLLs, and the `plugins`, `tools`, `resources`, and `runtime` folders together.
3. Put your game beside `Dekui.exe`, then open it.

Windows 10/11 x64 and a Vulkan-capable graphics driver are required. Python is bundled privately for setup; you do not need to install it or modify PATH. The application is unsigned.

## Linux

1. Extract `Dekui-1.0.0-beta.1-Linux-x86_64.tar.gz` and open the `dekui` folder. If downloading a build artifact from Actions, extract its outer ZIP first.
2. Put the extracted folder somewhere writable under your home directory. Keep `Dekui.AppImage`, `tools`, and `resources` together.
3. Put your game beside `Dekui.AppImage` and make the AppImage executable if needed.
4. Launch `Dekui.AppImage`.

Python 3.9 or newer and a Vulkan-capable graphics driver are required.

If FUSE is unavailable, launch from a terminal with:

```sh
APPIMAGE_EXTRACT_AND_RUN=1 ./Dekui.AppImage
```

## First launch and later launches

First launch silently checks the game and builds the required mod files in `user/` before opening the game. Allow extra time on first launch. There are no file-selection forms or preparation progress windows; an error is shown if setup cannot finish.

The game stays in place; setup does not move or duplicate it. Later launches use the prepared files. Keep the game in the application folder: missing or unsupported games block gameplay. Older installations using `game/mm3d.cci` remain supported.

## Settings

Press **F1** for Graphics, HUD, Difficulty, Audio, Controls, and About. Game touchscreen input is disabled. Controller and keyboard mappings can be changed under Controls.

Hold **ZR** and press **SELECT** during gameplay to hide or show the minimap. The full map remains available from the inventory Map tab.

For an optional texture pack, use **Graphics → Open texture folder**, install a compatible pack, enable **Use custom textures**, and restart Dekui.

## Updates and saves

Back up `user/` before updating. Preserve your game and the entire `user/` folder, including saves. Replace application files with the complete new package; keep its runtime and resource folders together.

Setup refuses to overwrite existing user data. If setup reports an incomplete or incompatible installation, preserve that folder and try a fresh application folder. Do not delete saves to troubleshoot.
