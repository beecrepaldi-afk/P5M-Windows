# P5M for Windows

P5M is a PS5 Remote Play app based on chiaki-ng. This is the Windows edition: the same streaming core as [P5M for macOS](https://github.com/beecrepaldi-afk/P5M-mac), tuned for Windows 10 and 11.

> **Alpha — and it works.** On October 11, 2026 the first tester ran P5M on a real PC (Windows 11, Ryzen 7 7800X3D) with a **PS5 Pro**, both on Ethernet: the console streamed over protocol version 20 at a steady 1080p 60 fps, with no packet loss and no audio dropouts. Every build is also opened on GitHub's Windows machines, with both renderers, before it ships. If you try P5M, please use *Report a problem* — every diary helps.

## What's new in P5M

**Speaks the PS5's newest language.** The console and Sony's official app stream over version 20 of the PlayStation streaming protocol. Open-source players stopped at version 12. By studying how the official app behaves, we mapped version 20 end to end: a stronger key exchange, new protection that keeps sound clean when the network drops packets, and the new adaptive trigger format.

**No more stuck buttons.** Releasing several buttons at once used to travel in a single packet; if the network lost it, the console kept the button held. P5M sends each change on its own and repeats the last one.

**Less waiting on a lossy network.** When a missing packet can be rebuilt from the error-correction data already received, P5M moves on right away instead of waiting for it.

**Sound closer to the picture.** Audio blocks are 10 ms instead of 50, and the sound queue grows or shrinks with what your network needs.

**Learns your network.** Each network (identified by its router) remembers the bitrate that worked last time. Clean sessions climb, lossy ones step back.

**Faster connection over the internet.** PSN remote play reaches the console sooner: no blocking router setup, reliable STUN servers first and retries while the network wakes up.

**Built for Windows.** The stream's network traffic joins a Windows audio/video quality-of-service flow (Wi-Fi gives it the video priority class), the network and render threads run under the Multimedia Class Scheduler at game priority, and while you play Windows keeps a 1 ms timer and does not throttle the app to save power, even when its window is in the background.

**A Direct3D 11 renderer, built for Windows** *(optional)*. Like Metal on the Mac, it draws the stream with the system's own graphics API instead of Vulkan: the video is decoded and shown on the same device, frames go out through Windows' flip-model presentation one at a time, with tearing allowed when VSync is off and HDR10 when Windows has HDR on. While nothing is on screen but the game, it skips blending the interface over the video, and it asks Windows for the highest GPU priority. Turn it on in *Settings › Advanced options › Renderer Backend*. Vulkan stays the default until diaries from real PCs show which is faster — the Direct3D 11 diary records how long each frame takes to reach the screen.

**Measured, not guessed.** Every session keeps a diary with network packets, frame timing and bandwidth.

**Something wrong?** Use *Report a problem* in the app. It saves your last session diary — with addresses, accounts and keys removed — to your Desktop and opens a short form here on GitHub.

## Download

Get the latest `P5M-*-win_x64-portable.zip` from [Releases](../../releases), unzip it anywhere and run `P5M.exe`. Windows 10 (1803 or later) or 11, 64-bit.

- P5M is not signed yet, so SmartScreen may warn about an unknown publisher: choose *More info › Run anyway*.
- If Windows says the Visual C++ runtime is missing, run `vc_redist.x64.exe` from the same folder.
- P5M keeps its settings apart from an installed chiaki-ng.

## Help us test

The most useful report right now: switch to the **Direct3D 11** renderer, play a real game for a few minutes, then use *Report a problem*. Reports with Wi-Fi, with PSN (playing away from home) and with a DualSense over USB are just as welcome.

P5M for Windows is created by [beecrepaldi-afk](https://github.com/beecrepaldi-afk). See [AUTHORS.md](AUTHORS.md). P5M is free software under the GNU AGPL v3, like chiaki-ng.

The upstream project and attribution follow below.

---

![chiaki-ng Logo](gui/res/chiaking-logo.svg)

# [chiaki-ng](https://streetpea.github.io/chiaki-ng/)

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://streetpea.github.io/chiaki-ng/).

## Discord
[chiaki-ng community Discord](https://discord.gg/tAMbRuwXDH)

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.
