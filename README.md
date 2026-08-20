<div align="center">

# xemu — Team-Resurgent fork

**A fork of [xemu](https://xemu.app), the original Xbox emulator
([xemu-project/xemu](https://github.com/xemu-project/xemu)).**

</div>

This repository is a fork of xemu maintained by **Team-Resurgent**. The default
`teamresurgent` branch tracks upstream xemu and adds the enhancements listed
below. All of xemu's original functionality, documentation, and licensing still
apply — see the [upstream project](https://github.com/xemu-project/xemu) and
[xemu.app](https://xemu.app) for general usage, configuration, and help.

## What this fork adds

Features on the `teamresurgent` branch that are not (yet) in upstream xemu:

- **CCI compressed disc images** — mount `.cci` Xbox disc containers directly,
  including multi-part (`name.1.cci`, `name.2.cci`, …) images. The container
  format is implemented by the standalone
  [libCCI](https://github.com/Team-Resurgent/libCCI) library rather than
  vendored in-tree, so the same reader/writer can be shared by other tools.

- **USB keyboard & mouse from the UI** — attach an emulated **Xbox Keyboard**
  (`usb-kbd`), **Xbox Mouse (Relative)** (`usb-mouse`), or **Xbox Mouse
  (Absolute)** (`usb-tablet`) to any player port straight from *Settings →
  Input* — no monitor commands needed. Includes **per-keyboard routing**: pick
  which physical keyboard drives the emulated controller so one keyboard can act
  as the gamepad while another types into the guest.

- **NV2A render-to-vertex-buffer (R2VB)** — support for effects such as
  DisplacementMap that render into a vertex buffer.

- **NV2A signed texture formats** — adds the missing `V16U16` signed texture
  format (and HILO hemisphere dot-map handling).

- **`rdpmc` compatibility stub** — the `rdpmc` instruction returns 0 instead of
  raising `#UD`, for compatibility with certain titles / fast-CPU setups.

## Downloads

Every push to `teamresurgent` builds Windows, Linux, and macOS (arm64 + x86_64)
via GitHub Actions and publishes them to the rolling
[**latest** release](../../releases/tag/latest).

## Building from source

The build works exactly as upstream xemu. In short:

```sh
git clone https://github.com/Team-Resurgent/xemu-cci
cd xemu-cci
./build.sh              # native (Linux/macOS); use -p win64-cross for Windows
```

See upstream's [build documentation](https://github.com/xemu-project/xemu#building)
for platform prerequisites and options.

## Credits & license

xemu is created and maintained by the xemu project and contributors; this fork
would not exist without their work. Licensed under **GPL-2.0** (see
[`LICENSE`](LICENSE) / [`COPYING`](COPYING)), the same as upstream xemu.
