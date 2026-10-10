// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "windowsStreamTuning.h"

#include <chiaki/thread.h>

#include <QString>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <avrt.h>
#include <timeapi.h>

#include <atomic>

void setWindowsStreamActivity(bool active)
{
	static std::atomic<bool> current{false};
	if (current.exchange(active) == active)
		return;

	// Windows 11 lets a process lose its timer resolution and run on the
	// efficiency cores (EcoQoS) once its window is hidden or not in front;
	// the stream would then wake late in 15.6 ms steps. Opt out while it runs.
	PROCESS_POWER_THROTTLING_STATE throttling = {};
	throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
	throttling.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
#endif
	// StateMask 0 with the bit in ControlMask = "never throttle"; ControlMask
	// 0 hands the decision back to the system.
	if (!active)
		throttling.ControlMask = 0;
	throttling.StateMask = 0;
	SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));

	if (active)
		timeBeginPeriod(1);
	else
		timeEndPeriod(1);
}

void setCurrentThreadGamePriority()
{
	DWORD task_index = 0;
	HANDLE task = AvSetMmThreadCharacteristicsW(L"Games", &task_index);
	if (task)
		AvSetMmThreadPriority(task, AVRT_PRIORITY_HIGH);
	else
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	// MMCSS lets go of the registration when the thread ends.
}

static void streamThreadPriority(ChiakiThreadName name, void *)
{
	switch (name)
	{
		case CHIAKI_THREAD_NAME_TAKION:
		case CHIAKI_THREAD_NAME_TAKION_SEND:
		case CHIAKI_THREAD_NAME_FEEDBACK:
		case CHIAKI_THREAD_NAME_CTRL:
		case CHIAKI_THREAD_NAME_CONGESTION:
			setCurrentThreadGamePriority();
			break;
		default:
			break;
	}
}

void installWindowsStreamThreadPriority()
{
	chiaki_thread_set_affinity_cb(streamThreadPriority, nullptr);
}

QString windowsNetworkKey()
{
	// The route Windows would take to the internet names the router (next
	// hop); its neighbour entry holds the router's hardware address.
	SOCKADDR_INET dest = {};
	dest.Ipv4.sin_family = AF_INET;
	dest.Ipv4.sin_addr.s_addr = htonl(0x08080808); // any public address works
	MIB_IPFORWARD_ROW2 route = {};
	SOCKADDR_INET source = {};
	if (GetBestRoute2(nullptr, 0, nullptr, &dest, 0, &route, &source) != NO_ERROR)
		return QString();
	if (route.NextHop.si_family != AF_INET || route.NextHop.Ipv4.sin_addr.s_addr == 0)
		return QString();

	char ip[INET_ADDRSTRLEN] = {};
	inet_ntop(AF_INET, &route.NextHop.Ipv4.sin_addr, ip, sizeof(ip));

	MIB_IPNET_ROW2 neighbour = {};
	neighbour.Address = route.NextHop;
	neighbour.InterfaceIndex = route.InterfaceIndex;
	if (GetIpNetEntry2(&neighbour) != NO_ERROR || neighbour.PhysicalAddressLength != 6)
	{
		// Not in the cache yet: ask for it (one ARP, a few ms at most).
		if (ResolveIpNetEntry2(&neighbour, nullptr) != NO_ERROR || neighbour.PhysicalAddressLength != 6)
			return QString::fromLatin1(ip);
	}
	QString mac;
	for (ULONG i = 0; i < neighbour.PhysicalAddressLength; i++)
	{
		if (i)
			mac += QLatin1Char(':');
		mac += QStringLiteral("%1").arg(neighbour.PhysicalAddress[i], 2, 16, QLatin1Char('0'));
	}
	return mac;
}
