**Third Windows alpha of P5M.** This one is about getting connected: finding a PS5 that doesn't show up, and keeping P5M up to date.

**Updating from alpha 1 or 2:** download this version by hand one last time (see *Install* below). Your settings and consoles are kept. From now on P5M tells you when a new version is out and installs it for you.

## New in alpha 3

- **Updates from inside the app.** When a new P5M is out, *Update to …* appears in the main menu. One press downloads it, checks it, installs it and restarts P5M; your settings and consoles are kept. *Skip this version* hides it until the next one. You can turn the check off in *Settings › General › Check for updates*.
- **Register your PS5 through PSN, even when P5M can't find it on your network.** Once you sign in to PSN (*Settings › General › Remote play (PSN)*), the PS5s on your account show up in the list, including ones not registered yet. Press one and it registers over PSN, no PIN needed.
- **Help when no console shows up.** After 15 seconds with nothing found, the main screen explains the usual fixes: allow P5M in Windows Firewall, set your home network to Private, add the console by its address, or sign in to PSN.
- **Report a problem works without a stream.** The report now includes the app log (the console search, PSN, registration) and a network check: whether your network is Public or Private, the Windows Firewall state and its rules for P5M, and which network adapters are up (VPNs and virtual adapters often hide the PS5). Console names, your PSN account ID and your Windows user name are removed, as well as addresses and keys.

## My PS5 doesn't show up

1. Open *Windows Security › Firewall & network protection › Allow an app through firewall*, find P5M and tick both **Private** and **Public**.
2. In *Settings › Network & internet*, set your home network to **Private**.
3. The PC and the PS5 must be on the same router, with no VPN and not on a guest Wi-Fi. On the PS5, *Settings › System › Remote Play › Enable Remote Play* must be on.
4. Still nothing? Sign in to PSN in P5M and pick your PS5 from the list, or press **Add console by address** and type the PS5's IP (on the PS5: *Settings › Network › Connection Status › View Connection Status*).
5. If none of that works, press **Report a problem** and attach the ZIP to a new issue: it now shows where the search stops.

## Help us test the Direct3D 11 renderer

Still wanted: switch to **Direct3D 11** (*Ctrl + , › General › Advanced options › Video › Renderer Backend*), play a real game for about 5 minutes, then send a report with *Report a problem*. Even "it works fine" reports help.

## Install

1. Download `P5M-0.1.0-alpha.3-win_x64-portable.zip` below and unzip it anywhere you can write to (for example your Downloads or Documents folder, not Program Files, so P5M can update itself).
2. Run `P5M.exe`. If Windows says the Visual C++ runtime is missing, run `vc_redist.x64.exe` from the same folder first.
3. When Windows asks whether P5M may use the network, allow it on **Private and Public** networks.
4. P5M is not signed yet, so Windows SmartScreen may warn about an unknown publisher: choose *More info › Run anyway*.
5. On Windows 11 with **Smart App Control** on, Windows may block P5M outright, with no option to run it. Until P5M is code-signed, the only way around it is to turn Smart App Control off: *Settings › Privacy & security › Windows Security › App & browser control › Smart App Control settings › Off*.

Windows 10 (1803 or later) or Windows 11, 64-bit. P5M keeps its settings apart from an installed chiaki-ng.

## Known limits

- Registering through PSN is new and not yet confirmed on many networks; very strict networks (some mobile or carrier-grade NAT connections) may not connect.
- DualSense haptics over Bluetooth are not supported yet (USB works as in chiaki-ng).

P5M is free software under the GNU AGPL v3, based on chiaki-ng. Not endorsed by Sony Interactive Entertainment.
