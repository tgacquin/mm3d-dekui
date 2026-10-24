# Experimental hybrid fragment shaders

This is the first stage of a hybrid shader renderer, **not a complete ubershader
implementation or a guarantee of stutter-free gameplay**. It is disabled by
default and does not change saved settings or the transferable cache format.

Enable for a developer run with `DEKUI_HYBRID_SHADERS=1`. For a Steam launch
option, use `DEKUI_HYBRID_SHADERS=1 %command%`. Remove the variable to return to
the existing renderer. Do not combine this test with the presentation-handoff
experiment. `DEKUI_HYBRID_SHADERS=force` forces the fallback for visual comparison;
it is not a recommended performance setting.

On macOS the experiment additionally requires `MVK_CONFIG_FAST_MATH_ENABLED=0`
in the launching process. The renderer refuses the experiment without it:
Metal fast math produced different rounding/alpha-test outcomes between the two
paths in GPU equivalence tests. Normal rendering keeps its existing behavior.
A separate `-hybrid-strict` driver cache on macOS prevents reusing machine code
compiled with normal fast math. The ROM-free hybrid fixture sets this variable
only inside its own process.
Other drivers still require their own equivalence/performance validation.

## What is implemented

- A fragment shader interprets all six texture-combiner stages and the alpha test
  from draw-specific uniforms, including modifiers, scales, 8-bit rounding,
  DOT3 alpha, first-stage Previous handling and delayed combiner-buffer updates.
- Specialized shader/pipeline compilation proceeds asynchronously. While a
  specialized pipeline is unavailable, a compatible fallback pipeline draws the
  geometry. The feature does not deliberately skip draws.
- Fallback keys retain lighting, texture sampling, fog, depth/scissor, blending,
  custom-normal and atlas distinctions. Fixed-function and vertex state are
  preserved in the fallback pipeline key.
- Known fragment families and pipeline variants are prepared during disk-cache
  loading. Runtime variants stay out of the transferable shader files; the
  existing driver pipeline cache can still retain their compiled code.
- The extra uniform range and comparison/upload work are enabled only for this
  experiment. Existing fragment/atlas uniform offsets remain unchanged.
- The experiment applies to the supported US MM3D title and can assist both CPU
  and GPU rendering modes, since both use Vulkan fragment shading.

## Limits and remaining work

A new lighting/sampling family or a new Vulkan pipeline configuration can still
block on first use. A cold cache does not yet contain a complete ready-to-use
fallback set. Programmable vertex shaders and geometry shaders are not replaced
by interpreters. Broader fallback coverage and bounded pipeline preparation are
required before this can deliver Dolphin-style cold-cache behavior.

The fallback has more branches and texture-source selection work. Its cost and
background compilation contention must be measured on Steam Deck, with both
cold and warm caches, before any default or release changes. Shutdown logs report
fallback draw selections and selections that encountered an unfinished fallback
pipeline (these are not elapsed-time measurements).

## Verification

Build `mm3d_vulkan_fixture` and run it with `--hybrid-shaders`. It uses temporary
user data and no ROM. It compiles real fragment/atlas variants and executes the
actual generated specialized and fallback TEV code in a GPU compute fixture,
comparing intermediate and final results for 1,024 input cases. It also builds
real fallback graphics pipelines and verifies family reuse and fixed-function
state separation. Texture samples are deterministic fixture values;
this is arithmetic/interface coverage, not a substitute for in-game visual or
performance tests. Other renderer fixtures must continue to pass with the
experiment disabled.
