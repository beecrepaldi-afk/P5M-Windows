**Second Windows alpha of P5M.** Our build machine now opens the app with both renderers on every build and checks the screen, so this one is known to start and draw its interface. If you try it, please tell us how it went with *Report a problem* in the app, even if everything works.

## 🙏 Help us test the Direct3D 11 renderer

P5M has a new renderer built for Windows (Direct3D 11) that should put frames on screen faster than the default (Vulkan). We need real PCs to prove it — **5 minutes of your time helps a lot**:

1. Open P5M and press **Ctrl + ,** (or choose *Settings* with the controller).
2. Go to **General › Advanced options › Video › Renderer Backend** and pick **Direct3D 11**. P5M restarts.
3. Connect to your PS5 and **play a real game for about 5 minutes** (menus barely move, so they don't tell us much).
4. Leave the stream, then press **Report a problem** on the main screen. P5M saves `P5M-report-….zip` to your Desktop — with your addresses, accounts and keys already removed — and opens a form here on GitHub.
5. Drag the ZIP into the form, add a line about your setup (Wi-Fi or cable, home or away), and submit. A free GitHub account is needed.

Even "it works fine" reports are useful: the diary inside tells us how long each frame takes to reach your screen.

## New since alpha 1

- Windows wording: "this PC", and the key hints show Enter and Ctrl (they showed Mac keys)
- No more Steam shortcut prompt on first start
- The PSN reminder points to Settings › General › Remote play (PSN)
- Animated background shader is now built in
- Direct3D 11 renderer:
  - skips blending the interface over the video while the interface is empty (most of a game)
  - asks Windows for the highest GPU priority it grants
  - writes *frame → screen* time to the session diary every 10 s, so we can compare it with Vulkan

## What's in it

P5M for Windows shares its streaming core with [P5M for macOS](https://github.com/beecrepaldi-afk/P5M-mac):

- **PlayStation streaming protocol version 20**, the one the PS5 and Sony's official app use (open-source players stop at version 12)
- **No more stuck buttons** when the network drops a packet
- **Less waiting on a lossy network**: packets that error correction can rebuild don't hold the picture back
- **Sound closer to the picture** (10 ms audio blocks, adaptive queue)
- **Learns your network**: each router remembers the bitrate that worked
- **Faster PSN remote connection**

And tuning for Windows:

- The stream's traffic joins a Windows audio/video quality-of-service flow (video priority on Wi-Fi)
- Network and render threads run at game priority (MMCSS), with a 1 ms timer and no power throttling while you play
- **New, optional Direct3D 11 renderer** (Settings › Renderer Backend › Direct3D 11): hardware decoding and presentation on one device, flip-model presentation, tearing when VSync is off, HDR10 when Windows has HDR on. Vulkan stays the default until we have reports from real PCs.

## Install

1. Download `P5M-0.1.0-alpha.2-win_x64-portable.zip` below and unzip it anywhere.
2. Run `P5M.exe`. If Windows says the Visual C++ runtime is missing, run `vc_redist.x64.exe` from the same folder first.
3. P5M is not signed yet, so Windows SmartScreen may warn about an unknown publisher: choose *More info › Run anyway*.
4. On Windows 11 with **Smart App Control** on, Windows may block P5M outright ("Smart App Control blocked an app that may be unsafe"), with no option to run it. Until P5M is code-signed, the only way around it is to turn Smart App Control off: *Settings › Privacy & security › Windows Security › App & browser control › Smart App Control settings › Off*.

Windows 10 (1803 or later) or Windows 11, 64-bit. P5M keeps its settings apart from an installed chiaki-ng.

## Known limits

- DualSense haptics over Bluetooth are not supported yet (USB works as in chiaki-ng).

P5M is free software under the GNU AGPL v3, based on chiaki-ng. Not endorsed by Sony Interactive Entertainment.
