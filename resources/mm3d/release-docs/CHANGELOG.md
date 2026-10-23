# Changelog

## 1.1.0

### Added

- **ZL and ZR for item slots I and II**
  - Enable **Use stick-click controls** under Controls.
  - **Right-stick click:** enter first-person view or use the Pictograph Box.
  - **Left-stick click:** toggle the minimap.
  - **ZL:** equip and use Item I.
  - **ZR:** equip and use Item II.

- **Fan translation support**
  - Import a compatible translation pack under **Controls → Fan translation**.
  - Made primarily to allow for the Portuguese translation.

- **Widescreen + HD Pictograph photos**
  - Pictograph photos are now taken in widescreen and at the resolution of the window.

### Improved

- &nbsp;&nbsp;&nbsp;&nbsp;**Numerous 60 FPS interpolation improvements**
  - Expanded coverage for previously missed actor parts, moving objects, water, and particle effects.
  - Brought additional CPU particle-interpolation coverage to GPU mode.
  - Improved recovery from interpolation interruptions when entering water.

  <details>

  <summary><b>&nbsp;Detailed interpolation improvements</b></summary>

  > &nbsp;&nbsp;&nbsp;&nbsp;**Actors, Bosses & Objects**
  >
  > - Horseback bowstring motion, including firing, and Bunny Hood ears.
  > - Burrowed Deku Link's protruding head.
  > - Additional sliding doors in Spider House, Ikana, Pirate Fortress, and Great Bay Temple.
  > - Marine Research Lab fish and Bombchus.
  > - Freezard shrinking during defeat and Snowhead's rising pillar ice sections.
  > - Great Bay Temple's spinning machinery.
  > - Wart's body, eye, and bubbles.
  > - Gekko's small green jelly bubbles and combined jelly.
  > - Deku Princess's eyes following her head and Koume's target.
  > - Cow tails and the ghost balloon.
  > - Eyegore's previously missed meshes.
  > - Giant Link's hands when holding a boulder.
  > - Red and blue Twinmold during punching and swinging.

  > &nbsp;&nbsp;&nbsp;&nbsp;**Particles & Environmental Effects**
  >
  > - More waterfall effects in Great Bay and Zora Hall.
  > - Swimming Zora bubbles and popped-bubble particles.
  > - Mirror-shield reflected light and its animated glow.
  > - Stone Tower's fire rings and updrafts.
  > - Garo Master's sword flames.

  > &nbsp;&nbsp;&nbsp;&nbsp;**GPU Interpolation**
  >
  > - Expanded coverage for bubbles, ice particles, sparks, sword trails, shockwaves, and Garo sword flames.
  > - Added missing handling for fireworks, chest particles, fire impacts, droplets, ripples, and splashes.
  > - Expanded coverage for snow, forest particles, leaves, debris, moths, and torch animation.

  </details>

- **Audio modes**
  - Replaced the previous choices with **Balanced** and **Accurate HLE**.
  - **Balanced** corrects only incorect emulator sounds while maintaining the crispness of standard HLE audio.
  - **Accurate HLE** applies the broader audio-accuracy behavior, can sound more muffled.


### Fixed

- Fixed annoying switches to 30fps when underwater which were especially prevalent in the Great Bay Temple
- Fixed a softlock when striking an owl statue with a Deku Stick while strike activation is enabled.
- Fixed black screen on start up caused by a presentation deadlock when interpolation was on.
- Fixed a GPU-specific interpolation rejection during the day-closing transition.
- Fixed the Gear Pictograph retaining its old centered position after an update.
- Fixed the Moon mask-selection description box retaining its old centered position after an update.

## 1.0.0

### Added

- Optional “Fast time” difficulty setting, disabled by default. Uses the first-cycle clock rate (50% faster than normal) and speeds up time under Inverted Song of Time while keeping NPC schedules synchronized. Song of Double Time is unchanged.

- Optional “Dying moves time forward 6 hours” difficulty setting, disabled by default. Continuing after death advances the clock and updates the day when crossing dawn; reaching or passing the final dawn triggers the world ending. Fairy revivals, pit falls and deaths on the Moon are unaffected.

### Changed

- Expanded interpolation coverage for actor attachments, held items, particles, water effects, textboxes, inventory screens, file select, map zoom, and HUD animations. Coverage remains incomplete; some effects still animate at 30fps.
- Select Item retains its native timing with interpolated opening and closing animations.
- Improved notebook scrollbars and overlays, mask selection layout, and dawn/dusk sounds when advancing time after death.

## 1.0.0-beta.2

### Added

- 60fps CPU interpolation (default) and GPU interpolation, with an Original 30fps option under Graphics.
  Interpolation is still a work in progress; expect artifacts and some textures/models running at 30fps.
- New toggleable restoration features:
  - **Classic saving:** Saving that closely mimics the N64 original. Saving at an owl statue creates a temporary
    save. Saving returns to the title screen only if a save from a Song of Time reset already exists.
    A glowing icon will be visible next to your profile name indicating
    the temporary save. The temporary save is consumed when loaded; the permanent Song of Time save remains.
    You cannot save at feather statues while Classic Saving is enabled.
    Avoid switching this on and off to avoid unexpected behavior. The default is Project Restoration behavior.
  - **Classic Song of Double Time:** Mimics N64 behavior of advancing time to only dawn or night.
  - **Bombers don't approach automatically:** Stops Bombers from running up to you.
  - **Activate owl statues with a sword:** Owl statues can only be activated by hitting
    them. Skips Tatl telling you about the statue in Clock Town and automatic activation.
- Draw-distance sliders for NPCs and other distant objects.
- New QoL tab to disable Tatl interruptions and low health beeping.
- New 3x prices and fees difficulty option.
- New double defense difficulty option (reduces damage by 1/3 instead of 1/2).
- New modifiable HUD elements (minimap opacity and location banner positioning).
- Apple Silicon macOS support.
- French and Spanish language support that includes the new button prompts and messages.
- Configurable hotkeys for fullscreen and custom textures.

### Improved

- Reduced CPU interpolation overhead and frame-presentation stalls.
- Reworked the brown Majora-themed sidebar on "Select an item" and "Songs" screens. Select an item
  now doesn't use a background and the song list uses a cleaner semi-transparent background.
- Menus now adapt to screen shrinking at the end of Days 1 and 2.
- Ocarina prompts now remain visible while the song list is open.
- Added a fading backdrop behind the Song of Double Time selector.
- Added F1 hint at startup so users know how to access the settings menu.
- Fullscreen/windowed preference is now remembered; Esc exits fullscreen.

### Fixed

- Removed accidentally left in unused editor profiles and stopped automatic editor snapshots in release builds.
- Reduced cutscene FOV by 2 degrees to prevent missing geometry being visible.
- Fixed gyro input failing after controller reconnection.
- Fixed Gear-screen issues, including particles and rain still moving while paused.
- Fixed controller mapping incorrectly detecting stick movement when assigning buttons.

Android support is planned next...

## 1.0.0-beta.1 (2026-10-02)

- Initial release.
