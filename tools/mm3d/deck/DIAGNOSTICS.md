# Opt-in performance captures

Normal builds hide these controls and do not automatically start a capture.
Set this Steam launch option for the native Linux AppImage (no Proton):

```text
DEKUI_DIAGNOSTICS=1 %command%
```

Or launch from a terminal beside the AppImage:

```sh
DEKUI_DIAGNOSTICS=1 ./Dekui.AppImage
```

Open F1, select Diagnostics, then Capture performance (30 seconds). Close F1
within the three-second delay and reproduce the slowdown for 30 seconds.
The build revision and graphics settings are included in the log. Record the
scene separately. Compare Original and CPU interpolation at the same scene,
camera angle and resolution. Keep settings unchanged during each capture.

The tab displays the log directory. After the captures, quit normally and copy
`azahar_log.txt` before relaunching, which rotates the log. Portable packages use
`user/log/azahar_log.txt` beside the AppImage. Several captures can share one log;
they have session identifiers. Wait for each capture to finish before starting
another. Changing interpolation enablement cancels an active capture.

The report contains CPU scopes, replay batching, waits and GPU submission timing
where timestamps are supported. Scopes may overlap; submission timing includes
GPU waits and does not measure utilization or display latency. Capturing itself
adds overhead. Use matching captures for comparisons. Nothing is uploaded.

Remove the launch option and restart to hide the tab again. This option is not
saved in user settings. It does not enable the developer editor or change the
rendering algorithm. It is a support switch, not a password or access control.

## Presentation scheduling comparison

To isolate the 1.1.0 presentation-handoff change while retaining the two-frame
deadlock protection, compare the default launch above with:

```text
DEKUI_DIAGNOSTICS=1 DEKUI_DEFER_PRESENT_HANDOFF=1 %command%
```

Keep CPU interpolation, resolution, location and camera unchanged. Restart for
each run and capture 30 seconds after gameplay settles. The log reports either
`Presentation handoff dispatch=immediate` (default) or
`Presentation handoff dispatch=before wait (diagnostic)`.

The diagnostic mode leaves the handoff buffered while render frames are
available, as in the old batching schedule. It explicitly dispatches before
waiting for a free frame and completes worker handoffs before waiting for
presentation during pause, resize or readback. It does not remove animation
coverage, relax depth-cache freshness, or wait for GPU completion at dispatch.
This is an isolation test, not a confirmed performance fix. Remove the extra
environment variable and restart to restore the default; nothing is saved.
