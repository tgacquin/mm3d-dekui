# Interpolation enable freeze: presentation handoff

The two Windows logs supplied for issue #26 stop within approximately 0.1 seconds
of enabling interpolation. Both identify an AMD 5600G integrated GPU, one shared
graphics/presentation queue, and a requested two-image swapchain. The unmapped
guest reads occur long before enabling interpolation; the logs do not establish
those reads as the freeze's cause.

The presentation code contains a CPU-side deadlock when only two render frames
are available:

1. The presenter retains the last displayed frame for paused overlay redraws.
2. The renderer draws the midpoint into the other frame and flushes its GPU work.
3. `Present()` records a worker callback that will enqueue the frame for the
   presenter, but originally did not dispatch that callback.
4. The native half of the same pair calls `GetRenderFrame()`. Both frames are
   owned, so it waits for the presenter to release one.
5. The presenter cannot release the retained frame until it receives its
   replacement. That notification is still buffered on the blocked render
   thread. No future game draw can dispatch it.

Ordinary game draws between presentations can hide the missing dispatch at
30 fps. Additional render frames can also hide it. No Vulkan device error is
required for this stall, and it is not inherently specific to AMD or Windows.

The handoff must dispatch its worker callback immediately after recording it,
while preserving worker ordering after the preceding render submission. It
does not need a GPU completion wait, another swapchain image, or disabled
interpolation.

## Regression exercise

Build `mm3d_present_fixture` and run `build-macos/bin/Release/mm3d_present_fixture`
(or the corresponding Windows executable). This is a synthetic window with no
ROM or running emulator instance. Use an external timeout when testing the
unfixed code.

The fixture checks out excess render frames so only two are usable even on a
host with a larger swapchain. It then exercises consecutive interpolated
presentations, a burst of frame reuse, paused overlay redraws, clean-background
readback, resume and resize. The unused checked-out frames have no submitted
GPU work and are destroyed at teardown without waiting on their reset fences.

A local regression reproduction establishes this deadlock, but affected users
still need to verify the updated Windows build; these logs alone cannot rule
out other independent problems on their machines.

## Local verification (2026-10-23)

- Before the fix: the real Vulkan fixture reached the two-frame exercise and
  stalled until the external 20-second timeout terminated it.
- After the fix: the identical fixture completed successfully for both async
  preference settings, including paused redraw, readback, resume and resize.
  The primary overlay uses its presenter thread for either preference.
- Host: Apple M1 Pro / MoltenVK, with excess render frames reserved to reproduce
  the two-frame condition. This verifies the ownership/handoff failure without
  relying on the reporting user's GPU driver.
- `azahar`, `mm3d_present_fixture` and `mm3d_camera_midpoint_tests` built;
  `ctest --test-dir build-macos -R '^mm3d_camera_midpoint$' --output-on-failure`
  passed. No running emulator was restarted.
