# Frame interpolation

Interpolation preserves the native game update rate and adds intermediate rendered
frames. In **F1 > Graphics > Frame rate**, the options are:

- **Original**: the original game's 30fps.
- **60FPS CPU interpolation**: the default for new installations.
- **60FPS GPU interpolation**: uses hardware vertex shaders.

Both interpolation options use Vulkan. CPU interpolation processes vertices on the
CPU; it is not the software renderer. Selecting CPU interpolation disables hardware
shaders; selecting GPU interpolation enables them. Enabling either option enables
VSync. Coverage remains incomplete: some models and effects still animate at 30fps.
Performance depends on the scene, resolution and device.

## Implementation map

Paths below are relative to the repository root.

| Area | Files |
| --- | --- |
| Controls and defaults | `src/mm3d/overlay.cpp`, `src/mm3d/settings.cpp`, `src/mm3d/settings.h` |
| Camera midpoint | `src/mm3d/camera_midpoint.h` |
| Message window opening/closing | `src/mm3d/message_interpolation.h` |
| Magic meter refill/drain width | `src/mm3d/magic_meter_interpolation.h` |
| Full-heart HUD pulse | `src/mm3d/heart_interpolation.h` |
| Lens of Truth masks and cover | `src/mm3d/lens_interpolation.h` |
| Inventory menu motion and fades | `src/mm3d/inventory_interpolation.h` |
| Actor pose capture and interpolation | `src/mm3d/link_pose_capture.cpp`, `src/mm3d/link_interpolation.h` |
| Gyorg splash layers and water deformation | `src/mm3d/water_model_interpolation.h`, `src/mm3d/splash_interpolation.h`, `src/mm3d/link_interpolation.h` |
| Great Bay pipe flow, wheel runoff and water jets | `src/mm3d/great_bay_water_interpolation.h`, `src/mm3d/song_effect_interpolation.h`, `src/mm3d/link_interpolation.h` |
| Vertex replay | `src/video_core/renderer_vulkan/vk_rasterizer.cpp`, `src/mm3d/cpu_interpolation.h` |
| Presentation | `src/mm3d/prompt_layer.cpp`, `src/video_core/renderer_vulkan/renderer_vulkan.cpp` |
| Guarded replay scheduling | `src/video_core/renderer_vulkan/vk_scheduler.cpp` |
| Visibility depth | `src/mm3d/visibility_depth.h`, `src/video_core/renderer_vulkan/vk_rasterizer_cache.cpp`, `src/video_core/renderer_vulkan/vk_texture_runtime.cpp` |
| Performance capture | `src/mm3d/interpolation_performance.h`, `src/mm3d/interpolation_timeline.h` |

## Correctness constraints

Intermediate replay must not advance guest logic or input, or mutate native game
state. Matching a shader alone is insufficient to identify an actor or object.
Preserve HUD, stencil, transparency and framebuffer feedback ordering.

Gyorg captures `song-effect-1792196347019`, `song-effect-1792196326827` and
`song-effect-1792196300757` identify six water material variants. Their model
scale, scrolling UV offsets and opacity interpolate together; ambiguous layers,
new impact centers and frame gaps remain native. Model transforms are matched in
world space and converted back before the midpoint camera is applied. The CPU
path also replays the captured splash strip and blends successive atlas cells
with two additive draws, preserving the original UV cells.
Skeleton capture `link-pose-1792196602885` identifies direct bone 1 of the
528-vertex `seaboss_base_01` water surface. Its resting transform overlaps arena
geometry, so ownership additionally requires its model signature and bone layout.
Offline replay produces changed midpoints for all six material variants and
resolves all 42 eligible water-surface palette slots in the skeleton capture.
These checks do not replace in-game verification of the landing splash and wobble.

Skeleton capture `link-pose-1792197783726` identifies the eight-bone `minibus_b`
fish in `zelda2_boss03`. Its 732-vertex body starts with equivalent bones;
midpoint consensus now resolves all 45 eligible body slots while retaining all
75 fin slots. The four-bone `water_bomb` in the same capture is a separate model.
Capture `link-pose-1792198043334` identifies Great Bay's 36-vertex sliding shutter
(`m2_KON_shuter1_`). Its tiny bind offset confused direct versus skin matching.
The verified direct root now resolves all 48 eligible shutter draws and leaves
a stationary shutter at its native transform.

Great Bay captures `song-effect-1792198285059` and `link-pose-1792198262962`
identify ten scrolling-water material families, including the opaque pipe flow,
wheel runoff and multi-layer water jets. Match active UV bases, mesh sizes,
texture masks, complete combiner configuration and blend/depth state. All active
texture offsets receive midpoints through the existing CPU/GPU replay paths;
inactive texture coordinates, opacity and native texture bindings are preserved.
Replay recognizes 2,123 water draws and produces 1,389 changed UV midpoints.
Sampling gaps and animation restarts remain native. Equivalent-bone consensus
also resolves all 141 eligible wheel-water slots and 250 jet slots; paired jet
meshes share equivalent transforms within one actor, so their outputs require
agreement rather than an arbitrary bone choice.
Visual verification is still required.

Song capture schema 7 includes effective stencil operations/masks, host clip
scales, and bounded geometry for small stencil draws. CPU captures also retain
native clip-space triangle positions before widescreen correction. This supports
inspection of Lens of Truth edge coverage separately from the purple overlay.

Capture `song-effect-1792181159058` shows the two lens stencil masks using a
horizontal host clip scale of .9375 while the purple cover uses 1. The masks
therefore leave side strips outside their coverage. The verified mask and cover
materials now bypass that compression together. Their shared orthographic model
scale also receives consecutive-frame midpoints in both CPU and GPU modes.
Replay recognizes 135 draws of each mask and 134 cover draws; all three agree
on the three captured animated midpoints. Material, stencil, projection, frame
gap and generation guards exclude unrelated draws. Live visibility and
activation/deactivation still require visual verification.

Message window interpolation recognizes the sign and regular dialogue variants of
the shared single-quad plate layout and
smooths its scale and fade in the private midpoint HUD, in both CPU and GPU modes.
It preserves native glyph timing. Matching also requires consecutive frames, the
same texture and scene generation, stable atlas coordinates and a fixed window
center. Other window styles need capture verification; this is not a general
interpolator for every 2D layout.

Capture `song-effect-1792176719361` adds a regular-dialogue variant that uses the
first atlas row with the sign-style warm fade: green/alpha stay at one and blue
tracks red by 230/255, including a small overshoot. The earlier equal-red/green
check missed its opening and most of its fade. Replay now recognizes all 42
plate draws, yielding 27 midpoints and six changed poses (previously 34, 22 and
one). Regression coverage includes opening scale, fade overshoot, closing and
rejection of unrelated color/alpha values. Native glyph timing remains unchanged;
the capture is sampled and truncated, so live visual verification is still needed.

Capture `song-effect-1792181485133` adds fixed-cream dialogue plates with tint
`{250/255, 1, 230/255, 1}`. That tint does not follow the animated warm-fade ratio.
The verified variant now recognizes all 36 plate draws, producing 24 midpoints
including six changing opening/closing poses. Tests retain rejection of
unverified tints and alpha values.

`message_layout.h` now snapshots the shared native `MessageWindow` plate branch
immediately before widget rendering. Submitted transforms and atlas coordinates
must match a snapshot before its layout/pane/geometry/message identity is used.
That identity permits bounded opening/closing transforms and fades without a
fixed tint or center requirement. Glyph timing is unchanged, and atlas changes,
ambiguous ownership, frame gaps and scene changes remain rejected. The earlier
style recognizers remain a fallback. Memory tests cover read-only traversal,
float24 rounding, arbitrary tints, changed messages and unrelated panes. The
source-backed path still needs live confirmation; the cream capture retains
36 recognized draws, 24 midpoints and six changed poses in offline replay.
Capture layout flags 256/512/1024 report native ownership, a calculated midpoint
and a submitted midpoint respectively.
The ownership walk also includes `waitIcon` and `branchArrow` by native pane
name. Their direct-color layout descriptor (`f10.w=1`) is accepted only with
matching message ownership; CPU input retention for that descriptor requires a
recent MessageWindow snapshot. Inactive f93 is preserved. Atlas-cell changes
stay native, so a sprite-frame change does not blend through neighboring icons.
Sibling ownership, bobbing, inactive uniforms and atlas changes have memory tests;
final icon appearance still needs a live check.


Capture `song-effect-1792181322723` also contains 145 full-heart HUD batches.
The pulse changes the first heart's square size around a fixed center. Narrow
atlas, material, descriptor and geometry checks admit these batches outside
menu ownership; CPU mode retains their vertex inputs. Replay produces 96 moving
midpoints. Only centered size changes interpolate; other HUD movement, health
atlas changes, gaps, scene changes and ambiguous draws stay native. The second
mixed icon batch is excluded, and no health values or texture coordinates change.

The same capture contains 30 explosion-fragment batches (478 quads) with opaque
white vertex tint, full sprite UVs, a fixed 7:6 aspect and the verified one-stage
particle material. CPU particle replay now admits that combination, matching
consecutive fragments by fixed size and reciprocal, unambiguous proximity. It
produces 20 midpoint batches with 178 changed particle poses in the sampled
capture. Spawn/despawn, distant or ambiguous matches remain native; texture
coordinates are never blended. This path currently applies to CPU interpolation.

The earlier save-resume capture `song-effect-1792181121348` contains a full-screen
white quad whose opacity advances at the native rate (for example .9686275,
.9333334, .9019608). Its transition fade is still copied without interpolating
that opacity; the textbox, heart and lens changes do not alter it.

Inventory interpolation uses frame/generation stamps from submitted Items, Masks,
Gear, dungeon-map and music-list panels. It retains CPU layout inputs and matches
complete batches by their ordered atlas cells, tint, projection and combiner.
Consecutive unambiguous batches interpolate widget transforms, font opacity and
combiner fades; texture coordinates, texture selection and guest timing stay
native. Changed text, ambiguous repeated batches, page changes and frame gaps
reject interpolation. Verified pause-texture replacements also replay into the
private midpoint target, preserving the frozen backdrop. Menu widget midpoints
can establish presentation without a moving world camera.

Magic-meter capture `song-effect-1792177988857` contains 144 single-quad green
fill draws with fixed atlas cells and material; the refill changes only f11.w.
`magic_meter_interpolation.h` recognizes that atlas/material combination and
interpolates width on consecutive unambiguous frames. The two captured
consecutive refill steps generate widths 47.65625 and 50.78125 between native
46.09375, 49.21875 and 52.34375. Sampling gaps remain native. The original border,
left anchor, height, UVs, opacity and guest magic amount are preserved. HUD
placement/scale edits, texture/framebuffer/generation changes, abrupt jumps and
duplicate fills discard history. CPU input retention requires the verified fill
atlas; both renderer paths reuse isolated layout-uniform replay. Tests cover
refill/drain, custom height, discontinuities, exclusions and CPU uniform isolation.

The presentation clock measures guest cadence independently of midpoint
availability. A missing replay still falls back to the native frame, but does
not impose another two eligible frames of warm-up when a short menu transition
recovers. Cadence uses emulated timestamps recorded at completed top-screen
buffer swaps; a host GPU wait or catch-up interval does not change the guest's
update rate. Native 60 Hz intervals, frame gaps, guest-time stalls/rewinds and
clock-source changes still invalidate cadence; incomplete or stale captures are
never paired. Host timestamps remain the fallback when guest timing is absent.
Camera-midpoint tests cover recovery, host stalls, duplicate refreshes and
switching between 30 Hz and 60 Hz. Host display pacing remains unchanged.

Capture `song-effect-1792161425062` contains 16,000 draws over approximately
16 seconds and is truncated. Replaying the recorded menu interval produced 345
changed layout batches (203 moving, 26 scaling/rotating, 156 fading; categories
overlap). This measures matching coverage, not visual correctness or every menu
animation. `mm3d_inventory_interpolation` tests ownership expiry, CPU recording,
simultaneous motion/scale/fades, unchanged UVs and conservative rejection.

The subsequent Gear capture `song-effect-1792169360440` contains 14,152 native
draws without truncation. Offline replay finds 418 changed layout batches,
including 214 with translation, but the user still reports 30 Hz transitions.
Matching native uniforms is therefore insufficient evidence of successful
presentation. Song-effect schema 5 adds bounded `layout_replays` (same sampled
frames, at most 16,000 entries) and `presentations` (all host attempts, at most
4,000 entries). The former records ownership, candidate/match/CPU-replay/submission
flags, maximum overridden-uniform delta and maximum CPU output-position delta.
The latter records eligibility status and native/pair/skip decisions. Both are
local capture metadata, active only during the existing recording window; they
do not read GPU images or add normal gameplay log output. Their enum/flag
descriptions are embedded in the exported JSON.

Capture `song-effect-1792169919147` confirms that CPU menu replay changes vertex
positions and submits the midpoint draws, and that many transition frames are
paired. Presentation records use the completed frame counter, one greater than
the corresponding draw frame. It also exposes lost matches when inherited f8
changes on closing and ambiguity between three black shadow strips with opacity
36/59/82. The cached layout shader does not read f8 through the accepted
descriptor paths; matching now ignores it while preserving its native value.
For ambiguous black batches only, a second identity includes exact opacity and
requires a shared horizontal translation with unchanged shape and vertical
placement. Same-opacity duplicates, fades to another strip, changed shape and
nonhorizontal motion remain rejected. Replaying this same capture increases
matched moving batches from 218 to 298; frame 1042 recovers from 2 to 19 changed
batches. These are offline matching results, pending an in-game visual check.

Capture `song-effect-1792170683655` shows valid CPU midpoint submissions for the
moving brown panel, banner and bottom tabs. Opening draw frames 3031 and 3162
have active midpoint status at presentation but are rejected by host cadence;
closing samples do pair. Completed-frame emulated timestamps address that
presentation gap. The sampled tab midpoints also change output vertices, so
this does not establish a separate tab matcher failure. At the captured widget
positions, the fixed minimum 40px tab travel keeps most of the short animation
below the screen. Travel now uses the actual topmost face/rim and its scale,
including the selected tab's lift, with a 2px offscreen margin. Final positions,
native transition duration and guest update rate are unchanged. Visual
confirmation of the remaining opening/tab complaint is still needed.

World-map capture `song-effect-1792171687340` has no inventory owner on any
sampled layout replay: `UpWorldMapHud` is submitted in top queue 2, outside the
original inventory queue scan. Ownership now includes that submitted layout;
generic backgrounds and titles cannot activate it. Queue scans use separate
scratch buckets so a failed/unsupported queue cannot leak entries into a later
queue. With ownership supplied, offline replay finds 477 changed batches,
including the green header, map nameplate, and both arrow/button shadow and
face layers of the bobbing navigation prompt. Its text intentionally remains
stationary in the native prompt layout. This is matching evidence; visual
verification remains necessary. CPU input retention is tested from submitted
map ownership through the shader capture gate, including deactivation.

The bottom Items/Masks/Gear tabs now advance their slide progress at 1.15 times
the menu transition's rate in both directions, clamped to the same endpoints.
This changes only the strip's travel; it does not accelerate the other panels,
the game's menu state, or the L/R prompts' bobbing period.

Capture `song-effect-1792172390544` confirms that the 1.15 tab multiplier applies
on both Gear and Masks. Masks submits changing menu geometry every 16–17ms and
correctly stays native; Gear normally presents midpoint/native pairs. Gear's
first sampled opening draw (5370) has no midpoint history, followed by paired
moving frames 5371/5372. The capture is truncated, so this is not a claim about
every transition. The captured brown panel moves 47.66px per Gear guest frame,
versus 33.36px per Masks guest frame at twice the update rate.

Gear now holds its first opening panel pose fully offscreen to establish real
history, then uses the remaining three 30 Hz intervals to reach the same final
position at the original finishing time. Travel is three quarters of the shared
Masks distance, or the Gear clearance bound if larger custom poses require it.
Closing already has three intervals and uses that same travel. For the captured
extents this gives 33.36px per displayed step in both menus. No previous geometry
is fabricated and the matcher/presentation guards remain intact. Placement is
clamped before applying travel so a large Gear background can still leave the
screen. Regression checks cover the hidden first pose, its real next-frame
midpoint, displayed displacement, endpoints, opening/closing, guest-state
preservation and unclamped offscreen travel. Visual verification is still needed.

Capture `song-effect-1792173144176` is not truncated. Visible Gear opening draws
3610–3612 and 3741–3742 contain successful CPU midpoint submissions for the brown
border/tiles, panel texture `2059e900`, and green header. Brown/panel positions
differ by 33.36px between native and midpoint geometry. Their corresponding
presentation frames (draw frame + 1) receive Pair plans. Hidden first poses
legitimately have no changed midpoint. This evidence does not verify composed
pixels or physical display spacing, and the user still reports stepped entry.
Do not infer another matcher or speed fix from that report alone.

The local “Capture moving menu frame pair” button waits three seconds, then up
to twenty seconds for a submitted inventory midpoint with horizontal motion
over one pixel. It saves one composed midpoint/native pair as `menu-pair-*.ppm`
in the log directory. This excludes stationary menus and camera-only pairs.
Readback deliberately stalls for image inspection; these captures must not be
used to measure frame pacing. Ordinary song captures remain metadata-only.

### File-select entrance

The save-summary panel is routed immediately after the file stones in the same top-screen draw queue, so stones pass behind it. Popups remain above both; unrelated queue entries keep their ordering. Queue capacity and write destinations are validated before routing changes are applied.

The native `in_menu_fileselect_ani` moves the three plates from X334 to X14
over frames 0..6, 2..8 and 4..10 at 30 Hz. Scaling that original 320px travel
with the relocated plates exposes them inside the wider top screen at arrival.
The entrance now cancels each plate's native X displacement and adds its own
right-side travel, independent of plate scale/placement. Its first submitted
pose seeds an offscreen position, then each plate keeps its native finish time.
The saved `file_select_entrance_ease` exponent ranges from 1 (linear) to 3,
default 2; it changes the approach curve without extending the native menu state.
Three independently stamped slide deltas feed the existing file-select midpoint
matcher, including repeated glyphs. Selection/detail and Options transitions
retain their separate motion.

The file cursor stays hidden until the last plate settles. A successfully routed
entrance also blocks pad, analog and touch input; completion or missing ownership
clears the gate, with a 250ms expiry if layout submission stops. Tests cover
offscreen embedded plate geometry and restoration, first submitted frame,
staggered endpoints, deceleration, ease settings, motion stamps, cursor hiding,
input release/expiry and settings persistence. Visual verification requires
entering file select again in the rebuilt app.

### World-map zoom camera

Skeleton capture `link-pose-1792173567924` contains a 102-bone map model with
coincident skin transforms. Song capture `song-effect-1792173639021` distinguishes
camera motion from skeletal motion: multiplying the captured camera by the
map model leaves the same 0.04-scale world transform (maximum deviation 0.0000371).
All 6,444 perspective draws share one camera/lens per captured frame. The map
already receives paired presentations, but its private camera does not match the
gameplay camera, so the world-camera gate previously replayed its native zoom.

`world_map_interpolation.h` uses a separate camera history, authorized only by a
currently submitted `UpWorldMapHud` in queue 2 and the captured projection shape,
aspect and clip planes. The existing bounded camera/lens interpolation applies
one halfway view to current map vertices, shared by CPU and GPU paths. Skeleton
overrides are skipped for this camera; adding shared-bone matching would not
address the camera rejection and is unnecessary for this static model. Owner,
generation and frame discontinuities reset history. Unrelated perspective draws
while the map owns the menu leave its camera history intact. Other inventory pages and
stale/closed map owners cannot authorize the new path.

Offline replay recognizes all 6,444 perspective draws, generating 4,284 midpoint
projections, with 1,152 changed draws. All 32 moving transitions with consecutive
captured frames produce changed midpoint projections. Sampling gaps seed fresh
history. Regression tests cover the halfway view/lens on native view-space
geometry, repeated materials, zoom-out, comparison mode, owner/generation/gap
resets, closure/reopening and unrelated projections. In-game smoothness still
needs verification with the rebuilt app.

Follow-up capture `song-effect-1792176229587` recorded zero map replay entries:
its depth scale is 1.000393748 instead of 1.000390768, outside the original
single-capture tolerance. Recognition now accepts the narrow captured far-plane
range while requiring the shared near-plane relationship, map aspect/shape and
submitted map owner. Offline replay recognizes all 6,433 perspective draws,
produces 4,248 midpoint projections (684 changed), and covers all 19 moving
transitions with consecutive captured frames. The earlier capture still passes.
Regression tests cover the second clip range and reject unrelated near/far
planes. This verifies capture replay, not live visual smoothness.

`burst_particle_interpolation.h` handles the captured Deku flower dust, green
Skulltula blood and purple death flames through CPU vertex replay. It preserves
native inputs and atlas tile choices while interpolating motion, scale and color;
the flames also interpolate their linked scrolling UV layers. Dust age groups
and blood-cloud expansion resolve dense bursts without treating submission order
as particle identity. Ambiguous matches, frame gaps and scene changes stay native.
The actor matcher separately recognizes Business Scrub shared bones and the
verified parent bones for the Milk Road pickaxe and Real Bombchu tail bomb.

The October 23 lizard-enemy hit capture adds blue blood to the same burst decoder
in both CPU and GPU modes. It requires the captured sprite combiner, square
geometry, full UVs and the exact lifetime-dependent blue corner gradient. Green
and blue bursts have separate kinds; a new blue burst cannot inherit a fading
burst's motion. All 36 captured batches decode (972 droplets), producing 24
midpoint batches and 578 moving droplet midpoints. Tests cover the blue gradient,
material rejection, motion and fading, color isolation, lifetime resets and
27-droplet correspondence after draw-order changes. Live visual confirmation
is still required.

`spark_interpolation.h` handles Real Bombchu fuse stars in CPU mode. Overlapping
submissions share an immutable previous-frame pool and deduplicate native stars
for the next frame. Geometry, doubled UVs, tint and lifetime fade constrain
matching; gray particles in the same batch remain native.
The tree-impact capture `song-effect-1792182638887` already passes those spark
material/geometry gates: 82 recognized batches, 52 midpoint batches and 178
changed particle poses out of 186 in matched batches. The earlier forest,
fire-impact and firework rules do not intercept them. No broader particle rule
was added. These counts verify offline matching, not final presentation through
the moon's motion-blur effect.

Epona's single-texture song layers match only the active UV basis and captured
scroll rates. Disabled texture bindings and UV matrices cannot split history or
reject a valid midpoint; their values remain untouched. The shared uniform path
supports both CPU and GPU modes.

Song of Soaring's `wing_warp` model uses a verified ten-bone palette for each
864-vertex wing draw. Its reconstructed bind transforms differ slightly from the
guest's along long bone chains. A model-specific matcher validates the complete
palette and carries that small difference into the midpoint, preserving the
native pose while leaving generic actor matching tolerances unchanged.

The removed Deku mask (`mask_deknuts`, draws 30/126/402) and the captured
hinged door (draw 180) have equivalent bones. Their verified four-bone models
use shared-bone resolution, with normal instance isolation and divergent
midpoint rejection. This does not admit other transformation masks blindly.

Song of Time's lost rupees and Deku stick use verified equivalent-bone models
and captured draw sizes. Their shared transforms interpolate only when the
midpoints agree, with separate history per item instance. Actor histories and
candidate caches allow up to 256 instances so the 128 clocks do not crowd out
Link and the lost items; existing frame expiry still applies.

Standing `grass05_model` bushes reuse compacted model slots after cutting.
A world-root displacement of one unit or more resets that model's history,
preventing interpolation between different bushes while retaining camera motion
and rooted sway. Flying grass fragments retain the ordinary movement limit.

CPU and GPU interpolation share guarded batching to reduce render-target switches.
Retain captured resources until the appropriate drain, and drain before unknown
resource dependencies. Reordering independent draws does not authorize deferring
work past resource mutations.

The visibility-depth cache applies only to the guarded visibility routine in the
supported game. Other depth reads remain exact. A cached result must be no more
than one game frame and 100 milliseconds old, and must match the surface, scene
and generation. Missing, invalid or stale results fall back to exact reads.
This reuse can delay a visibility response. Background refresh follows a verified
query and stops in Original mode or when invalidated.

For diagnostic comparisons, the current opt-out switches are:

- `DEKUI_ASYNC_VISIBILITY_DEPTH=0`: disable asynchronous visibility-depth reuse.
- `DEKUI_REFRESH_VISIBILITY_DEPTH=0`: disable background refresh.
- `DEKUI_DEPTH_READBACK_PRIORITY=0`: disable depth-readback prioritization.

## Capturing a problem

See [the diagnostics instructions](deck/DIAGNOSTICS.md) for packaged builds.
Enable `DEKUI_DIAGNOSTICS=1` and use the F1 30-second performance capture. Compare
Original, CPU interpolation and GPU interpolation in the same scene with the same
resolution, camera movement and draw distance.

The developer editor's Camera tab also provides actor skeleton captures (10 seconds)
and song effect captures (20 seconds) for identifying missing animation coverage.

CPU timing scopes can overlap. GPU submission spans can include waits. Callback
intervals do not measure physical display scanout. Synthetic benchmark speedups
are not gameplay FPS predictions.

## Validation

Run the relevant focused tests from the repository root:

```sh
ctest --test-dir build-macos --output-on-failure -R 'mm3d_(camera_midpoint|cpu_vertex_replay|visibility_depth|interpolation_timeline|camera_writer_jit)'
```

With a built Vulkan fixture and access to a GPU, run:

```sh
./build-macos/bin/Release/mm3d_vulkan_fixture --pass-switching
./build-macos/bin/Release/mm3d_vulkan_fixture --visibility-depth
./build-macos/bin/Release/mm3d_vulkan_fixture --depth-readback --performance
./build-macos/bin/Release/mm3d_vulkan_fixture --skipped-frames
```

Use the corresponding build directory on other platforms. Fixtures do not replace
manual checks of visuals, audio speed, transitions and resource-heavy effects.
Report the actual hardware and settings tested; a passing Mac run does not establish
Steam Deck or Windows performance.

### Sword trails, Dodongo limbs and explosion rings

The CPU replay path recognizes the captured white sword ribbon by its paired
edges, sequential UVs, opacity profile and complete strip connectivity. It
resamples the previous ribbon when segment counts change, interpolates position
and opacity in world space, then returns to the current draw's local coordinates.
UVs, native inputs and connector indices are preserved. New swings, distant
ribbons, frame gaps and conflicting same-frame submissions remain native.
This path currently applies to CPU interpolation.

Dodongo's verified 18-bone model uses 756- and 408-vertex palettes whose bind
reconstruction differs by about 1.5e-5 in the basis. Complete ordered palette
validation permits a 2e-5 basis tolerance only for these meshes, retains the
original position tolerance, and carries the native bind correction through the
hierarchy midpoint. Coincident actors remain ambiguous. This uses the shared
CPU/GPU skeleton path.

The Real Bombchu explosion capture contains ground, upright and expanding white
rings. CPU replay interpolates their growing quads and fading opacity using the
verified material combinations, doubled UVs and pale vertex colors. Matching
requires a stationary center, bounded expansion/fade and reciprocal unique
matches; particle compaction and concentric rings cannot borrow arbitrary history.
No texture address is hardcoded. This does not change explosion sprite frames.

Capture replay verifies geometry coverage, not final on-screen appearance.
Regression checks are included in `mm3d_camera_midpoint_tests`; visual testing
should cover repeated swings, camera movement, Dodongo walking, explosions and
scene changes.

Underwater bubbles in CPU replay use the captured lit material, small square
geometry and two horizontal atlas cells. Reciprocal matching uses position, size
and lighting color, independent of compacted particle indices. Movement, size and
color interpolate; sprite-cell selection stays native. Crowded ambiguous matches,
new distant particles and frame gaps retain the native draw. The capture replay
covered 610 of 621 bubbles in eligible frame pairs; this is not a guarantee for
unseen bubble variants or a replacement for an in-game visual check.

Captain Keeta's verified 20-bone `stal_keeta` skeleton shares direct head
transforms between bones 9 and 10 and root transforms between bones 0 and 1.
The shared CPU/GPU skeleton path accepts equivalent transforms for its captured
12-, 42-, 81-, 564- and 768-vertex meshes only when their midpoint results agree.
Distinct coincident actors and divergent bone histories remain ambiguous. Capture
replay changed the head meshes from zero to 36 interpolated draws out of 55;
the remaining samples seed history across capture gaps. The captured Stalchild's
moving bones already interpolate; no Stalchild-specific change was established.

Skull Kid's tossed ocarina (`stk_okarina`, signature `89c1c57e8d857e3b`) has
two equivalent bones and a 384-vertex mesh. The shared CPU/GPU skeleton path
accepts their transform only when the midpoint results agree. This interpolates
translation and rotation while preserving model scale and camera motion; distinct
coincident actors, divergent histories and frame gaps retain the native draw.
Capture replay produced 59 changed midpoint draws out of 89 (previously zero),
with 30 history seeds across capture gaps. In-game visual verification is still
required, including catching the ocarina after each toss.

### Moon effects and Select Item transitions

The shared CPU/GPU scrolling-UV path covers the three `moon_fall_model` layers
(720/288/336 vertices) and the two `moon_start_anm` purple layers (1440 vertices
each). Recognition checks active texture count, complete UV bases and linked
phase offsets. The former advances at 1/160 of a texture cycle per guest frame;
the latter advances at 1/120. Midpoints follow the short path at cycle wraps,
including the third texture; inactive UV rows remain unchanged. The purple
layers use additive and reverse-subtractive blending, so blend equation is part
of the history key. Other reverse-subtractive effects remain excluded.
Capture replay covered all eligible pairs: 57 per original moon layer and 110
per purple layer in the latest capture. These are geometry/uniform checks, not
in-game visual verification.

Select Item retains its original six-frame linear entry/exit timing. The custom
duration and easing controls and saved settings have been removed. Its stamped
motion history and CPU/GPU midpoint replay remain enabled, including both slide
directions. The earlier full-size/full-opacity prompt treatment is retained.

The moon's `MenuMaskPresen` has independent Select Mask controls for its panel,
title, description and Choose/Cancel prompts, with the stamped six-frame slide
in both directions. Its 24
icons use owner+0x50, and its animation player is at owner+0x30 (different from
Give Item). The six-column grid and both selection cursors move together; the
item-only right strip is not applied. Only the mask panel, title and controls
are routed to the top screen; the bottom backdrop is omitted. Legacy settings
seed the mask panel/title/prompts from Give Item and preserve the description's
previous dialogue pose. New `mask_present_*` keys save subsequent edits separately.
An exterior shadow surrounds the union of the three mask-panel texture pieces
without darkening internal seams. Select Mask samples its quadratic opacity
falloff in 24 thin exterior rings instead of three broad bands and coarse corner
cells. The rings contain 96 disjoint rectangles within the existing scratch
allocation; each corner belongs to only one rectangle. Tests check opacity,
edge joins, scaling, routing and restoration of guest memory. Live visual
verification of the smoother falloff is still needed.
Select Mask uses a broader, darker halo weighted toward the bottom for visual
separation from the world at smaller editor scales. The title's delayed visibility and scale/fade tracks
are overridden during calculation/drawing and restored afterward. Guest-memory
tests cover routing, all 24 icons, cursors, prompts, both transitions and malformed
layouts; in-game placement still needs visual verification.

### Majora's strands and detachment fragments

The verified ten-bone `majora_mask_hai` chain (`5a77df48ac4918b7`, 240 vertices)
uses scale-compensated joints. Recovering child transforms through scaled parent
inverses introduced shear and caused every child to fall back to its native pose.
This chain now interpolates in unscaled parent frames and recomposes along the
interpolated hierarchy. Other skeletons retain their existing shear checks.
The captured skeleton replay now changes all ten palette slots for each of the
1,408 continuous, uniquely owned draws; gaps and ambiguous ownership stay native.

Majora's detachment debris shares the CPU four-attribute atlas-leaf format. Its
nominal six-unit squares now tolerate f24 rounding, batches support up to 128
quads, and small equal-size fragments can fall twelve units per guest frame.
Reciprocal matching, overlap rejection, frame/scene boundaries and discrete UV
cells remain intact. All eleven batches with geometry in the original capture
decode, with 62 particle midpoints across the available consecutive pairs. The
original capture omitted the largest batches; capture eligibility now includes
this attribute layout, and synthetic tests cover 100- and 128-fragment bursts.
These replay checks still require in-game visual confirmation.

Majora's Wrath (`majora_demon`, `18ccb05513137730`, 113 bones) has two more
scale-compensated chains, bones 18--60 and 67--109. Apply the unscaled-parent
treatment only to those chains, including when a body draw seeds the frame's
history first. Twelve verified whip palettes also need a small bind-translation
tolerance (.05 units); the ordinary basis tolerance remains unchanged. Validate
each whole active palette and carry its native bind correction into the midpoint.
Replay recognizes all 1,008 captured whip draws, with 55 continuous interpolated
frames per mesh; the remaining frame pairs retain the root-motion/cut guards.

The four giants (`giant`, `369a86dcc57ca979`) already have valid joint interpolation.
Their 1,014-vertex leg mesh narrowly misses the ordinary skin-matrix match. A
seven-slot palette check admits its measured basis/translation differences
(under .00002/.05), preserving the native correction. Replay replaces all seven
slots in 108 eligible draws. Other giant materials retain the existing path,
and coincident instances are still rejected rather than selecting an arbitrary
giant. Synthetic tests cover stationary poses, both whip chains, body-first
submission, bind corrections, frame gaps, disabled interpolation and ambiguity.

### File-select sliding panels

The credits roll uses the layout shader with batches of up to thirteen 16x18
font quads, including batches spanning multiple lines and short tail batches.
The negative descriptor component references a color register; its register
number can change across batches. Match actual glyph UVs, tint, projection and
horizontal position, then require the captured upward movement (about .6172
layout units per native frame). This keeps repeated letters on different lines
separate and rejects stationary text, reflow, color changes and capture gaps.
Only the vertical midpoint changes; the native text draw remains untouched.
CPU recording admits candidate font batches, while replay applies the stricter
geometry and motion checks. GPU replay uses the same midpoint uniforms.

Replay of the credits capture recognizes all 7,038 font draws. Of 4,692 draws
with an adjacent captured frame, 4,662 produce midpoints; new/reflowed glyphs
remain native. The same recognizer admits no draws in the other four song
captures in this audit. Tests cover multiline text, repeated glyphs, batch
splits, color-register relocation, discontinuities and CPU uniform isolation.

File-select UI now contributes horizontal widget midpoints to both CPU and GPU
interpolation, including scenes without a world camera. Native file-panel
ownership is stamped by guest frame and generation. CPU vertex inputs are
retained only while that state is recent; no inactive texture binding is used
as a menu identifier.

Widgets match independently across batch splits and reordered draws using their
active atlas, projection, tint, UVs and non-translating geometry. Native panel
motion disambiguates repeated glyphs. Ambiguous matches, scale/projection changes,
large jumps, frame gaps and different panels retain native positions. History is
bounded to two frames. Uniform overrides affect only the private midpoint copy;
the native animation timing and file-select easing remain unchanged.

The latest captured file-select/Options sequence produces 307 moving midpoint
batches from the uniform matcher alone. Its older capture has no native ownership
samples or retained CPU inputs, so live eligibility and CPU replay are covered
separately by regression tests; the final appearance still needs an in-game check.

File-select cursor pulses additionally interpolate stage-zero combiner opacity
for the captured white-tint material, independently of vertex movement. The
opacity override is confined to uploaded midpoint fragment uniforms and restored
after replay. A draw must have agreeing, adjacent opacity histories for every
quad; mixed batches at full opacity, abrupt changes and ambiguous matches retain
native opacity. Native atlas cells and animation speed stay unchanged.

The SubMenuTitle green strip, lower edge and both captured title atlas variants
also interpolate vertical translation. Atlas/geometry checks exclude ordinary
option-menu text reflow. CPU replay updates f12.x; GPU replay uses the same pose.
The capture contains 73 interpolated glow draws and 15 vertical banner/title
quads, in addition to the horizontal transitions. Final appearance requires a
live visual check.

Options transitions retain stamped UI ownership through the active `MenuOption`
panel when `FileSelect` leaves the draw queue. This admits CPU inputs and banner
midpoints during both directions without carrying stale file-panel ownership.
The confirmation-button cursor uses the same verified combiner-opacity path as
the file cursor; the latest capture includes that material inside the popup.

Confirmation plates use a verified nine-slice atlas/grid. Adjacent samples
interpolate both rectangle rows and combiner opacity, including growth, overshoot
and closing. Matching retains the captured pivot (the vertical center is offset
by three quarters of the corner width), projection, aspect and owner; broken
slices, repositioning, large changes and ambiguous draws stay native. All four
eligible moving plate pairs in the latest capture receive midpoint geometry.

### Fireworks

CPU interpolation recognizes the captured firework combiner, six pastel color
combinations, doubled UV squares and depth/blend configuration. It retains fade
values up to 400/255 rather than rejecting the brighter part of their lifetime.
Other particle decoders still default to a maximum alpha of one. The geometry
matcher uses reciprocal unique matches across batched volleys, preserving size
and rotation and rejecting renewed brightness, jumps and ambiguous neighbors.
The midpoint changes vertex position/color only; atlas selection stays native.

Capture replay recognizes 367 firework draws, emits 242 midpoint draws and
changes 13,908 particle samples. Capture gaps, new particles and ambiguous matches
retain native geometry. This is CPU-path coverage, not an in-game visual result
or GPU interpolation support. Regression tests cover fade, reorder, overlapping
particles, history gaps, source alpha bounds and unrelated colors/UVs.

### Ending-capture audit and mountain smith

The five song/effect and five actor-skeleton captures were reviewed using draw
replay and locally verified model palettes. Previously handled fireworks,
file-select transitions, Majora strands/whips and giant legs were not broadened
just because they appeared again. An unmatched particle decoder is not evidence
of a missing interpolation: many of those draws are ordinary meshes, shadows,
or geometry whose motion comes from model uniforms.

Two additional NPC materials had a clear identity problem: Tingle's 30-vertex
skin mesh (`2215c979d3103929`, palette 1/3) and Madame Aroma's 144-vertex mesh
(`7900d5513a1e4e5d`, palette 15/16/24/10/11). Their first skin bone coincides
with another bone, making generic single-slot ownership ambiguous. The complete
verified palette now establishes ownership with the existing tight matrix
comparison. Native bind correction is retained. Replay covers all 52 and 130
eligible palette slots respectively; overlapping actor instances remain rejected.

A subsequent mountain-smith capture confirms that Gabora's head material
(`c4bab09e9917b4ae`, 360 vertices) was left native because bones 11/12/13
coincide. His locally verified rigid head materials (36/72/78/360 vertices)
can now establish ownership from equivalent bones, but only when their
independent midpoint results agree. Other body materials already pass the
matcher. Tests retain rejection for divergent histories, duplicate instances,
unknown materials and capture gaps.

Unresolved candidates are deliberately not patched speculatively: Cremia's
scale-compensated joints and bind differences, Tingle's deforming fourth joint,
other NPC ownership ambiguities, and an unidentified additive particle strip
need further verification. This audit does not claim visual coverage of every
animation or sprite frame in those scenes.

### Falling mountain snow

The captured snow uses CPU-submitted, camera-facing square strips with fixed
2--4.24-unit sizes, zero vertex RGB, unit UVs and a specific inverse-color
combiner with ordinary alpha blending. Match flakes by mutually unique nearby
centers, stable size, orientation and a small opacity change. Blend their
positions and fade while retaining UVs and square size during camera turns.
Spawns, ambiguous neighbors, jumps, capture gaps and repeated batches remain
native; history is bounded and keyed by active texture and generation.

The new capture decodes all 159 snow batches (3,613 flakes), producing 105
midpoint batches and 2,241 interpolated flakes across adjacent captured frames.
None of the prior five effect captures match this snow material. The CPU interpolation replay path supplies the updated geometry; GPU-mode
particle interpolation is not added by this change.
Regression tests cover material/geometry exclusions, falling motion, fading,
particle reordering, camera turns, ambiguity, resets and malformed strips.

### Winter first-person white flashing

The mountain capture submits an opaque 288-vertex sky dome, translucent sky,
then a pale full-screen background cover before scenery. First-person camera
motion offsets the sky's model translation by as much as 12.78 units. The old
sky classifier required every translation component to be within .05 units of
zero, misclassifying the sky as foreground in 75 of 166 captured frames. That
caused the following pale cover to enter the final HUD layer and obscure the
intermediate scene while native frames retained the correct order.

Sky recognition now accepts that camera-relative translation while retaining
the verified mesh count, sky clip range and rigid 320-unit basis, and additionally
requiring an untextured draw. The background remains in the world replay in
native submission order. The native draw, projection and colors are unchanged;
letterboxing and later fades retain their existing ordering. Regression tests
cover offset sky, foreground textures, clip ranges, scale changes and orthographic
geometry. An in-game check is still needed to confirm the reported flashing is
fully resolved.

### Fire-arrow impact clouds and sparks

The fire-arrow/boulder capture contains two previously unmatched additive strip
families: large, constant-sized clouds using a 4x4 animation atlas and small
signed-blue sparks using doubled UVs. Both have a specific three-stage combiner;
its constant color distinguishes the two families. The CPU interpolation path
now recognizes their material, signed vertex tint, square geometry and UV layout.

Clouds match only with stable size, small displacement/opacity changes and the
next atlas frame (including wrap). Sparks match with bounded displacement,
orientation and lifetime fade. Matching must be mutually unique, and ambiguous
particles, frame gaps, different atlases/generations and repeated batches stay
native. Midpoints blend position, size/orientation and opacity, retaining the
current atlas cell so sprites never scroll through neighboring images. The
cloud's authored sprite animation itself remains at native cadence.

Replay recognizes all 78 relevant batches (1,616 particle samples), producing
52 midpoint batches with 1,052 interpolated particles across adjacent captured
frames. No materials in the preceding five audited song captures match this
new family. Tests cover atlas wrap/discontinuities, fading, geometry exclusions,
spawn jumps, ambiguity, repeated batches, texture changes and generation resets.

### Closing-day padded scene copy

The first shrinking-border capture contains 37 consecutive solid-clear/scene-copy
pairs. The scene transfer starts 112 rows into its 512x512 RGBA8 texture, while
the draw binds the texture base. Exact-address provenance therefore rejected
the copy and forced native presentation throughout the closing-day effect.

Scene provenance now separately recognizes a successful copy from the current
512x400 RGBA8 scene into that padded region. It accepts either the complete
buffer or the observed 480x400 visible rectangle: 50 bands of 480x8 RGBA8 pixels
with 32x8-pixel gaps at both ends, preserving the 512-pixel tiled stride.
Acceptance requires the
verified orthographic projection/model, native shrink UV bounds, replacement
combiner, and an immediately preceding clear of the same target. Both CPU and
GPU interpolation use this decision. Partial/failed transfers, other gaps, different
formats or sizes, overwritten copies and frame changes remain rejected. The
alias is never added to exact-address or previous-frame feedback provenance.

The capture verifies draw ordering and material. The subsequent runtime log
records 19 rejected copies with size 768000, input/output width 15360, gaps 1024,
and UV bounds (0,0)-(478/512,399/512). The initial contiguous-only fix rejected
this valid tiled rectangle. Regression coverage now includes that observed
transfer, malformed strides, partial/failed copies, and writes into the last
tile band beyond the packed byte count. The rebuilt app still needs an in-game
border-zoom check; existing rejection/transfer logs retain the relevant guards.

The following rendered frame pair exposed a separate alignment error: the
native copy samples 478x399 of the 480x400 scene, shifting both the HUD and world
relative to midpoint replay. Applying that sampling transform to the midpoint
image predicts the observed mismatch (mean RGB error drops from 53.9 to 2.6 in
the magic-bar patch, 6.4 to 0.64 on the wall, and 23.2 to 1.4 on the buttons).
The verified normalized copy now uses a runtime fragment-shader variant to
expand its UVs to the full copied rectangle. This is shared by CPU and GPU
vertex paths and does not modify guest vertices, uniforms, or texture content.
The same verified copy bypasses a second widescreen correction without relying
on local saved draw rules. Recognition runs before the native draw; the existing
clear/copy pairing still controls midpoint eligibility. Other atlas and pause
draws reset the copy flag, and cube/shadow samplers do not receive this override.

### Map-zoom follow-up diagnostics

The reported in-game zoom remains unconfirmed despite passing camera replay.
The history now ignores unrelated world projections while map ownership remains
active, with a regression test for interleaved gameplay/map draws. The existing
capture contains no such unrelated draws, so this is not evidence that the
reported slowdown has been resolved.

Song effect capture schema 6 adds bounded `map_replays`: stamped map owner,
replay eligibility gates, midpoint calculation/submission flags, projection
change and actual CPU output-position change. Compare these with the existing
presentation records to distinguish rejection, unchanged CPU geometry, and a
presentation problem. No game data or additional logging is captured outside
an explicitly requested Song effect recording.

### Twinmold and Giant Link

Both Twinmold models share a verified 32-bone skeleton and the same ordered body
segment palettes. Their reconstructed skin matrices differ from native matrices
by up to .000294 in the basis and .063 units in translation. Model/mesh-specific
matching checks the entire active palette and preserves that small native bind
correction. The body chain cancels parent scale when recovering local motion;
this avoids rejecting the rotating, swelling segments as sheared transforms.
The capture replay now covers all 1,762 eligible Twinmold mesh draws and 5,338
active palette slots, including both models' different tail meshes.

Giant Link retains human Link's skeleton but changes individual limb proportions.
Those nonuniform parent scales made 16 animated joints fail ordinary local-pose
interpolation throughout the capture. Verify the human signature/topology and
four captured limb proportions before using unscaled parent frames. His hands
and connected arm palettes also require tightly bounded native bind correction.
All 299 captured giant poses match the form guard, and replay covers all 736
eligible mesh draws / 2,714 active slots. The collapsed shadow joint remains
native. Ordinary human Link retains the existing interpolation path.

Tests cover hierarchical halfway poses, stationary reconstruction, full palette
validation, malformed/unknown skeletons, coincident instances, disabled mode and
frame gaps. Captures are sampled and truncated; these are replay results, not
confirmation of every boss animation in-game. The skeleton path is shared by
CPU and GPU interpolation.

### Location-banner text

A submitted gameplay `Telop` layout stamps ownership separately from inventory
menus. Its paired 16x18 foreground/shadow glyphs retain their font atlas, tint and
size while their diagonal entrance, slight tilt and combiner fade interpolate.
The CPU recorder now retains its larger text batches as well; CPU and GPU use
the same private uniform overrides. Removing Telop from the draw queue or opening an inventory
menu clears ownership. Unrelated fonts, horizontal reflow, changed glyphs, frame
gaps and ambiguous batches stay native.

Replay identifies all 246 location-text draws in the latest capture and produces
62 changed midpoint batches: 20 with movement and 54 with opacity changes
(overlapping categories). Native rendering inputs are unchanged.

### Held Piece of Heart

Capture `link-pose-1792184106077` correlates the held `zelda_gi_hearts` model
(`b8b55777b90f150e`, three bones) with meshes of 180, 42 and 408 vertices.
All three bones are coincident in this animation; generic unique-bone matching
rejects them. The verified model/mesh combinations now use the existing
consensus resolver, requiring matching bones to agree at the midpoint too.
Offline replay improves from 0/96 to 96/96 eligible mesh overrides across the
three meshes (32 each). The capture is truncated. Regression checks cover scale,
rotation, camera movement, frame gaps, disabled interpolation, mesh/signature
mismatches, overlapping instances and divergent bone histories.

### Partially explored map zoom

Capture `song-effect-1792193425977` uses a map depth row of
`{0,0,1.0002501010894775,5.001250267028809}` (near 5, far about 20,000),
which the earlier 12,800-unit far-plane fingerprint rejected. A temporary second
depth band handled this capture; the model-owned map recognition below replaces
both bands. Active UpWorldMapHud ownership, the near plane, aspect, perspective
shape, frame adjacency and camera continuity remain required.
Offline replay recognizes 1,800/1,800 map draws, with 1,200 midpoint draws and
380 changing poses; none of the 38 sampled consecutive moving frames is missed.
This verifies capture matching, pending live CPU/GPU presentation confirmation.

### Notebook portrait flash alignment

Capture `song-effect-1792193513311` contains 18 solid portrait-highlight draws
whose clip scale was 1 while their textured portraits used 15/16. A saved
fullscreen draw-state rule also matched these small layout quads. The renderer
now keeps localized, axis-aligned solid layout panes in the ordinary widescreen
coordinate space, preserving the fullscreen rules for full-screen covers and
full-width banners. Both the Schedule row flash and detail flash are covered;
native timing, opacity, and portrait positions are unchanged.

The shared Events/Schedule scrollbar also now ends its thumb five units inside
the lower track cap, matching its upper inset. Previously it stopped at the
lower cap's origin, leaving an extra three-unit gap even at full coverage.
Tests cover full and partial coverage, scrolling endpoints, native animation
sizes, scale, and restoration of guest memory after drawing.

Follow-up capture `song-effect-1792194355932` includes both pages. Their track
bounds agree (Y=40.2..232.2), but Events stretches its shorter 296-unit track
to Schedule's 320 units: its thumb ends at 228.9567 rather than 229.2, and its
caps are 1.2973 units tall rather than 1.2. Events now uses the same native
356-unit track bottom as Schedule before normalization. A complete Events
Begin/End regression verifies matching final bounds and cap sizes under three
list scales, and exact guest-memory restoration; the previous code fails it.

Capture `song-effect-1792197084902` exposes a separate thumb seam: the layout
shader floors each piece's position. The strip can end at 227.99998 while the
lower cap starts at 228, leaving a whole native pixel uncovered. Of 191 complete
scrollbar samples, 58 have a gap after flooring. Extending the strip half a local
unit into each cap removes those gaps in the captured geometry without changing
the outside bounds. Both notebook pages share the correction. Tests include
fractional positions near the bottom of a 61-entry list and floored join coverage;
final appearance still requires an in-game check.


### Map zoom independent of explored-map clip range

The common landscape model recovers to a 0.04-scale identity at world origin
when multiplied by the inverse-view camera. With live UpWorldMapHud ownership,
this identifies the map camera on each frame. Subsequent materials and markers
must share that frame's camera and projection. A paused gameplay draw cannot
consume map history solely because the map HUD is active. Far planes are no
longer enumerated; projection sanity, model recognition, owner/generation,
consecutive frames, and camera continuity remain mandatory. Map skeleton
interpolation remains suppressed so the camera is applied once.

Capture replays recognize all 6,444 fuller-map and 1,800 sparse-map draws, with
zero missed moving frames (32 and 38 respectively). Replaying the sparse map
with varying previously unseen far planes also passes. Regression tests cover
unrelated preceding world draws, markers, owner changes, closure, frame gaps,
comparison mode and invalid transforms. Live presentation still needs checking.

### CPU sun glare movement and intensity

Capture `song-effect-1792194857611` contains 104 CPU glare draws. The previous
position interpolation existed only in accelerated vertex upload. All captured
glare draws match a fixed orthographic projection/model and a distinctive
72-vertex additive material; its second combiner constant animates alpha.
The CPU path now interpolates shaded clip XY, vertex color and that alpha in
the private midpoint replay. With the fixed transform this is equivalent to
interpolating screen-space input positions, without another shader run or guest
memory access. Native geometry, UVs, depth, combiner state and coverage remain
unchanged. Texture/framebuffer/generation changes, gaps, UV changes and large
motion reject interpolation. Capture geometry now includes these depth-disabled
glare draws so a follow-up capture can verify their vertex continuity.
Material replay matches 104/104 captured draws; history tests exercise movement,
brightness, duplicate draws and rejection cases. Visual confirmation is pending.


### Chest appearance particles

Capture `song-effect-1792198935796` contains 45 blue particle draws surrounding
an appearing chest. The chest model starts translucent midway through the burst;
this is the appearance effect, not the treasure-opening light. Its exact six-stage
material, blue tint, unit-square UVs, square geometry and bounded sizes identify
it without relying on the captured texture address. CPU midpoint replay now
interpolates particle position and size. Native sizes fluctuate substantially, so
matching uses center motion and orientation rather than size continuity. Mutual
nearest matches and an ambiguity margin protect against array compaction and
new particles; collapsed squares stay native. Histories reset with scene changes
and require consecutive native frames.

Capture replay recognizes all 45 draws, producing midpoints in all 30 sampled
consecutive-frame pairs and changing 610 of 653 noncollapsed particles in those
pairs. Unmatched particles retain their native geometry. Tests cover reorder,
compaction, size changes, new particles, ambiguous matches, frame gaps, reset,
generation isolation, repeated draws and material/UV/tint rejection. This path
is for CPU interpolation; final appearance requires an in-game check.

### Coastal water and Snowhead interpolation follow-up

Great Bay coast/temple vortex and beaver-race water now identify 27 captured
mesh/material combinations using active UV bases and full combiner state.
Scrolling histories ignore inactive texture bindings, reject discontinuities,
and accept only measured per-frame velocities (including the two verified
speeds of a shared waterfall mesh). Offline replay produces midpoint UVs for
all sampled consecutive pairs of those layers.

The beaver wake and Blue Bubble flame additionally track individual moving
models. The wake retains its mirrored basis while interpolating its fast spin;
the flame keeps its two scrolling layers synchronized through wraps. Matching
is bounded by world motion, basis changes, material and scroll speed, with
ambiguous/reused instances left native. Both paths run before CPU/GPU replay.
The captured wake and flame layers produce intermediate transforms on every
sampled consecutive pair; first frames and capture gaps remain native.

Snowhead's shutter uses its verified direct root despite its tiny bind offset.
The red flag and Blue Bubble flame permit equivalent bones only for their
verified skeletons/meshes, and require agreement of intermediate poses. Broken
snow-boulder halves select their verified model instead of being rejected when
a shadow actor shares the transform. Coincident instances remain rejected.

Boss-warp stars now use their upward movement and captured lifetime fades to
avoid confusing vertically stacked neighbors. Replay matches 6,217 of 6,324
particles, with no cross-particle mismatches against the captured continuity;
107 remain native (102 new births and five ambiguous candidates).
Cut-grass fragments accept their captured animated size range: both texture
variants now decode all 95 sampled strips. Chest-reveal sparkles accept the
larger squares measured in Snowhead, avoiding rejection of an entire strip
because one sparkle exceeds the former size bound. Births and ambiguous
particle matches still remain native.

The submitted LapTimeSong layout now activates the existing menu-interpolation
path, including retained CPU inputs. The ownership ends when it leaves the
submitted queues and does not activate world-map interpolation. It covers
compatible Double Time menu geometry; visual confirmation of every transition
is still required. Tests cover material rejection, wraps, mirrored rotation,
frame gaps, duplicates, actor ambiguity, particle sizes and layout ownership.

### Double Time white-background cutscene clipping range

The cutscene can retain a scene-dependent shorter far plane (depth term 1.00043–1.00051, near term about 5.00250149). Recognize this separately from the existing time-song projection families, preserving skew, lens-step, camera-cut and frame-gap guards. Replaying the sampled capture accepts all 30 consecutive cutscene pairs previously rejected for lens changes; the 15 acquisition frames after capture sampling gaps remain rejected. Native white background clearing is unchanged.

### Lock-on letterbox bar motion

Interpolate the native black top/bottom bars in the ordered midpoint replay, using the captured untextured material, orthographic projection and edge-anchored transform. Keep independent histories for each edge, preserve width, depth and color, and reject gaps, duplicates and height jumps above the captured five-pixel step. Both CPU and GPU paths use the same midpoint height. All 120 sampled bar draws match; all 32 moving consecutive pairs yield midpoint heights. Acquisition/disappearance boundaries remain native; full-screen fades are excluded.

### Goht shared bone transforms

Goht's captured 29-bone skeleton has coincident skin transforms in body meshes and coincident direct transforms in a small limb mesh. Permit the existing consensus resolver for the verified signature and mesh sizes (12, 129, 150, 219, 750, 756, 1878 vertices). Matching current transforms must also agree at the midpoint; different actor instances, missing frames, unknown meshes and divergent histories remain rejected. Replay of both captures restores 539 additional palette-zero overrides. Collapsed zero-scale geometry remains native. Tests cover each mesh, other palette slots, divergent histories, duplicate instances and unrelated skeletons.

### Goht complete skin palettes, including the head

A follow-up capture exposed reconstructed-bind error above the generic matcher in head meshes (96/348/606 vertices) and active slots of other body meshes. Validate all eleven skin mesh palettes against the local model's ordered bone tables, using the measured bounds (up to .05735 translation and 1.009e-5 basis error, guarded at .06 / .000012). Preserve the native bind correction in each interpolated slot. A collapsed bone 15 is validated but never revived. The newest capture improves from 667/2021 to 2021/2021 eligible visible skin-slot overrides; all change from the native pose. The other noncollapsed skeleton bones also produce moving midpoints. This verifies captured animation, not unseen attack/death states. Tests cover complete palette ownership, head/body motion, preserved corrections, rejected mismatches/duplicates/gaps, and hidden geometry.

### Romani Ranch alien glow

The captured moving glow is the alien beam model's 324-vertex outer layer and 180-vertex core, verified against local asset mesh counts and the complete captured materials. Per-instance history now interpolates their transforms, outer-layer UV scroll (including period-1/2 wraps), and animated combiner colors/opacity. Color changes are allowed only in these two materials' animated constant fields; all other material fields remain exact. The midpoint upload preserves native fragment state and clears its color override before subsequent draws. Both layers yield 580 moving midpoints in the capture; births, gaps, distant jumps and ambiguous neighboring instances stay native. Existing flame/wake matching and reflection behavior remain scoped to their original patterns.

### Mounted Link and captured actor gaps (October 22)

The mounted bowstring uses a deliberately singular human bone 24. Complete its
missing Y basis for rotation interpolation, then restore the zero column; this
exception requires the human topology and 12-vertex bow mesh. Bunny Hood's
three-bone model follows the human head midpoint through its verified fixed
attachment, with animated ear children retained. Extend the direct-root sliding
shutter family to Spider House, Ikana and the audited Fortress meshes (84/264
vertices). Allow consensus resolution of equivalent bones for the reflected
mirror light, marine lab fish, Bombchus and burrowed Deku head (414/186 vertices).
Current and intermediate candidates must agree; coincident instances, hidden
zero-scale geometry and capture gaps remain native.

Offline actor-history replay yields 28 moving bowstring midpoints, 28 Bunny Hood
midpoints, 178 moving Spider House shutter midpoints, 126 mirror-light midpoints,
108 fish midpoints, 330 Bombchu midpoints and 108 burrowed-head midpoints. Counts
refer to palette zero; capture acquisition/gaps are not interpolated. The rod
capture already yields 850 moving rod/reel midpoints through the existing path,
so no fishing-rod skeleton exception was added. The latest Tingle capture has
Tingle's body but neither balloon model signature; it cannot validate the
existing sphere/knot interpolation or establish a new balloon defect.

### Bow release and Zora room water follow-up (October 22)

The follow-up bow capture `link-pose-1792711900684.json` shows human bone 24
stretching from zero Y scale, rapidly contracting, and crossing into negative
Y scale during release. The previous flat-only exception missed those changes.
Reconstruct orientation from the two intact axes and interpolate signed string
scale separately, scoped to the human skeleton and verified 12-vertex string
mesh. Preserve generic singular-transform rejection, scale bounds, shear checks,
duplicate ownership and history-gap guards. An offline replay with a fixed view
improves from 239 to 384 moving midpoints among 386 changing poses; the remaining
two samples cross the existing 100-unit discontinuity threshold at view changes.
The capture is truncated, so this result describes its recorded poses only.

Water captures `song-effect-1792711997102.json` and
`song-effect-1792712165332.json` identify additional Zora room sheet, foam and
surface layers. Add their measured UV bases, material/blend signatures and scroll
rates, including the alternate speed of an existing 168-vertex additive sheet.
Resolve equal-size, equal-basis meshes using the complete material instead of
rejecting everything after the first structural match. The keyboard room's
600-frame cycle wraps by known whole texture periods; interpolate its short
scroll step rather than the phase reset. Only these measured periods may wrap.
Offline replay produces 952 intermediate draw samples in the first capture
(119 consecutive pairs across eight submissions) and 840 in the keyboard room
(120 pairs across seven layers), including the recorded loop boundary. Capture
sampling gaps reacquire natively. Regression checks cover material collisions,
alternate speeds, exact loop periods and rejection of unrelated phase jumps.
These are capture replays and automated checks, not post-build visual validation.

### Ice breath, bubble-pop fragments and freezing particles

Add material- and geometry-scoped CPU particle interpolation for the captured
ice-enemy breath, probable fairy bubble-pop fragments, ice-arrow square shards
and freezing mist. Match stable size, bounded movement, lifetime alpha and atlas
progression; preserve native atlas tiles and signed mist color offsets. The
shared previous pool handles multiple ice enemies drawing the same material;
ambiguous matches, births and discontinuities remain native. White square ice
shards are distinguished from rectangular bomb debris despite shared material.

Replay decodes all 891 breath batches and 12 pop batches, generating 12,756 and
160 moving particle samples respectively. The freezing capture decodes all 105
shard batches and 36 mist batches, generating 1,324 and 130 intermediate particle
samples. These totals include acquisition frames, births and unmatched particles
in the native sample counts; they are not a claim of complete matching at spawn.

### Reflected light and Zora Hall water animation

The mirror beam, halo and end glow retain their skeletal midpoints while their
UV layers and animated combiner colors interpolate. Rotation uses the captured
0.75-degree step, scale-two basis and actual pivot translation; wrapping follows
the short angular path. Each material produces 62 intermediate UV samples in the
capture. Zora Hall's nine waterfall, foam and pool materials use their measured
scroll rates and full material/blend checks, including the source-color blend
on the 63-vertex layer. Each yields 105 intermediate scroll samples. Inactive
texture rows remain native, and phase resets do not interpolate.

The reflected shield face on walls uses a different path: it is composited into
opaque receiver materials through texture unit 1, with its world-space projector
in all three rows c14..c16. `mirror_projection_interpolation.h` matches the full
combiner and the projector's 0.03/0.03/1 basis, recovers its pose, interpolates its
origin and rotation, and restores the projection scale. Interpolating UV offsets
alone would miss rotation and the third row. The projector history is shared by
receivers using the same projection texture and scene generation, independent of
their base textures and mesh sizes. Intensity also interpolates; frame gaps,
ambiguous projectors, teleports and direction cuts retain native values.

Both CPU vertex replay and GPU uniform upload apply all twelve coefficients only
to the midpoint draw, leaving the native wall material intact. The October 23
wall-face capture identifies 1,410 receiver draws across twelve mesh sizes and
produces 936 moving midpoints; the other samples seed history after capture gaps.
Tests cover projector rotation about its origin, the central projected ray,
intensity, repeated wall draws, material isolation, discontinuities and resets.
Live visual confirmation is still required.

### Water-entry interpolation dropout

The water-entry capture records two extended `no-midpoint` intervals exactly
while both lens axes wobble. Its underwater far-plane coefficient is
1.0007358789, outside the former exact coefficient 1.0004167557. Draw distance is
scene-dependent and cannot identify underwater distortion. Permit the existing
bounded five-percent two-axis motion for the native perspective family with a
five-unit near plane, independent of far distance. Keep the same clipping range
within each pair, require both axes to change without skew/sign flips, and
retain camera-motion, timing and frame-gap guards. The capture's lens replay
improves from 34/76 to 75/76 consecutive pairs; the clipping-range transition
still reacquires natively for one frame. This is offline verification, not a
live visual result from the still-running older executable.


### Great Bay rotor, waterfall sheets and vortex

The rotor screw and foreground deck share a one-bone root signature but use
separate model instances. Permit the existing two-owner consensus resolver for
signature `85335493df397bb2` and the verified 48/72/144/348/720-vertex meshes.
Both histories must agree; third owners, divergent histories, unknown meshes
and capture gaps remain native. The rotor capture produces 354 moving
palette-zero midpoints for these meshes.

Add eleven captured static water materials and the 240-vertex rotating vortex
in the rotor room. The vortex's two layers have scales two/four, a shared
negative 1.2-degree native angle step and linked scroll/pivot translations.
Interpolate wrapped angles and reconstruct the pivot at the intermediate
phase, preserving inactive UV rows. Repeated submissions reuse the complete
active UV rows, including the rotated basis. Add 1500/1851/45-vertex meshes to
the existing Great Bay water material families. Large accumulated coastal
scroll phases allow bounded float rounding in both samples, capped at 0.00005;
changed scroll speeds and phase resets remain rejected. Each new static layer
and the vortex produce 93 intermediate samples in the rotor-room capture;
the twice-submitted 432-vertex material produces 186.

### Swimming Zora bubbles

The follow-up Great Bay capture `song-effect-1792712414403.json` contains
79–100 swimming bubbles per draw. The old 64-quad decode/history limit rejected
every one of its 134 batches before matching could run. Raise these limits to
128, matching the existing shared decoder's 513 inputs/768 strip indices and
the existing 128-quad history pool. Material, geometry, stable-size, movement
and ambiguity checks are unchanged. Replay now decodes all 134 batches and
produces 8,317 moving intermediate samples among 13,069 captured bubbles;
capture gaps, births and ambiguous matches remain native. Tests cover 64, 79,
100 and 128 bubbles, repeated draws, gaps and rejection beyond capacity.
This is capture verification; a new in-game visual check is still needed.

The captured trail already reaches the additive bubble path. Its small, fixed
random-size bubbles move roughly 11–12 world units per native frame, exceeding
the original eight-unit particle matching bound. For the complete verified
combiner and small-bubble batch (2.07–3.05-unit edges), permit up to 16 units of
motion while requiring size agreement within 0.005. Retain mutual nearest
matching, ambiguity rejection, RGB continuity, bounded history, and native
atlas tiles. Changing between ordinary and swimming matching clears history.
Ordinary growing bubbles retain the original movement and size bounds.

All 139 captured batches decode. Offline history replay improves from 476 to
3,650 moving intermediate particle samples out of 5,657 captured particles;
roughly one third of frames acquire history after capture gaps. Newly spawned
and ambiguous particles remain native. This capture has active presentation
midpoints throughout, so the defect is independent of underwater lens dropout.
No alpha-blend exception or new texture is needed.


### Freezard death shrink and Snowhead rising pillar

Freezard's four bones coincide, so the generic owner matcher rejects its
630-vertex body even while the native scale changes. Permit the existing
consensus resolver for signature `4fa89f19eb218899` and that mesh only. All
candidate intermediate transforms must still agree. In the death capture,
108 body samples previously produced no midpoints; they now produce 72. All
four sampled consecutive shrinking pairs have the expected intermediate
vertical scales (0.0054, 0.0050, 0.0014 and 0.0010). Births, frame gaps,
coincident actors, divergent bone histories and fully collapsed geometry
retain the existing safeguards.

Snowhead's 384-vertex ice rings alias a direct transform with the neighboring
ring's skin transform. Their bind offsets differ by one ring height. One ring
also aliases the middle stone section's scaled skin. Recognize only these
verified one-bone signatures/binds (`046391818a624e42` and
`0952810a097e354ae`), require a direct/skin pairing from distinct instances,
and require independent midpoint agreement. Duplicated coincident rings,
third owners and rings moving apart do not inherit each other's history.

Replay using the static room model to remove camera motion yields 185 moving
midpoints across 280 ring submissions; all 185 consecutive sampled ring
pairs interpolate. The separate 468/618-vertex stone sections already produce
37 midpoints each. The remaining submissions acquire history after capture
sampling gaps. Unit tests cover rise motion, middle-section skin, changed
binds, wrong meshes, gaps, extra owners and divergent prior trajectories.


Validation after the October 22 capture fixes: the full `azahar` target and
`mm3d_camera_midpoint_tests` / `mm3d_cpu_vertex_replay_tests` build successfully.
Camera/actor/particle regression tests and both interpreter/JIT vertex replay
tests pass. The running emulator was left untouched; live visual verification
requires loading the rebuilt executable on the user's next restart. No commit
or push was made. The separate proposed stick-click control layout remains
unimplemented; its English and localized-text audit is recorded in
`tools/mm3d/CONTROLS_TEXT_AUDIT.txt`.


### Ice breath atlas follow-up

The follow-up capture `song-effect-1792709021269.json` contains 540 recognized
breath batches, with active 60 Hz presentation throughout (596 pairs).
Offline replay matches 7,777 of 9,351 particle samples having a captured previous
frame; another 4,688 samples follow deliberate capture gaps. Of those matched
particles, 7,708 advance one atlas tile. The former implementation interpolated
position/alpha but retained the current tile, leaving the internal puff image
stepping at the native animation rate.

For the exact additive breath material only, render the current and previous
tiles at the same intermediate position with half the interpolated alpha each.
The verified combiner passes vertex alpha through; SourceAlpha/One blending
therefore preserves the combined contribution without moving UVs between atlas
cells. Reuse the existing CPU particle secondary-draw path. Unmatched/new
particles receive no second contribution; unchanged tiles, history gaps,
texture/generation changes and nonadditive ice mist retain their prior behavior.
The native 30 Hz draws and guest particle simulation are unchanged. World-layer
composition copies RGB opaquely, so framebuffer alpha is not treated as coverage.

Focused tests cover brightness conservation, complete atlas cells, repeat draws,
births, gaps, changed generations and isolation from freeze-mist materials.
Live visual confirmation is still required; the capture is not a post-change
recording.
Wart bubble follow-up (2026-10-22)
- `wort_ball`, topology/bind signature b847a2c91407dd5d, 216 vertices.
- Root squash/shear rejected rigid interpolation. More importantly, native depth sorting reuses model slots for different bubbles; treating instance addresses as particle identities causes 100–478 unit jumps.
- Match the complete world-space pool reciprocally (32-unit movement bound), require matching model/generation, bounded affine deformation, and reject ambiguous crossings. Never widen generic actor limits.
- Captures 1792713188219 / 1792713468804 / 1792713489270: 2241/2260, 1020/1020, 894/894 consecutive changing samples produce intermediate poses. Initial capture keeps 19 ambiguous matches native. Replay derives camera motion from the stationary room shutter.
- Hit bubbles in song-effect-1792713505244 use a distinct cyan/fading material. All 19 batches decode; 150 intermediate particles across consecutive frames (239 total particles including first/gapped frames). Match stable random size and preserve native atlas tile.
- Deku Princess follow-up is documented below; both skeleton and song-effect captures are now available.

Wart body follow-up (2026-10-22)
- Capture `link-pose-1792721612378.json` identifies the eight-bone `wort` model,
  signature `b4670e8c455fcc2b`. Its 1872-vertex body starts with coincident skin
  transforms for bones 0 and 1, so generic slot-zero matching rejects ownership.
  This is separate from the bubble sorting/deformation issue above.
- The user's local `zelda2_boss04.gar.lzs` confirms body skin palette `{1,5,7}`
  and a separate 396-vertex eye using direct bone 3. Match the complete body
  palette, signature and topology, then use the existing actor hierarchy/history.
  Retain each palette slot's native bind correction and reject multiple owners.
- Capture replay: 84 body draws; all 53 consecutive changing samples interpolate
  in all three body slots (previously zero). The eye retains 53/53 changing
  midpoints. Gaps and first samples seed history instead of bridging missing data.
- Camera midpoint tests cover the coincident-bone regression, all body branches,
  eye-before-body draw order, repeat draws, disabled mode, mismatched palettes,
  topology/signature changes, duplicate owners, generations, gaps and teleports.
  Live visual confirmation remains outstanding; no game assets are bundled.

Cow tail and ghost balloon follow-up (2026-10-22)
- Cow capture `link-pose-1792724628330.json` identifies signature
  `bd01809703b206e7`, shared by the local `cow` and `cow2` models. All five
  local tail joints (5–9) contain shear and fail ordinary rotation decomposition.
  Interpolate bounded affine steps only for these joints, with the exact ten-bone
  topology verified; reconstruct the chain in its interpolated parent frames.
- Replay accepts all 360 consecutive local tail steps. Each of the four distal
  tail palette slots has 72/72 meaningful midpoints; the tail-root slot has 24/72
  above the palette matching tolerance (the remaining changes are very small).
  Head/body behavior is retained. The capture is truncated but has usable pairs.
- Balloon capture `link-pose-1792724905266.json` is `ghost_balloon` in the local
  `zelda2_po_fusen.gar.lzs`, signature `ae6056c772fdbd26`, not Tingle's balloon.
  Its body joint 1 has genuine local shear and fails rigid interpolation in all
  42 consecutive pairs. Use bounded affine interpolation only for this joint,
  guarded by its exact nine-bone topology. Arms and hanging pieces retain the
  ordinary hierarchy interpolation.
- Replay now produces 42/42 meaningful body midpoints across all five meshes
  (1350, 72, 36, 54 and 264 vertices), previously zero. No animation clock or
  game asset changes are involved. One arm frame also had a skin transform
  coincident with the root. The verified complete 1350-vertex palette
  `{3,4,2,1,8,6,7,5}` disambiguates it while preserving native bind corrections;
  generic matching tolerances are unchanged. All eight body slots now produce
  42/42 meaningful midpoints, including the arm crossing.
- Regression tests exercise both hierarchies, nearby instances in reversed draw
  order, native palette preservation, symmetric deformation, repeated balloon
  draws and separate meshes, unrelated skeletons/topologies, collapsed/large/
  nonfinite steps, teleports, disabled interpolation, generations and frame gaps.
  Azahar and camera midpoint tests build successfully; the camera midpoint
  CTest and `git diff --check` pass. Live visual confirmation remains outstanding
  after relaunch; the running emulator was left untouched. No game assets are bundled.

Pictograph high-resolution display (2026-10-22)
- Graphics now exposes "HD Pictograph photos (restart required)", enabled by
  default. The saved `hd_pictograph_photos` preference is latched when a game
  session initializes; changing it cannot invalidate active surfaces or saves.
  Disabled sessions bypass photo pixel/JPEG inspection, HD GPU capture/binding,
  PNG reads and writes, and HD-specific frame widening. Native photo capture,
  subject checks and Gear placement/scale remain. Existing HD PNGs are retained.
  Hook tests cover zero HD backend operations/guest reads while disabled;
  layout tests cover native preview/frame dimensions and retained Gear controls;
  settings and upgrade tests cover default-on and a persistent disabled choice.
- Hooks verify USA code words at shutter return188de0, accepted-photo188fb0,
  JPEG import609fbc, and picture binding1b6d38. Shared conversion120648 and
  destruction198868 retain their independent pause-background handling.
- Copy the linear display buffer's scaled GPU surface before native JPEG
  downsampling. Private RGBA8 surfaces use their full extent at scale1 so host
  downloads preserve resolution. Native JPEG/MPO and subject checks are untouched.
- Accepted photographs write optional hash-keyed stereo PNGs in
  config/mm3d-local/pictographs. PNGs are rotated upright and corrected to16:9;
  they are user-generated files, not bundled assets. Missing/corrupt files fall
  back to the original saved image. Previously taken photos cannot gain detail.
- Only capture-owned512x256RGB565 uploads can be replaced, and six decoded-pixel
  probes must validate upload orientation. Photo replacement uses full brightness,
  distinct from the pause scene's quarter-brightness shader path.
- The bound photo/frame layout widens by16/15 around the screen center, composed
  with existing Gear photo position/scale. Ordinary pause snapshots are unchanged.
- Hook fixtures verify binary words, replayed-instruction inputs, ownership,
  cancel/keep/import isolation, corrupt data, and no guest/save-memory writes.
  Live visual verification still required for shutter preview, Keep, Gear reload,
  stereo orientation, and PNG framing on the user's running game after restart.

Deku Princess eye motion (2026-10-22)
- Skeleton capture1792713857784 identifies deknuts_queen, signature91e28867df89d7bc,
  25 bones. Its separately drawn24-vertex eye mesh matches direct bones9 and12;
  both transforms are identical in every captured pose. Generic unique-bone
  identification therefore rejected the eyes while the body interpolated.
- Allow this verified mesh/skeleton through the existing equivalent-bone route.
  All matching bones must agree on the interpolated transform, and ownership must
  remain unique. Divergent histories, unrelated meshes, disabled interpolation,
  frame gaps, and coincident instances retain the existing rejection rules.
- Replayed90 eye draws from the capture: before,0/60 consecutive moving samples
  received an intermediate transform; after,60/60 do. Regression coverage checks
  shared-head motion plus the rejection cases above.
- Song-effect capture1792715009328 confirms the eye draw switches between separate
  textures (167/8/4 sampled draws), with unchanged UV bases. The local archive's
  dn_queen_eye.cmab contains four32x32 eye textures. Blink artwork/timing is left
  native; this fix addresses eye geometry following the moving head at30fps.
- Validation: azahar and mm3d_camera_midpoint_tests build successfully; camera
  midpoint regressions, CPU vertex replay, and photo lifecycle tests pass.
  Live visual confirmation is pending; the running emulator was not restarted.

Pictograph shader validation follow-up (2026-10-22)
- The headless atlas GPU fixture now supplies has_separable_shaders, matching the
  real Vulkan renderer. Its previous zero-initialized profile omitted SPIR-V
  input locations and failed before testing the photo changes.
- With the renderer's profile, generated PICA shaders compile and the GPU atlas
  fixture passes selected/inverse masks, orientation,2x scaling, and reset.
  Separate2D/cubemap photo shader checks and photo lifecycle CPU tests also pass.

Koume target minigame (2026-10-22)
- Skeleton capture1792716099763 and local zelda2_tru.gar.lzs identify the
  one-bone tru_mato1_model target (036049aec268c4f9) and tru_kusari1_mod chain
  (22fef6cc8a39263c). The144/600-vertex draws share a root within the normal
  palette tolerance, so generic unique-owner matching rejected their pair.
- Admit only this model pair, its captured mesh sizes, identity binds, distinct
  instances/models, and one-root topology. Track both histories and require
  intermediate transforms to agree. Independent targets continue through the
  ordinary unique-owner path; unmatched or divergent objects remain native.
- Replay before/after: target58 consecutive moving draws,29 ->58 interpolated;
  chain29 consecutive moving draws,0 ->29 interpolated. The capture reached its
  8192-draw limit, so these counts describe its recorded portion only.
- Regression cases cover translation/rotation, reversed owner order, disabling,
  unknown meshes, extra/unrelated owners, changed binds, frame gaps, and
  coincident current transforms with different movement histories.
- Live visual confirmation remains pending; the running emulator is untouched.

Pictograph high-resolution crash (2026-10-22)
- macOS report azahar-2026-10-22-204441.ips faults in StreamBuffer::Map from
  BindPhoto. The log records a2800x1680 capture (7x), followed by the staging-size
  assertion. Its18816000-byte RGBA read exceeds the runtime's16MiB download ring.
- PhotoTransfer::Read now downloads private RGBA surfaces in whole-row batches
  of at most8MiB, copying each completed batch before staging can be reused.
  KeepPhoto uses it for full-resolution export; BindPhoto downloads only its six
  small orientation probe patches. No resolution reduction or staging-ring growth.
- The --photo-readback GPU fixture exercises production upload/download paths
  at6x,7x,16x and the largest imported16:9 photo. Coordinate patterns check every
  pixel through staging wrap, partial final batches, offset rectangles, and probe
  patches; invalid/empty rectangles must fail safely.

Pictograph preview and save follow-up (2026-10-22)
- Capture1792716516265 records180 draws for each bound photo texture; layout
  replay flags228 show native success and photo binding but no midpoint replay.
  The generic HUD gate excluded all pause-style bindings outside inventory.
  Verified photo bindings3/4 now enter that existing replay path with their
  texture and shader override intact. Pause backdrop handling is unchanged.
- The captured photo pane uses UVs0..400/512 and0..240/256. Flipped photo sampling
  must therefore use240/256 as its upper V, not1.0; otherwise sixteen padding
  rows produce a black stripe and shift the image within the frame.
  --photo-sampling checks the production shader remapping on the GPU in both
  orientations using coordinate colors across the entire photo rectangle.
- KeepPhoto now snapshots each actual eye once, then moves rotation, resampling,
  PNG encoding and atomic file publication to one background worker. Mono eyes
  share immutable pixels. Only the active job and newest pending photo can be
  queued; shutdown drains the newest accepted save. Latest kept pixels remain
  available for an immediate Gear reopen before the PNG files finish writing.
- Save-queue tests check off-thread execution, nonblocking submission while a
  prior save is held, bounded pending work, and completion on shutdown. Photo
  binding tests distinguish replayable photos from pause textures/reserved slots.
- Final validation: azahar, the GPU fixture, photo tests, and camera midpoint
  tests build successfully. CTest photo lifecycle/save queue, camera midpoint,
  and CPU vertex replay pass. GPU photo readback passes6x/7x/16x/imported16:9;
  photo UV sampling and existing atlas masking fixtures pass. In-game preview,
  Keep responsiveness, and target motion still require the user's next relaunch.

### Photo UV origin and fullscreen frame override follow-up

- The prior sampling fixture used the MFL UV range directly and missed
  LayoutShader.vsh instruction0xfc, which computes V=1-V. UpCollect's resource
  V=0..240/256 therefore reaches the fragment shader as16/256..1. Bindings3/4
  now map respectively1-V andV-16/256; the existing pause mapping is unchanged.
  The GPU fixture now models the actual vertex-stage output, and both
  orientations cover the full picture without the16-row black strip.
- Capture1792716516265 frame4880 draw77 batches the blackboard and gradient
  frame (12 untextured vertices), and a saved fullscreen override sets clip
  scale1. Draws78/79 sample the picture with clip scale15/16. This mismatched
  scaling creates the thick side borders. The verified gradient frame/batch
  is exempted from that override; fullscreen fades retain their rules.
- UpCollect's native208x128 frame around200x120 is widened to retain four
  pixels on every edge. Only its draw-time geometry is staged and restored;
  Gear position/scale and shutter-preview placement continue to compose.
- Validation: native inventory checks pass, including geometry restoration;
  headless Vulkan --photo-sampling passes using actual shader UVs. Live game
  appearance still needs validation after restarting into the new executable.

### Photo orientation and animated/viewer frame follow-up

- User screenshots showed an upside-down preview despite an upright saved PNG,
  plus remaining frame padding during opening and in Gear. Inspected the latest
  saved PNG365514cd...: the captured picture itself is upright and complete.
- The private surface always stores display-buffer rotation: PNG(x,y) maps to
  texture(1-y,x). Capture, cached reopening, and PNG import share that convention.
  Native JPEG tiling only validates the upload; its inferred orientation cannot
  determine the private surface's orientation. BindPhoto now uses binding4 for
  that fixed convention and removes the JPEG/source color-correlation guess and
  its GPU readbacks. Native JPEG and saved image bytes remain untouched.
- UpCollect's frame can draw alone or with its blackboard. Register packing varies
  with batching; fixed color registers91/95 were too restrictive. Recognize the
  frame's aspect ratio and four BGR gradient colors through the referenced color
  index, allowing native fade alpha and movement/scale. Only a black backdrop can
  share this corrected batch. Unrelated fullscreen fades retain their overrides.
- Replayed all180 frame batches from capture1792716516265 successfully. Regression
  cases cover single/batched frames, different color-register placements,
  scale0.45..1.2, fade alpha0..1, movement, and unrelated gradients/fades.
- The GPU photo sampling fixture now uploads and samples an actual rotated image
  with known upright rows/columns. It uses the production binding constant, so
  reversing the photo fails instead of merely demonstrating two possible flips.
  It passes on Metal; camera midpoint and photo lifecycle tests also pass.
  Live opening-animation and saved-viewer confirmation remains a user test.

### Stone Tower fire, Eyegore, and Giant Link follow-up

The fire-ring/updraft song capture `1792760588323` identifies four exact
material/mesh/UV patterns. Their scrolling layers use one wrapped phase so the
layers stay aligned across resets. The ring's third layer also pulses in height;
interpolate that UV scale as well as its offsets. Shader, active texture mask,
blend/depth state, TEV material and UV bases must all match. Texture addresses
key history but do not identify the effect. CPU and GPU replay modify private
midpoint uniforms only. Gaps, generation changes, unexpected phase speeds and
conflicting submissions reseed or remain native; unused UV rows stay untouched.
Replay recognizes 222 ring, 333 core, 333 sheet and 1,660 ember draws, producing
148 / 222 / 222 / 1,105 consecutive-frame midpoint draws respectively.

Garo Master's sword-flame capture `1792760717446` has a fifth exact UV pattern
and two CPU-generated moving strips. Verify the 54-index / 20-input topology and
all three UV sets; the unused color attribute contains stale values and must be
preserved. Match complete strips in world space independently of draw order,
with bounded movement, nearest-candidate separation and one previous strip per
current sword. Frame/scene gaps reset tracking. UV replay covers all 76 usable
frame pairs. Geometry returns 67 midpoint strips (59 actually moving); nine
ambiguous fast crossings deliberately retain native geometry. This remaining
fallback needs live evaluation rather than a looser ownership guess.

Eyegore captures `1792760938711` and `1792760965706` expose coincident skin
transforms that make generic bone/owner matching ambiguous. Verify the complete
22-joint topology and seven native mesh palettes; retain the native bind
correction without expanding generic tolerances. Both replays now cover every
active slot of all seven meshes in 36 and 38 usable frame pairs. Tests include
coincident rest joints, camera motion, full palette validation and ambiguous
actor ownership.

Giant Link's boulder capture `1792761204870` uses 318-vertex gripping hands,
instead of the previously covered 234-vertex fists. Both meshes are verified
against the local human Link model and retain its existing giant-form scale and
whole-palette checks. Replay now covers both gripping hands and both connected
arm palettes in all 48 usable pairs each (192 mesh draws total).

Twinmold punch/swing captures `1792761518687` and `1792761575004` expose motion
near/beyond the ordinary 100-unit actor continuity cutoff. Allow up to 200 units
of world-space root movement only for the full verified Twinmold topology, shared
by red and blue models. One terminal head joint also needs the existing bounded
affine deformation fallback for small shears during swinging; other joints keep
the normal hierarchy. Fixed-camera replay covers all 870 usable punch mesh draws
and 848 swing mesh draws, including frames formerly dropped at the cutoff.
The short red capture `1792761608754` already passes its 1,382 usable mesh draws;
it shares the same guarded correction. Skeleton captures lack camera samples,
so replay does not establish actual world-space speed; synthetic tests separately
verify moving-camera cancellation, fast swings and rejection of teleports.

These are capture replay and automated-test results, not live visual confirmation.
The running emulator is left untouched; the new executable must be loaded for
in-game assessment. No model or texture assets are included with these changes.

### Gekko's green jelly bubbles (Great Bay Temple)

Skeleton capture `1792762543633` identifies `muddjelly_small` from the local
`zelda2_bigslime` model. Its two 216-vertex surfaces use direct bones 1 and 2;
all three bones and their identity binds coincide. Generic ownership therefore
rejects every draw. Its 15 model slots also change physical bubble ownership:
43 same-slot steps jump, with a maximum view-space displacement of 738 units.
Do not use the model-instance address as the bubble's identity.

A separate, bounded jelly history verifies signature `b7bb8c8a4e2368b2`, the
three-word topology, identity binds and coincident bones. Match the complete
world-space pool reciprocally using position, model/generation and valid
transform interpolation. Require separation from the second candidate and a
64-unit movement bound. Use rotation/scale interpolation where possible, with
the existing bounded affine fallback for small deformations. Both surfaces use
the same intermediate direct matrix. Ambiguous overlaps remain native.

Follow-up capture `1792762637761` contains the combined jelly rather than the
small bubbles. The local `muddjelly`, `muddjelly_sub`, `muddjelly_ura` and
`muddjelly_ura_s` models share signature `e4e4b9a66b715b85`, an identity bind and
960-vertex direct-root surfaces. Their front/back model instances alternate each
frame, leaving no consecutive instance pairs. Canonicalize identical front/back
submissions only for this verified one-bone model family, then track its motion
independently of the small-jelly pool. The separate triangular bubble particles
already receive intermediate matrices and keep their existing path.

Fixed-camera replay changes 1,348 of 1,350 usable small-jelly draws; one ambiguous
physical match (two surfaces) remains native. All 104 usable combined-jelly draws
receive intermediate matrices, 102 of which differ from native. Skeleton
captures do not contain the world camera; synthetic tests separately cover
camera motion, rotating bubbles, swapped slots, alternating front/back models,
small shear, reciprocal ownership, duplicate submissions, generation changes,
frame gaps, teleport rejection, disabled mode and bounded pool capacity.
These are capture/test results; in-game appearance needs checking with the new
executable. Neither the running game nor guest/native draw matrices are changed.

### October 23 Majora whip follow-up

The new `link-pose-1792765408773.json` capture contains `majora_demon`
(`18ccb05513137730`, 113 bones). All twelve known whip palettes still match:
1,080 draws, including 720 consecutive draw pairs. Offline replay produces all
active palette overrides for all 720 pairs. Every joint's local midpoint passes
its existing decomposition/scale checks. This does **not** verify the visible
result reported by the user; no tentacle animation change was made on that basis.

Skeleton captures now include schema-3 `replays` alongside the unchanged native
`draws`/`actors`. These record rendering eligibility gates, camera-match/fallback
state, CPU versus accelerated replay, available pose count, changed/overridden
palette masks, maximum uniform delta, native camera, and whether the midpoint
draw was submitted. Join by frame, vertex base, index offset and vertex count.
The masks are palette slots, not global skeleton bone indices. Collection is
bounded and active only during an explicitly requested capture; it makes no
animation or guest-memory changes. This evidence is needed to distinguish an
animation calculation problem from discarded intermediate rendering.
