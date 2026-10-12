# Microsoft Store — listing and submission (P5M for Windows)

Texto da página da loja (em inglês, como toda comunicação pública) e as
respostas que a submissão no Partner Center pede. Copiar e colar.

## Product name

P5M

## Short description (até 1000 caracteres? a loja mostra ~270 no topo)

Play your PS5 on your Windows PC over your home network or the internet. Free and open source, with low latency and a controller-friendly interface.

## Description

P5M is a free, open-source Remote Play app for the PlayStation 5. Stream your PS5 to your Windows PC — at home over your local network, or away from home through PSN — and play with a DualSense or another controller.

- Speaks the PS5's newest streaming protocol (version 20), the one Sony's official app uses
- Low latency: game-priority threads, a quality-of-service network flow and an optional Direct3D 11 renderer with flip-model presentation
- Up to 1080p at 60 fps, HDR10 on HDR displays
- Learns your network: each network remembers the bitrate that worked
- No more stuck buttons when the network drops a packet
- DualSense adaptive triggers and haptics over USB
- A controller-friendly interface you can drive without a mouse
- Every session keeps a diary of frame timing and network quality; "Report a problem" saves it, with addresses and accounts removed

You need a PS5 with Remote Play enabled (Settings › System › Remote Play) and a PlayStation account.

P5M is based on chiaki-ng and is free software under the GNU AGPL v3. Source code: https://github.com/beecrepaldi-afk/P5M-Windows

P5M is not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc.

## What's new in this version

First Microsoft Store release.

## Keywords (até 7)

PS5, Remote Play, game streaming, DualSense, PlayStation, open source, chiaki

## Category

Games › (ou Utilities & tools — escolher no Partner Center)

## URLs

- Privacy policy: https://github.com/beecrepaldi-afk/P5M-Windows/blob/windows/PRIVACY.md
- Website: https://github.com/beecrepaldi-afk/P5M-Windows
- Support contact: https://github.com/beecrepaldi-afk/P5M-Windows/issues

## Pricing

Free. Mercados: todos.

## Restricted capability: runFullTrust (justificativa para o campo da submissão)

P5M is a desktop application built with Qt (C++). It runs as a full-trust desktop app packaged with MSIX (Desktop Bridge); it does not use the UWP app container. It needs full trust for raw HID access to game controllers (DualSense), low-latency audio and Direct3D 11 / Vulkan presentation, and UDP networking to the user's PS5 on the local network.

## Notes for certification (campo "Notes for certification")

P5M streams a PlayStation 5 console. Full testing requires a PS5 with Remote Play enabled, so the testers will see the main screen searching the local network for consoles; this is expected without a PS5. The app does not require an account to start. Optional PSN sign-in happens on Sony's own web page. No test account can be provided because PSN accounts are owned by Sony.

## Age rating (questionário IARC)

Responder como "app de utilidade/streaming", sem conteúdo próprio: sem violência, sem compras, sem chat entre usuários, sem compartilhamento de localização. O conteúdo exibido vem dos jogos do próprio usuário.

## Screenshots

Mínimo 1 (recomendado 4), 1366x768 ou maior. A CI gera prints do app aberto
(artifact `P5M-smoke-test`); prints em resolução maior e com um jogo em
andamento são melhores e vêm de um PC real.
