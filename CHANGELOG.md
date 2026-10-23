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
- **New difficulty options**
  - **Fast Time**
    - Use the faster first-cycle clock speed for all cycles
  - **Time Penalty on Death**
    - Dying advances time by six hours.
- **Cutscene FOV slider**
  - Adjustable from 45° to 70°. Values above 49° are not recommended, as it may reveal missing geometry.
- **Disable Fog**
  - Removes atmospheric fog effects. Not recommended, as fog is used for atmospheric effect.

### Improved

- &nbsp;&nbsp;&nbsp;&nbsp;**Extensive 60 FPS interpolation improvements**
  - Improved animations across characters, bosses, objects, particles, environmental effects, cutscenes, HUD elements, and menus.
  - Fixed numerous previously missed animations and visual artifacts.
  - Expanded particle interpolation, particularly in CPU mode.

  <details>
    
  <summary><b>&nbsp;Detailed interpolation improvements</b></summary>
  
  > &nbsp;&nbsp;&nbsp;&nbsp;**Actors, Bosses & Objects**
  >
  > - Postman's cap and bag, Carpenter's carried wood, and Milk Road worker's pickaxe.
  > - Tingle's balloon, Madame Aroma's mesh, and Business Scrub animations.
  > - Captain Keeta's head, Dodongo's body materials, and Gabora's head.
  > - Twinmold's body segments and tails.
  > - Giant Link's skeleton, hands, and arms.
  > - Majora's Mask's hanging strands and Majora's Wrath's whip and tentacle chains.
  > - The four giants' previously missed leg meshes.
  > - Gyorg's smaller fish.

  > &nbsp;&nbsp;&nbsp;&nbsp;**Particles & Environmental Effects**
  >
  > - Falling water droplets, ripples, bubbles, and flowing water.
  > - Waterfall textures, foam, runoff, and pool surfaces.
  > - Great Bay Temple's pipes, water jets, and water-wheel effects.
  > - Gyorg's water deformation and splash effects.
  > - Leaves, snow, moths, dust, sparks, and explosions.

  > &nbsp;&nbsp;&nbsp;&nbsp;**Songs, Cutscenes & Screen Effects**
  >
  > - Epona's Song scrolling effects.
  > - Song of Time's falling sequence.
  > - Lens distortion during time-manipulation songs.
  > - Moon approach effects and swirling purple layers.

  > &nbsp;&nbsp;&nbsp;&nbsp;**HUD, Textboxes & Menus**
  >
  > - Textbox opening and closing animations.
  > - Magic-meter and heart animations.
  > - Location banners and credits scrolling.
  > - Items, Masks, Gear, and world-map transitions.
  > - File-select and confirmation-window animations.

  </details>

- **Menu and interface improvements**
  - Moved the Moon's mask-selection interface onto the main screen, originally left out as an oversight.
  - Improved file-select transitions and file-stone entrance animations.
  - Improved Schedule and Event Notes scrollbar coverage and spacing.

- **Pictograph handling**
  - The "A Choose" prompt now becomes fully opaque when a photo is available to view.

### Fixed
- Fixed game reverting 30fps during day closing in effect when using interpolation.
- Fixed rapid screen flashes while in first-person view in certain outdoor areas.
- Fixed Lens of Truth revealing elements at widescreen edges.
- Fixed some bushes flickering or disappearing after nearby bushes were cut.
- Fixed saved Pictograph photos incorrectly replacing the paused game background when going to Gear screen.
- Fixed newly learned character portrait highlights.
- Fixed Schedule and Event Notes layout inconsistencies, including footer spacing and new-entry badges.

### Other Changes
- Removed the interpolation work-in-progress notice from settings.

Thank you to everyone for enjoying the mod so much! I am planning to 
take an extended break from this mod as I am happy with where it's at. 
That means I will not be working on an Android or a Switch port. 

However, as this repo is open source and LLM's are readily available, I encourage 
anyone interested in ports for these devices to fork this repository 
and submit pull requests to allow the building of them. 

There will likely be one or two smaller updates on the way to continue addressing performance
improvements, crashes, and other issues users run into.

---

## 1.0.0-beta.2

### Added
- **60 FPS interpolation**
  - CPU interpolation (default) and GPU interpolation.
  - Original 30 FPS option available under Graphics.
  - **Note:** Interpolation is a work in progress. Expect visual artifacts, some textures/models still running at 30 FPS, and potential slowdowns on lower-end hardware. These issues will continue to be addressed in future updates.
- **French and Spanish language support**
  - Includes translated button prompts and messages.
- **New toggleable restoration features**
  - **Classic Saving**
    - Closely mimics the original N64 saving system.
    - Saving at an owl statue creates a temporary save.
    - Saving returns to the title screen only if a Song of Time save already exists.
    - A glowing icon appears next to your profile name to indicate a temporary save.
    - Temporary saves are consumed when loaded; permanent Song of Time saves remain.
    - Saving at feather statues is disabled while Classic Saving is enabled.
    - Avoid repeatedly toggling this feature to prevent unexpected behavior.
    - **Default:** Project Restoration saving behavior.
  - **Classic Song of Double Time**
    - Mimics N64 behavior by advancing time only to dawn or night.
  - **Bombers Don't Approach Automatically**
    - Prevents Bombers from automatically running up to you.
  - **Activate Owl Statues with a Sword**
    - Owl statues can only be activated by striking them.
    - Skips Tatl's explanation in Clock Town and automatic activation.
- **Draw-distance sliders**
  - Adjustable draw distance for NPCs and other distant objects.
- **New Quality-of-Life (QoL) tab**
  - Disable Tatl interruptions.
  - Disable low-health beeping.
- **New difficulty options**
  - 3× prices and fees.
  - Double defense (reduces damage by 1/3 instead of 1/2).
- **Customizable HUD elements**
  - Minimap opacity.
  - Location banner positioning.
- **Apple Silicon macOS support**
- **Configurable hotkeys**
  - Fullscreen.
  - Custom textures.

### Improved
- Reduced CPU interpolation overhead and frame-presentation stalls.
- **Reworked Majora-themed sidebars**
  - "Select an Item" no longer uses a background.
  - "Songs" now uses a cleaner, semi-transparent background.
- Menus now adapt to screen shrinking at the end of Days 1 and 2.
- Ocarina prompts remain visible while the song list is open.
- Added a fading backdrop behind the Song of Double Time selector.
- Added an F1 hint at startup to help users locate the settings menu.
- Fullscreen/windowed preference is now remembered.
- Pressing Esc exits fullscreen.

### Fixed
- Removed unused editor profiles accidentally included in previous builds.
- Disabled automatic editor snapshots in release builds.
- Reduced cutscene FOV by 2 degrees to prevent missing geometry from being visible.
- Fixed gyro input failing after controller reconnection.
- Fixed Gear-screen issues, including particles and rain continuing to move while paused.
- Fixed controller mapping incorrectly detecting stick movement when assigning buttons.

### Coming Soon
- Further interpolation optimizations and improvements.
- Android support.

---

## 1.0.0-beta.1 (2026-10-02)

- Initial release.
