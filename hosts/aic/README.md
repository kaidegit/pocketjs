# PocketJS on ArtInChip D12x

This directory hosts PocketJS guests on ArtInChip SoCs running luban-lite.
`hosts/aic` follows the same shape as `hosts/sifli`: a luban-lite application
keeps its `main.c` and references this checkout through `POCKETJS_ROOT`; no
application code programs the display or the touch panel.

The D12x (T-Head E907F, `rv32imafc` with the `ilp32f` ABI) renders purely in
software: the RGB565 backend in `engine/backends/rgb565` rasterizes damage
rectangles on the CPU, and the aicfb display engine scans the result. The 2D
engine (GE) stays unused.

## How a frame reaches the panel

```
app + pocket.host.json
        │ tools/aic.ts package (PocketJS CLI, target aic-d12x-demo68)
        ▼
aic-d12x-demo68.pocket ── generated/pocket_bin.c ── d12x_os.itb (rodata)
        │
        ▼
port/pocketjs_host.c    one guest: QuickJS realm, pak, frame loop
        │  pocketjs_ui_turn ── retained core ── DrawList words
        │  pocketjs_rgb565_prepare/render_strip/commit ── CPU raster into the
        │  non-scanned aicfb buffer, one strip per damage rectangle
        ▼
aicfb PAN_DISPLAY + WAIT_FOR_VSYNC
```

Each of the two framebuffers owns a render target, so damage tracking knows
what each buffer already shows. The first render and structural DrawList
changes repaint the whole buffer; a static screen reaches `damage=0`.

## The pieces

| Path | Responsibility |
| --- | --- |
| `rust/` | The `pocketjs-aic` staticlib: one archive with the four ESP-IDF native crates, one copy of `pocketjs-core`, the software renderer, and the `pocketjs_native_*` C ABI |
| `port/pocketjs_host.c` | Package load, guest mount, input, render, present, frame pacing |
| `port/pocketjs_display.c` | `mpp_fb` open, screen info, `PAN_DISPLAY`, `WAIT_FOR_VSYNC`, cache maintenance |
| `port/pocketjs_touch.c` | Touch device binding, active contact snapshots, panel-range coordinates |
| `port/pocketjs_time.h` | Tick deadline comparisons and frame waits across the 32-bit tick wrap |
| `port/pocketjs_heap.c` | `heap_caps_*` inside a private TLSF pool, with the offset-slot alignment the reused components need |
| `port/esp_compat/` | `esp_err.h`, `esp_log.h`, `esp_heap_caps.h` shims so the portable component sources compile unmodified |
| `third_party/quickjs-ng/` | quickjs-ng 0.14.0, the QuickJS the guest realm pins; `quickjs-libc.c` compiles with `-D__wasi__`, the restricted-POSIX profile that keeps console and file helpers |
| `SConscript` | cargo build, source list, include paths, archive link |
| `examples/demo68/` | The touch demo package: 480x272, density 1, 60 Hz |

The component C sources come from `hosts/esp-idf/components` in this same
repository and are never copied. Their headers document the API contracts.

**The AIC input recorder retains two seconds of history.** A touch frame
owns a copy of its contact array; the default 36,000-frame recorder can
exhaust the guest heap before old touch frames are overwritten. The AIC
build bounds all input lanes to `2 * tickHz` frames.

The touch port stores the driver's DOWN/MOVE/UP reports in **five active
contact slots**. A stationary contact survives a frame without a report;
UP removes it. An empty snapshot clears the core's hit-at-down captures,
so a new touch with a reused GT911 id resolves a new hit. The host scales
panel coordinates from the reported range to the logical viewport.

**GT911 GET_INFO must report the range used by its samples.** With no
coordinate transforms, the SDK reads this range from the controller's
resident configuration. Kconfig defaults do not program that configuration;
overwriting a 480x272 range with 1024x600 makes viewport scaling miss the
button. The SDK retains its configured ranges for transformed reports.

**Luban's tick starts at `0xffff0000`** and wraps after about 65 seconds at
1,000 Hz. Calibration starts with the current tick, and frame waits use
the modular deadline difference. A failed guest turn prints the frame,
contact count, tick, guest heap, and pool usage before exiting the task.

## Build a firmware

```sh
bun tools/aic.ts package          # compiles the guest, writes the contract header,
                                  # emits generated/pocket_bin.c
cd <luban-lite>
scons --apply-def=d12x_demo68-nor_rt-thread_pocketjs_defconfig
POCKETJS_ROOT=/path/to/pocketjs scons -j8
```

`bun tools/aic.ts build` runs the same scons steps with `POCKETJS_ROOT` set.

The application directory is `application/rt-thread/pocketjs` in the SDK:
`main.c` calls `pocketjs_aic_run_embedded` on the
`pocketjs_embedded_package` array, and the defconfig turns LVGL and the
startup UI off because the PocketJS host owns the framebuffer.

A successful build produces
`output/d12x_demo68-nor_rt-thread_pocketjs/images/d12x_demo68-nor_v1.0.0.img`.
The guest package links into the firmware: `generated/pocket_bin.c` holds the
`.pocket` bytes as a const array, the port parses it in place from
flash-backed rodata, and no filesystem participates in loading. The flash
layout follows `target/d12x/demo68-nor/pack/image_cfg.json`: `os` at 0x100000
and `os_r` at 0x400000, each 3072 KB, `data` at 0x700000 (9216 KB) unused by
this port. The `.img` file is a container: each section inside it carries its
own flash offset, so flash section files at their partition offsets instead
of writing the container raw.

## Contracts

`tools/aic.ts package` writes `generated/pocketjs_aic_contract.h` from
`pocket.host.json`. The firmware refuses a package whose `target_id`, host
ABI, tick rate, viewport, density, presentation, or profile hash differs —
`pocketjs_package_select` compares the embedded host inputs against the
compiled-in contract, so a stale package linked into the firmware fails
admission instead of rendering with the wrong assumptions.

The guest bundle evaluates against the `pocketjs_native_ui_*` core and the
`pocketjs_native_renderer_*` software rasterizer; the accelerator argument
stays null, so every rectangle, blend, and blit runs on the CPU.

## Verification

- `bun tools/aic.ts package` twice — the `.pocket` bytes are identical.
- `bun tools/pocket-pack.ts verify <file.pocket>` — footer hash and plan
  re-admission on the artifact itself.
- `cargo build --release --locked --no-default-features --target
  riscv32imafc-unknown-none-elf` under `hosts/aic/rust` and a symbol audit:
  the archive exports the `pocketjs_native_*` surface and leaves only
  `pocketjs_idf_rust_alloc`, `pocketjs_idf_rust_dealloc`, and
  `pocketjs_idf_rust_panic` undefined; `port/pocketjs_heap.c` covers the
  first two through `ui_core.c`, and the panic hook aborts the firmware.
- The full scons build of `d12x_demo68-nor_rt-thread_pocketjs` and the size
  report against the 3072 KB `os` partition.
- On the board: the serial line `[PocketJS] fps=… damage=… px=… full=…
  js_heap=…/…` once per second, the demo UI, and touch moving the count.

## Limits

- One guest runs at a time; there is no launcher or guest switching yet.
- The `native` presentation is the only one the host implements; the logical
  viewport equals the panel at density 1.- Strips render in place in the framebuffer, so the host asserts
  `stride == width * 2` at startup; a padded stride needs a scratch-strip
  path first.
- `CONFIG_DATA_ORDER_BGR` decides the byte order the display engine reads;
  if red and blue swap on the panel, fix that config, not the renderer.
- The os partition is 3072 KB; the firmware plus the embedded Hero package
  takes about 2660 KB of it. A package larger than the remaining headroom
  needs the `os` partition widened again or an external load path. The
  QuickJS heap limit is 4 MB of RT-Thread heap.
