# DekUI

This is a mod to make Majora's Mask 3D fully functional on one screen. In fact, the bottom screen isn't even accessible.

Every element that could only be interacted with through the bottom screen has now been brought to the top. Previously large HUD elements have also been adapted
in an effort to bring it in line with the original game. The goal was to make this game feel like any other console Zelda while also allowing for a very
customizable experience so it could feel fresh for returning players.

A custom fork of Azahar is used as the backing engine.

## Features

- Gear, Masks, Items, and Map accessible all through one screen with item models and descriptions preserved
- 60fps interpolation with CPU (default) and GPU options; select Original in Graphics for 30fps
- Full Bomber's Notebook functionality on one screen
- Song menu, song of double time, and give item menus accessible on one screen
- File select and options on one screen
- On-screen minimap (hold ZR and press SELECT to hide or show it)
- Project Restoration features (https://restoration.zora.re/)
- New toggleable restoration features (classic saving, classic owl statue behavior, classic Song of Double Time, stop Bombers from running up to you)
  - Classic Saving allows saving at owl statues, but not feather statues. Saving returns to the title screen only when a save from a Song of Time reset already exists.
- Redesigned HUD elements (smaller and wider textboxes, thicker magic bar) meant to emulate a console-like experience
- HUD scaling, positioning, spacing, and visibility controls
- Difficulty options (damage multiplier, adjustable red potion, blue potion, and bottled fairy healing, no heart, rupee, fairy, or magic drops, disabled ammo pickups, rupee loss or moon crash on death, and 2×/3× purchase prices)
- Widescreen, increased FOV, and slider to adjust camera distance
- References in text to the bottom screen and other 3DS specific features have been replaced with any new controls for a more console-like experience
- Correct rupee sounds for HLE audio (for the first time through a 3DS emulator!)
- Pause menu background image adapts to resolution (3DS/emulators default to 240p)
- Correct bloom lighting at higher resolutions
- Custom texture support
- French and Spanish support with translations for custom button prompts and new dialogue
- Many other minor changes in camera and HUD elements

## Screenshots

<table>
  <tr>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 47 23 PM" src="https://github.com/user-attachments/assets/b8789b85-ba46-4597-8620-2cb74fd189dd" /></td>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 22 54 PM" src="https://github.com/user-attachments/assets/d5ad8081-d427-42d9-a425-e6a5142bbe0f" /></td>
  </tr>
  <tr>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 24 03 PM" src="https://github.com/user-attachments/assets/db42b110-943d-4cd7-994e-61dc4f5cd797" /></td>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 24 33 PM" src="https://github.com/user-attachments/assets/a4b8a4ff-8c8c-4422-939f-098599a2931b" /></td>
  </tr>
  <tr>
    <td><img width="350" alt="Screenshot 2026-10-03 at 2 44 17 PM" src="https://github.com/user-attachments/assets/91f4d8e8-f15e-4e51-a6d0-2977a7d18b1e" /></td>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 24 57 PM" src="https://github.com/user-attachments/assets/a86a93a6-683f-4d57-81f0-09e11770c374" /></td>
  </tr>
  <tr>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 26 27 PM" src="https://github.com/user-attachments/assets/a98b6072-cf63-4340-8103-a65139892c8f" /></td>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 27 38 PM" src="https://github.com/user-attachments/assets/168108f2-0b24-4d58-8428-b6c44977741e" /></td>
  </tr>
  <tr>
    <td><img width="350" alt="Screenshot 2026-10-03 at 2 47 16 PM" src="https://github.com/user-attachments/assets/b2301c2d-3763-48db-a270-5b316f288752"/></td>
    <td></td>
  </tr>
</table>

## Getting started

1. Download the latest release corresponding to your platform
2. Put Majora's Mask 3D (USA 1.1) in the same folder as the application
3. Run the app. First launch silently checks the game and prepares its files automatically.

**Press F1 to open settings and adjust camera zoom, HUD layout, controls, audio, graphics, restorations and difficulty.**


## Updating

1. Close DekUI and back up your entire `user` folder.
2. Extract the complete new release into a separate writable folder.
3. Copy your existing `user` folder and game file beside the new application.
4. Launch the new version.

Saves, HUD settings, graphics preferences, custom textures, and unrelated mods are preserved. On first launch, recognized older menu layouts update automatically with backups; customized layouts are kept.

If the upgrade fails or is interrupted, the game will not open; relaunch to retry. Resource backups are retained under `user/setup/resource-upgrade-backup-*` after success, or `user/setup/resource-upgrade-pending/backup` while an upgrade is pending. Do not delete your `user` folder or these backups to troubleshoot.

## Troubleshooting

#### On Mac, it doesn't detect my .3ds or .cci file even though they're beside the app
Try this:

1. Close DekUI and open Terminal.
2. Type xattr -dr com.apple.quarantine , including the trailing space.
3. Drag Dekui.app into Terminal, then press Return.
4. Reopen DekUI.

Dragging it into the terminal will give the full path of it. It should work from there on out.

#### I get an error saying the main partition is encrypted despite having the right version of the game

Please use another legally acquired file of the same USA Version 1.1 .cci or .3ds. Some don't work for some reason.

## Reporting issues

Found a bug or crash? Please report it at:
https://github.com/tgacquin/mm3d-dekui/issues

Include your version, operating system, and steps to reproduce the issue.

## Note about this project

AI was used extensively for the purpose of identifying each element the game renders and allowing repositioning, scaling, and transforming of them from the bottom screen to the top screen. This project otherwise would not have been feasible for me to do as the game is not yet decompiled and changes have to be done at the assembly level. However, all repositioning/scaling of the elements for a clean interface was done by me and required a lot of forethought in terms of
how the layouts should look. For those unwilling to try this project because of this, please play the recompilation which is also an excellent way to enjoy this game.

Thank you to leoetlino for his work on Project Restoration!
