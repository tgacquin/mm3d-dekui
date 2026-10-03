# Dekui

This is a mod to make Majora's Mask 3D fully functional on one screen. In fact, the bottom screen isn't even accessible. 

Every element that could only be interacted with through the bottom screen has now been brought to the top. Previously large HUD elements have also been adapted
in an effort to bring it in line with the original game.

A custom fork of the Azahar emulator is used.

## Features

- Gear, Masks, Items, and Map accessible all through one screen with item models and descriptions preserved
- Full Bomber's Notebook functionality on one screen
- Song menu, song of double time, and give item menus accessible on one screen
- File select and options on one screen
- On-screen minimap
- Project Restoration features (https://restoration.zora.re/)
- Redesigned HUD elements (smaller and wider textboxes, thicker magic bar) meant to emulate a console-like experience
- HUD scaling, positioning, spacing, and visibility controls
- Difficulty options (damage multiplier, no heart, rupee, or fairy drops, and experimental doubled purchase prices)
- Widescreen, increased FOV, and slider to adjust camera distance
- All references to the bottom screen and home button have been removed
- Correct rupee sounds for HLE audio (for the first time through a 3DS emulator!)
- Pause menu background image adapts to resolution (3DS/emulators default to 240p)
- Custom texture support
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
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 35 30 PM" src="https://github.com/user-attachments/assets/c92d6580-d7a7-467f-9484-2da12cb2f242" /></td>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 24 57 PM" src="https://github.com/user-attachments/assets/a86a93a6-683f-4d57-81f0-09e11770c374" /></td>
  </tr>
  <tr>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 26 27 PM" src="https://github.com/user-attachments/assets/a98b6072-cf63-4340-8103-a65139892c8f" /></td>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 27 38 PM" src="https://github.com/user-attachments/assets/168108f2-0b24-4d58-8428-b6c44977741e" /></td>
  </tr>
  <tr>
    <td><img width="350" alt="Screenshot 2026-10-02 at 12 42 30 PM" src="https://github.com/user-attachments/assets/764651d2-77ca-4211-9931-54f547f80b03" /></td>
    <td></td>
  </tr>
</table>

## Planned features

- Remove every references to the bottom screen and replace with correct buttons
- More difficulty options
- Further restoration features that are also toggleable
- 60fps
- Further UI polishing/redesign
- Bug fixing

## Getting started

1. Download the latest release corresponding to your platform
2. Put Majora's Mask 3D (USA 1.1) in the same folder as the application
3. Run the app. First launch silently checks the game and prepares its files automatically.

**Press F1 to open settings and adjust camera zoom, HUD layout, controls, audio, graphics, and difficulty. Camera zoom is under Graphics.**


## Note about this project

AI was used extensively for the purpose of identifying each element the game renders and allowing repositioning, scaling, and transforming of them from the bottom screen to the top screen. This would not have been feasible for me to do as the game is not yet decompiled and changes have to be done at the assembly level. If this bothers you, the recompilation of the original is still a great way to play this game.

This project is also currently in beta and likely has many bugs/crashes I've yet to see. Make sure you save often.

Because this project uses the Azahar emulator, expect shader stuttering. These should subside within an hour of playing.

Thank you to leoetlino for his work on Project Restoration!
