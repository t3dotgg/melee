# Native visual updates at 120 FPS

`high_refresh.py` adds hooks to the generated USA v1.02 game loop. The
verified GameCube executable stays unchanged. Set `MELEE_RENDER_FPS=120`
to enable the extra drawing path with matching runtime timing. An unset value
or `MELEE_RENDER_FPS=60` uses the original drawing path.

The game still runs its input, animation clocks, collision, and game rules
at 60 updates per second. After each normal render, the hook draws one extra
frame. It predicts joint position, Euler rotation, and camera position
half a frame ahead using the previous two poses. This avoids the extra input
delay of drawing a blend of old frames.

The extra frame uses new geometry. It is not a second copy of the same XFB.
Before the extra render, the hook calls the original `VIWaitForRetrace`. This
sleeps the guest game thread until the next 120 Hz VI interrupt. The runtime can
continue controller alarms and audio during this wait. CoreTiming supplies the
single host clock. The runtime must also use
120 Hz VI timing, immediate XFB presentation, and no immediate-XFB cap.

The hook restores the exact saved transform and matrix bits before the next game
update and marks derived matrices dirty. Prediction stops across fighter action or
spawn changes, scene changes, skipped game updates, frame rewinds, large
position changes, and large pose changes. It skips quaternion joints and
joints with custom matrices. It keeps scale unchanged to avoid extra scale-vector
allocation during rendering. These cases still get the extra draw.

This is an experimental visual change. Texture animation, interface counters,
and particle systems that do not use joint transforms can still update at
60 Hz. Render callbacks run twice and can have their own visual side effects.
This does not make collision or input processing run at 120 Hz.

The addresses come from `config/GALE01/symbols.txt`. The hook boundaries are
`gm_801A4D34` at `0x801A5034` and `0x801A5058`. The repeated block invalidates
GX caches, starts rendering, runs render callbacks, and copies the XFB. It
contains no game-object process update. The extra render calls the original
`VIWaitForRetrace` at `0x8034F314` and resumes at `0x801A5034`. Structure offsets come from
`gobj.h`, `jobj.h`, `cobj.h`, `wobj.h`, and `ft/types.h`.

Run the focused installer and pose tests with:

```sh
python3 -m unittest discover -s native/macos/tests -p test_high_refresh.py
```

These tests check predicted poses, exact restoration, camera positions,
action changes, frame rewinds, teleports, angle wrapping, removed objects,
the match-camera rewrite, quaternion descendants, and the 60 FPS fallback.
Runtime frame pacing and motion need a game run.

Set `MELEE_REFRESH_STATS=1` to log render totals, frames with a predicted pose,
the current simulation counter, and measured render rate every two seconds.
The first line reports `wait=guest-vi` for the extra render path. Pose counts show
that transforms changed, but do not measure display scanout or input latency.

The match camera rewrites its WObjs during every render. A second generated
hook at `0x80030200` reapplies the predicted camera after `Camera_8002A4AC` and
before drawing. Quaternion and custom-matrix joints keep their local pose,
but their derived matrices are also saved and restored. This keeps frozen
bones attached to predicted parents.
