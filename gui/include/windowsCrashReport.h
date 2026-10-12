// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_WINDOWSCRASHREPORT_H
#define CHIAKI_WINDOWSCRASHREPORT_H

class QString;

// P5M for Windows: when P5M crashes (or aborts), writes crash-<time>.txt
// to the log folder: the exception, and the call stack as module+offset
// (no memory, no data), for Report a problem. Symbols stay with us
// (the PDB is a CI artifact).
void installWindowsCrashReport(const QString &log_dir, const QString &version);

#endif // CHIAKI_WINDOWSCRASHREPORT_H
