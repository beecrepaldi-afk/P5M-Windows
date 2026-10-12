// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_WINDOWSNETWORKREPORT_H
#define CHIAKI_WINDOWSNETWORKREPORT_H

class QString;

// For Report a problem: what usually hides a PS5 from P5M on Windows. The
// category of the connected networks (Public/Private), the firewall state
// and its rules for P5M.exe, and the network adapters that are up. No
// network names or addresses.
QString windowsNetworkReport();

#endif // CHIAKI_WINDOWSNETWORKREPORT_H
