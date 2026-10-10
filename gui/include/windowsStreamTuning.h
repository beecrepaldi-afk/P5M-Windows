// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_WINDOWSSTREAMTUNING_H
#define CHIAKI_WINDOWSSTREAMTUNING_H

// P5M for Windows: the counterparts of macStreamActivity.h.

class QString;

// While a stream runs: 1 ms timer resolution and no power throttling
// (EcoQoS) for the process, so the network and render threads wake on time
// even when the window is not in front.
void setWindowsStreamActivity(bool active);

// The calling thread joins the MMCSS "Games" task at high priority
// (fallback: above-normal thread priority).
void setCurrentThreadGamePriority();

// Network, send, feedback, control and congestion threads of the stream get
// setCurrentThreadGamePriority() as they start.
void installWindowsStreamThreadPriority();

// Hardware address of the router this PC goes out through ("aa:bb:..."),
// or its IP when the address is unknown; empty without a route. Keys the
// per-network bitrate learning, like macNetworkKey().
QString windowsNetworkKey();

#endif // CHIAKI_WINDOWSSTREAMTUNING_H
