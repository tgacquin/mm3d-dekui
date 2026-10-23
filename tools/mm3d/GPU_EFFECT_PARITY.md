# GPU-mode effect interpolation

GPU mode now executes the existing CPU-mode effect decoders and history calculations
on a bounded snapshot of native vertex inputs. The original guest buffers and draw
stay unchanged. Modified midpoint inputs are expanded in submission order into a
private float vertex buffer, with the original primitive topology. Vertex shading
and rasterization of these effects remain on the GPU.

This makes the same interpolation implementation available for Garo ribbons, ice
breath/pop/freezing particles, burst particles, snow, forest particles, sparks,
sword trails, shockwaves, bubbles/pops, fireworks, chest particles, fire impacts,
water droplets/ripples/splashes, leaves/debris, moth transforms, and torch UV motion.
Existing camera, skeleton, water-material and UI uniform interpolation remains
shared. Existing GPU ripple/splash paths remain as fallbacks when the new guarded
input path does not produce an override.

Screen glare is one exception: its existing fix interpolates already-shaded
positions/colors. GPU mode automatically uses CPU vertex processing for the small,
verified glare batch and the existing CPU replay, with GPU rasterization. This
preserves movement/color/intensity behavior without replacing its math. The setting
still says GPU; it is not a promise that every draw uses a hardware vertex shader.

The normalized day-close screen copy also uses CPU vertex processing for its
four-vertex batch. Its recognition requires shaded texture coordinates, including
any shader-generated coordinates. The GPU input-bound estimate does not provide
those outputs. World geometry still uses GPU vertex processing, and the existing
texture provenance, TEV, matrix and shaded-UV checks remain mandatory.

## Isolation rules

- Only the verified world shader and small candidate attribute layouts are sampled.
- Snapshot reads validate range, stride, attribute formats, index widths and bounds.
- Input-cache ordering matches the CPU path, including repeated indices and eviction.
- Attribute-to-register mappings, fixed values and missing components are retained.
- The packed float layout selects its matching generated vertex shader too; byte/
  short conversion and emulation flags must not survive from a native layout.
- The native layout and shader are restored after the private draw.
- Existing material/geometry/scene/frame/cut guards are unchanged.
- Two-pass atlas blends submit both private batches in the CPU path's draw order.
- No extra CPU vertex shader execution is used for world-effect input snapshots.
- No change to the user's selected mode, existing settings, assets or saves.

## Automated verification

`mm3d_cpu_vertex_replay_tests` includes GPU snapshot/packing checks for byte, signed
byte, short and float attributes; padding and defaults; register mappings; 8/16-bit
indices; vertex offsets; cache eviction; invalid inputs; and native-memory isolation.
Its existing interpreter/JIT shader replay checks remain enabled.

An optional directory argument reads local `song-effect-*.json` captures. The tool
reconstructs float input buffers and verifies identical submitted input registers
between the captured CPU sequence and the GPU snapshot/packing path. It also checks
that untouched attributes survive isolated midpoint edits. No captures are shipped.
This establishes transport equivalence; it is not visual verification of each scene.

`mm3d_vulkan_fixture --gpu-interpolation` performs an offscreen GPU render/readback
using the production packed layout. It checks supported list/strip/fan topology,
attribute mapping, fixed/default values and two half-opacity draws. It opens no
emulator window and does not touch the running game.

`mm3d_camera_midpoint_tests` retains the per-effect history, matching and cut guards.

### Results (2026-10-23)

- Built `azahar`, `mm3d_cpu_vertex_replay_tests`,
  `mm3d_camera_midpoint_tests`, and `mm3d_vulkan_fixture` successfully.
- GPU snapshot/packing unit checks and existing CPU interpreter/JIT replay checks
  passed, including malformed input and non-indexed offset overflow rejection.
- Scanned all 146 local song-effect capture files (9.73 GiB). Of these, 117 had
  eligible recorded inputs: **293,908 draws / 9,781,324 submitted vertices** passed
  exact input transport comparisons and source-buffer isolation checks.
- Camera/effect interpolation regression tests passed.
- Offscreen Vulkan render/readback passed on Apple M1 Pro through MoltenVK. Two
  half-alpha passes use source-over blending, so their expected red/green result
  also verifies submission order. Unsupported primitive topologies are skipped.
  The initial sandboxed attempt could not access Metal; the same fixture passed
  with GPU access outside the sandbox, using temporary application data.
- `git diff --check` passed.

These checks validate input transport, the shared interpolation calculations, and
GPU layout/blending on this device. They do not establish visual correctness in
every game scene or parity on every driver. No per-effect manual retest was
requested; the running game and selected interpolation mode were left alone.

### Day-close GPU discrepancy

The user's subsequent live log isolated an additional presentation-path bug:
GPU day-close draws rejected the clear/copy pair with HUD reason 5 and
`uv=0,0,0,0`, despite the complete padded scene transfer being tracked. The raw
GPU attribute-bound estimate indexes an input buffer using a mapped shader
register and does not evaluate texture-coordinate shader outputs. CPU mode
already obtains the bounds from shaded vertices. This recognition difference
was outside the particle input-transport comparisons above.

GPU mode now selects the existing CPU vertex path for only the normalized,
opaque four-vertex screen-copy candidate with the verified projection/model.
It does not relax scene provenance or accept zero UV bounds. The regression
covers the logged zero-bound failure, mono/stereo shaded bounds, clear/copy
pair completion, and exclusion of ordinary quads, world depth, stencil,
alpha fades, other geometry and unnormalized transforms.

Validation: rebuilt `azahar` and both replay/midpoint test targets; the GPU
day-close routing and mono/stereo clear-pair regression, CPU interpreter/JIT
replay checks, and camera/effect midpoint tests passed. This fixes the identified
rejection path; the running game was not restarted for visual verification.
