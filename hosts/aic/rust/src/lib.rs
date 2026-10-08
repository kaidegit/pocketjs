//! `pocketjs-aic` — the ArtInChip luban-lite staticlib.
//!
//! The crate body stays empty on purpose: every `#[no_mangle] pub unsafe
//! extern "C"` symbol of the four dependency crates lands in this archive
//! because they are linked in as rlibs. The exported surface is the one the
//! ESP-IDF components document:
//!
//! - `pocketjs_native_ui_*`   (`hosts/esp-idf/native/ui-core`)  — the
//!   retained UI core behind `pocketjs/ui_core.h`;
//! - `pocketjs_native_renderer_*`, `pocketjs_native_target_*` and the stats
//!   helpers (`hosts/esp-idf/native/render-rgb565`) — behind
//!   `pocketjs/render_rgb565.h`;
//! - the ABI layout helpers (`hosts/esp-idf/native/abi`).
//!
//! The C host supplies `pocketjs_idf_rust_alloc`, `pocketjs_idf_rust_dealloc`
//! and `pocketjs_idf_rust_panic` (`hosts/aic/port/pocketjs_rust_alloc.c`).
//! Building this crate as one archive keeps a single copy of `pocketjs-core`
//! in the firmware, so the ESP-IDF symbol-namespacing wrapper does not apply.

#![no_std]

// Loading the four rlibs into this archive is the crate's whole job. Each
// `extern crate` forces the dependency to load even though nothing here
// calls it; the `#[no_mangle]` exports and the `#[panic_handler]` from
// pocketjs_idf_runtime then reach the final archive.
extern crate pocketjs_idf_abi;
extern crate pocketjs_idf_runtime;
extern crate pocketjs_idf_ui_core;
extern crate pocketjs_idf_render_rgb565;

#[cfg(not(target_arch = "riscv32"))]
compile_error!(
  "pocketjs-aic builds for the luban-lite E907F: --target riscv32imafc-unknown-none-elf"
);
