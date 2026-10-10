# P5M para Windows — estado do port

Ramo `windows`, criado em 10/10/2026 a partir do commit `699b5798` (beta 4
do Mac).

## O que vem do Mac sem mudar nada

Tudo que está em `lib/` é C portável e já passa por `#ifdef __APPLE__`
onde precisa (conferido com `x86_64-w64-mingw32-gcc -fsyntax-only` em todos
os arquivos alterados, e o holepunch à mão):

- **Takion v20 por padrão** (`P5M_TAKION_V20=0` volta à v12), com a
  negociação `TAKIONPROTOCOLREQUEST`, cabeçalho estendido, ECDH P‑521 e os
  gatilhos de 8 bytes extras — ver `mac/TAKION-V20.md`.
- **Botão preso**: um evento por pacote e reenvio do último 3x a cada 30 ms
  (`feedbacksender.c`).
- **FEC sem espera**: perda coberta pelo FEC não segura mais 16 ms
  (`takion.c`, `p5m_fec_covers_gap`).
- **PSN mais rápida**: UPnP assíncrono (2,5 s), ctrl em 1,5 s, STUN do
  Google na frente, `get_websocket_fqdn` com 5 tentativas.
- **Áudio de 10 ms** (era 50) e fila de som adaptativa — no Windows ela
  mede a fila do SDL, o mesmo caminho que o Mac usa sem o CoreAudio nativo.
- **Diários** `[frames]`, `[reorder]`, `[packets]`; DSCP AF41 e
  `SO_RCVBUF` de 1 MB.
- Interface nova (QML `p5m/`), atalho R1+L3+R3, PSN remota em 1080p,
  sanitização de log, "Report a problem".

As opções exclusivas do Mac já somem sozinhas no QML
(`Qt.platform.os === "osx"`) e o backend Metal volta para Vulkan fora do
macOS (`settings.cpp`).

## O que é só do Windows (novo neste ramo)

| Mac | Windows | Onde |
|---|---|---|
| `SO_NET_SERVICE_TYPE` = vídeo interativo | **qWAVE** `QOSTrafficTypeAudioVideo` (DSCP CS5, 802.1p 5 → WMM vídeo no Wi‑Fi). O Windows ignora `IP_TOS` de programas | `lib/src/takion.c` (`p5m_win_qos_*`), link `qwave` |
| QoS `USER_INTERACTIVE` nas threads de rede | **MMCSS "Games"** em prioridade alta (fallback: `ABOVE_NORMAL`) nas threads takion, envio, feedback, ctrl e congestionamento, e na thread de render | `gui/src/windowsStreamTuning.cpp` |
| `setMacStreamActivity` | Durante o stream: `timeBeginPeriod(1)` e **sem power throttling/EcoQoS** (inclusive o que ignora a resolução do timer com a janela atrás) | idem |
| `macNetworkKey` (MAC do roteador) | `GetBestRoute2` + `GetIpNetEntry2`/`ResolveIpNetEntry2` → aprendizado de bitrate por rede liga no Windows | idem + `streamsession.cpp` |
| Nome P5M, versão própria | `P5M`/`P5M_WIN_VERSION`, configurações e logs separados do chiaki-ng | `main.cpp`, `gui/CMakeLists.txt` |

## Renderizador Direct3D 11 (opção, 10/10)

`gui/src/d3d11renderer.cpp`, mesma interface do `MetalRenderer` (a janela
usa `NativeRenderer`; o valor 2 do backend é "nativo": Metal no Mac,
Direct3D 11 no Windows, salvo como `d3d11`). Escolhido em Settings ›
Renderer Backend; **o padrão continua Vulkan** até haver diário de PC real.

- Um dispositivo D3D11 para tudo: o D3D11VA decodifica nele
  (`nativeHwDeviceCtx`), a fatia é copiada na GPU para uma textura NV12/P010
  amostrável (sem passar pela CPU), o Qt Quick desenha a interface com o RHI
  D3D11 numa textura nossa.
- Swapchain flip model (`FLIP_DISCARD`, 3 buffers), objeto de espera com
  latência 1 (o mesmo "um present por vez" do Metal), tearing com VSync
  desligado.
- HDR10 (PQ/BT.2020 + metadados) quando o Windows está com HDR ligado; senão
  o HDR é mapeado para SDR com a curva do Quest (reta até 0,8, ombro até
  1000 nits, branco do painel 240 nits).
- Diário `[d3d11] 10s:` com o modo de apresentação (`composed (DWM copy)`,
  `overlay`, `independent flip`), presents/s e tempo do Present.
- Shaders HLSL compilados na abertura (`d3dcompiler_47`, do próprio Windows);
  validados aqui com o `glslangValidator`.
- Abordagem (não código) inspirada no `d3d11va.cpp` do Moonlight-qt. Fica
  para depois: cópia zero de verdade (amostrar a fatia do decodificador) e o
  upscaling da NVIDIA/Intel (`VideoProcessorSetStreamExtension`, ver mpv).

## Build

Não há build Windows nesta máquina. A CI
`.github/workflows/build-windows-p5m.yml` (clang-cl + vcpkg + Qt 6.9, a
mesma receita do chiaki-ng) roda em push no ramo `windows`, em tags
`win-v*` ou à mão, e gera `P5M-<versão>-win_x64-portable.zip` com
`P5M.exe`. O instalador Inno do chiaki-ng ficou de fora (tem a marca dele).

## Não portado (e por quê)

- Renderizador Metal, MetalFX, pacing por display link, HDR do CAMetalLayer:
  no Windows fica o Vulkan/libplacebo do chiaki-ng, que já faz HDR.
- VideoToolbox direto: o equivalente (D3D11VA/Vulkan Video pelo ffmpeg) já é
  o caminho do chiaki-ng.
- CoreAudio adaptativo, Core Haptics e háptica do DualSense por Bluetooth
  (`macDualSense*`): o Windows segue com SDL e a háptica por USB
  ("Wireless Controller" como dispositivo de áudio de 4 canais).
- Carimbo de chegada do kernel (`SO_TIMESTAMP_MONOTONIC`): o Windows só tem
  `SIO_TIMESTAMPING` com suporte da placa; os diários usam o horário do
  processo.

## Próximos passos sugeridos (por valor)

1. Primeira build pela CI e teste num PC com Windows 10/11 (LAN e PSN).
2. Saída de áudio WASAPI com `IAudioClient3` (período mínimo do motor,
   ~3 ms em vez dos ~10–20 do modo compartilhado) — o "CoreAudio
   adaptativo" do Windows.
3. Apresentação: conferir no diário se o Vulkan está em `FIFO`/`MAILBOX` e
   se a janela em tela cheia entra em *independent flip*; medir
   frame→screen como no Mac.
4. Háptica do DualSense por Bluetooth (relatório 0x32 pelo hidapi, como no
   Quest e no Mac).
5. Instalador próprio (Inno com a marca P5M) e assinatura.

**Nada testado em Windows real ainda.** Sem commit; publicar só com o ok do
dono.
