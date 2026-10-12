// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "windowsCrashReport.h"

#include <QDir>
#include <QString>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <csignal>
#include <cstdio>
#include <cwchar>

namespace {

// Filled at install time: the handler avoids the heap, which may be the
// thing that broke.
wchar_t crash_dir[MAX_PATH];
char crash_version[64];
volatile LONG crash_written = 0;

void writeStr(HANDLE file, const char *s)
{
    DWORD n = 0;
    WriteFile(file, s, DWORD(strlen(s)), &n, nullptr);
}

void writeAddress(HANDLE file, const char *label, DWORD64 pc)
{
    char line[400];
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(pc), &module) && module)
        GetModuleFileNameW(module, path, MAX_PATH);
    // File name only: the folder can carry the user name.
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    snprintf(line, sizeof(line), "%s %ls+0x%llx\r\n", label, name,
        static_cast<unsigned long long>(module ? pc - reinterpret_cast<DWORD64>(module) : pc));
    writeStr(file, line);
}

HANDLE openCrashFile()
{
    if (InterlockedExchange(&crash_written, 1) != 0 || !crash_dir[0])
        return INVALID_HANDLE_VALUE;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH + 64];
    _snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\crash-%04u-%02u-%02u_%02u-%02u-%02u.txt", crash_dir,
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void writeStack(HANDLE file, CONTEXT ctx)
{
    writeStr(file, "Stack:\r\n");
    for (int i = 0; i < 64; i++) {
        const DWORD64 pc = ctx.Rip;
        if (!pc)
            break;
        writeAddress(file, "  ", pc);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(pc, &base, nullptr);
        if (fn) {
            PVOID handler_data = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, fn, &ctx, &handler_data, &establisher, nullptr);
        } else {
            // Leaf function: the return address is on top of the stack.
            if (!ctx.Rsp)
                break;
            ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
            ctx.Rsp += 8;
        }
    }
}

void writeHeader(HANDLE file, const char *what)
{
    char line[256];
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(line, sizeof(line), "P5M %s crashed: %s\r\nTime %04u-%02u-%02u %02u:%02u:%02u, thread %lu\r\n",
        crash_version, what, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentThreadId());
    writeStr(file, line);
}

LONG WINAPI onException(EXCEPTION_POINTERS *ep)
{
    HANDLE file = openCrashFile();
    if (file == INVALID_HANDLE_VALUE)
        return EXCEPTION_CONTINUE_SEARCH;
    char what[160];
    const EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        snprintf(what, sizeof(what), "exception 0x%08lx (%s at 0x%llx)", r->ExceptionCode,
            r->ExceptionInformation[0] == 0 ? "read" : (r->ExceptionInformation[0] == 1 ? "write" : "execute"),
            static_cast<unsigned long long>(r->ExceptionInformation[1]));
    else
        snprintf(what, sizeof(what), "exception 0x%08lx", r->ExceptionCode);
    writeHeader(file, what);
    writeAddress(file, "At", reinterpret_cast<DWORD64>(r->ExceptionAddress));
    writeStack(file, *ep->ContextRecord);
    CloseHandle(file);
    // Windows Error Reporting still runs and ends the process.
    return EXCEPTION_CONTINUE_SEARCH;
}

void onAbort(int)
{
    HANDLE file = openCrashFile();
    if (file != INVALID_HANDLE_VALUE) {
        writeHeader(file, "abort (a fatal error or a failed check)");
        CONTEXT ctx;
        RtlCaptureContext(&ctx);
        writeStack(file, ctx);
        CloseHandle(file);
    }
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

} // namespace

void installWindowsCrashReport(const QString &log_dir, const QString &version)
{
    if (log_dir.isEmpty())
        return;
    const QString native = QDir::toNativeSeparators(log_dir);
    if (native.size() >= MAX_PATH)
        return;
    const int n = native.toWCharArray(crash_dir);
    crash_dir[n] = 0;
    snprintf(crash_version, sizeof(crash_version), "%s", version.toUtf8().constData());
    SetUnhandledExceptionFilter(onException);
    std::signal(SIGABRT, onAbort);
}
