# Native frame timing

`../patches/frame-timing.patch` applies at the Dolphin source root in
`ModernGekko-Template/lib/ModernGekko/vendor/dolphin`. The base runtime commit is
`55c7b023fa`. It does not change the original game executable.

## Fixed video cadence

Melee schedules its input alarm at 60 Hz in `lb_80019628`. The scene loop in
`gm_801A4D34` consumes every queued input sample, runs game updates, then renders
once. The original NTSC VI cadence is approximately 59.94 Hz. Those independent
clocks drift. The queue can then contain two game updates for one rendered
frame.

`MELEE_RENDER_FPS=60` or `120` selects an exact native VI cadence. The patch
divides cumulative CPU cycles at each half-line boundary. The remainder carries
between half-lines, so integer rounding does not shorten every scanline. At
120 Hz, two fields occupy exactly one original 60 Hz game update. The game timer
and audio clock keep their original rates.

An unset or unrecognized value keeps the original VI timing. Deterministic
sessions also keep the original VI timing. The setting is read once per process.
The separate game render patch must supply the additional scene draw for 120
distinct frames. VI timing alone cannot create another game image.

## Mac waits

The original precision timer sleeps until 1.02 ms before its deadline, then calls
the scheduler repeatedly. On macOS, both the ordinary sleep and a direct
`mach_wait_until` can have their wake times moved by timer coalescing.

The native timer uses a critical `kqueue` timer with an absolute Mach deadline,
then waits on the CPU for at most the final 200 microseconds. The final wait uses
the CPU yield or pause instruction. It does not give the thread back to the
scheduler. A failed kernel timer falls back to `mach_wait_until`.

The CPU and GPU pacing threads request interactive QoS and latency tier zero
once. Other timer users keep their thread policy. The app also needs its active
gameplay process activity to stop App Nap. The renderer patch owns that activity.

The runtime exports `MeleeNativeWaitNanoseconds(uint64_t delay)` for the generated
C render code. That code resolves the function with `dlsym`. Both paths then use
the same wait implementation and thread timer. The argument is a relative delay
to avoid mixing clock epochs. The caller rechecks its absolute deadline after
each wait. The function caps a single wait at one second.

## Checks

Run the focused tests with no game data or runtime build:

```sh
python3 native/macos/timing/test_timing.py
```

The tests compile the actual helper headers extracted from the patch. They
check exact full-frame cycle totals, a full minute without accumulated rounding
drift, unequal field sizes, expired and short deadlines, and a rejected kernel
timer. Mac timer tests require macOS.

Run the isolated wait benchmark:

```sh
python3 native/macos/timing/test_timing.py --benchmark
```

Both waits use the same process activity and thread policy. Each rate runs for
five seconds per timer. One Apple Silicon run on September 8, 2026 gave:

| Rate | Timer | p99 lateness | Maximum lateness | One thread CPU use |
| --- | --- | --- | --- | --- |
| 60 Hz | Original | 15.126 us | 51.751 us | 1.252% |
| 60 Hz | Native | 0.293 us | 0.959 us | 1.142% |
| 120 Hz | Original | 6.376 us | 296.292 us | 3.334% |
| 120 Hz | Native | 0.293 us | 52.084 us | 2.292% |

These are host wait measurements. They do not measure input-to-display latency
or certify sustained game performance. Background activity can change the
results. A later run gave 120 Hz p99 lateness of 5.947 ms for the native timer and
11.489 ms for the original timer. The initial background tests also showed
millisecond wake delays with both the original wait and plain Mach waits.

`Timer.cpp`, `CoreTiming.cpp`, and `VideoInterface.cpp` compiled with the actual
ARM64 runtime flags in isolated output files. Full game verification belongs to
the integrated build.
