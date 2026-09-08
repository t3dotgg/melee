# Metal rendering

`fluid-render.patch` applies inside the pinned runtime's Dolphin checkout,
after the runtime and native boot patches. Copy these headers before the
native build:

| Header | Destination under `Source/Core` |
| --- | --- |
| `MeleeMetalFrameLog.h` | `VideoBackends/Metal` |
| `MeleeRenderConfig.h` | `VideoCommon` |

The renderer uses three drawable buffers at 120 FPS and two at 60 FPS.
Two buffers stalled the 120 FPS game on the tested 60 Hz display. Three
buffers let rendering and the 60 Hz simulation keep up.

The renderer submits game rendering before drawable acquisition and submits
the composed frame before the presentation wait. Metal can finish this work
while the CPU waits. Retina output uses the window's backing scale, including
after a move to another display.

Set `MELEE_METAL_LOW_LATENCY=0` to measure the original submission order and
three drawable buffers with the same executable. `MELEE_METAL_DRAWABLES=2`
or `3` overrides the buffer count for a separate comparison. These changes
need a new launch.

## Frame measurements

Set `MELEE_METAL_FRAME_LOG` to a local CSV output path. Close the game normally
to write the file. Logging stores at most 72,000 frames in memory. It adds no
file writes during play. Frames after this limit are not recorded.

| Column | Measurement |
| --- | --- |
| `frame` | Metal presentation sequence, including the initial blank frame |
| `submit_s` | Host time when the frame is queued for presentation |
| `gpu_start_s` | Earliest start of a render command buffer for this frame |
| `gpu_end_s` | Latest end of a render command buffer for this frame |
| `gpu_busy_ms` | Sum of GPU time for those render command buffers |
| `display_s` | Presentation callback time reported by Core Animation |
| `gpu_submissions` | Number of recorded render command buffers |

All columns ending in `_s` use host-clock seconds. GPU measurements exclude
the separate texture and buffer upload command buffers. The time between
GPU start and end can include idle gaps. Use `gpu_busy_ms` for the sum of
recorded GPU execution time.

A zero `display_s` means that no presentation time was reported before
shutdown. Exclude those rows and the initial loading period from frame
interval statistics. Compare consecutive `display_s` values to measure
presentation callback intervals.

With VSync off, callbacks can exceed the panel's refresh rate. These callbacks
measure presentation throughput. They do not prove that the panel displayed
every complete frame. A 60 Hz display cannot show 120 complete frames per
second. Use a 120 Hz display to assess that output. Callback timing also does
not prove that successive frames contain different game poses.

Run the callback and CSV checks with:

```sh
python3 -m unittest discover -s native/macos/tests -p test_metal_frame_log.py
```
