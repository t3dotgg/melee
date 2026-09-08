# Stage lighting

The native build has stage-specific lighting for Fountain of Dreams,
Battlefield, and Final Destination. Enable Stage lighting in Settings,
Video. Light and material changes apply when play resumes. Water resolution
and Final Destination's sky colors apply when the stage is loaded again.
`MELEE_STAGE_LIGHTING=0` selects the original lighting at launch.

| Stage | Changes |
| --- | --- |
| Fountain of Dreams | Blue and pink point lights, neutral ambient fill, reduced material highlights, and a 320 × 240 live water reflection |
| Battlefield | Warm key light, cool fill, warm platform trim, and controlled cloud highlights |
| Final Destination | Cooler fighter lighting, a slow light-color cycle, blue and gold trim, and revised colors across all six animated sky layers |

Fountain's original reflection is 80 × 60. The new buffer has 16 times as
many pixels and uses 144,000 additional bytes in guest memory. It retains
RGB565 and the existing reflection shader. Camera, image, and buffer sizes
stay consistent. An old save with a smaller cached buffer keeps its original
reflection size until a sufficiently large buffer is available.

The lighting hooks change HSD light colors and directions before the game
uploads them. They change stage material colors after animation has run.
The hooks restore edited source fields after drawing and before saving or
loading a state. Shared objects are changed only once per render. Alpha,
collision, controls, and animation timing are preserved.

Much of Battlefield and Final Destination uses baked vertex lighting. Those
meshes retain their existing shading. Final Destination's unlit trim uses
specific authored magenta, white, and orange material colors. The new palette
changes only those constant materials on the platform.

Final Destination's sky variant is generated during app packaging. The tool
checks the original stage file's hash and changes only direct vertex RGB
values. It preserves geometry, texture data, alpha, animation, and archive
layout. The original stage file stays in the bundled Game folder. A narrow
disc-read override selects the same-size variant in `Resources/Lighting`
when lighting is enabled. Generated DAT files remain local and are not
committed or uploaded.

## Validation on 2026-09-08

The test Mac has an M5 Max, macOS 26.5.2, and a 60 Hz display. Tests used
the upscaled texture pack and 4x internal resolution. Computer use checked
the game view and the live lighting switch. The first Fountain comparison
had washed-out columns, so the point lights and material highlights were
reduced before the final comparison.

The final warmed samples used two idle human players and the same stage
setup for both settings. Each sample lasted five seconds.

| Stage | Original lighting | New lighting |
| --- | ---: | ---: |
| Fountain of Dreams | 60.01 FPS | 60.00 FPS |
| Battlefield | 60.00 FPS | 59.99 FPS |
| Final Destination | 59.99 FPS | 60.00 FPS |

Earlier cold samples were slower. These short warmed samples do not establish
performance for every match or display. A 120 Hz display still needs its own
visual check.

Live memory inspection confirmed a 320 × 240 RGB565 Fountain image and a
matching camera viewport and scissor. The Final Destination read log confirmed
that the bundled sky variant loaded. All three stage tests shut down with
zero CPU fallback steps and zero failed code checks.

The native app build, signature check, and complete matching GameCube build
pass. The original executable retains SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45`. The native test suite includes
lighting restoration, render-mode guards, old reflection buffers, sky byte
bounds, alpha preservation, and bundled variant selection.

This is Theo's fully automated slop experiment. It is not meant for serious
use or investigation. No support, maintenance, or human review is promised.
