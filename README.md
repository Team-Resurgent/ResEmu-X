<div align="center">

# ResEmu-X

**A fork of [xemu](https://xemu.app), the original Xbox emulator, featuring
unofficial enhancements by Team Resurgent.**

</div>

<p align="center">
  <a href="https://github.com/Team-Resurgent/ResEmu-X/blob/teamresurgent/LICENSE"><img src="https://img.shields.io/badge/License-GPLv2-blue.svg" alt="License: GPL v2"></a>
  <a href="https://github.com/Team-Resurgent/ResEmu-X/actions/workflows/build.yml"><img src="https://github.com/Team-Resurgent/ResEmu-X/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="https://discord.gg/VcdSfajQGK"><img src="https://img.shields.io/badge/chat-on%20discord-7289da.svg?logo=discord" alt="Discord"></a>
</p>

<p align="center">
  <a href="https://ko-fi.com/J3J7L5UMN"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi"></a>
  <a href="https://www.patreon.com/teamresurgent"><img src="https://img.shields.io/badge/Patreon-F96854?style=for-the-badge&logo=patreon&logoColor=white" alt="Patreon"></a>
</p>

<p align="center">
  <a href="https://github.com/Team-Resurgent/ResEmu-X/releases/latest"><img src="https://img.shields.io/badge/download-latest-brightgreen.svg?style=for-the-badge&logo=github" alt="Download"></a>
</p>

> **ResEmu-X is independently maintained by Team Resurgent and is not affiliated
> with or supported by the upstream xemu project.**

This repository is a fork of xemu maintained by **Team Resurgent**. The default
`teamresurgent` branch tracks upstream xemu and adds the enhancements listed
below. All of xemu's original functionality, documentation, and licensing still
apply — see the [upstream project](https://github.com/xemu-project/xemu) and
[xemu.app](https://xemu.app) for general usage and configuration. However,
because these enhancements are unofficial, please report any problems you hit
with this fork to Team Resurgent — via the
[ResEmu-X issue tracker](https://github.com/Team-Resurgent/ResEmu-X/issues) or
[Discord](https://discord.gg/VcdSfajQGK) — and **not** to the upstream xemu
project.

## What this fork adds

Features on the `teamresurgent` branch that are not (yet) in upstream xemu:

- **Xenium modchip emulation** — emulate the Xenium modchip, including its flash
  and banking. Enable it and point it at a modchip BIOS in *Settings → System →
  Modchip* (no command-line `-device` needed); it overlays the configured MCPX
  Boot ROM. With Xenium disabled, xemu boots normally from the selected Flash ROM.

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

- **`rdpmc` compatibility stub** — the `rdpmc` performance-counter instruction
  returns 0 instead of faulting, so titles that use it keep running instead of
  crashing (helps with certain titles / fast-CPU setups).

## Downloads

Prebuilt Windows, Linux, and macOS binaries:
[**latest** release](../../releases/tag/latest).

## Building from source

The build works exactly as upstream xemu — see its
[build documentation](https://github.com/xemu-project/xemu#building) for
platform prerequisites and options.

## Credits & license

xemu is created and maintained by the xemu project and contributors; this fork
would not exist without their work. Licensed under **GPL-2.0** (see
[`LICENSE`](LICENSE) / [`COPYING`](COPYING)), the same as upstream xemu.
