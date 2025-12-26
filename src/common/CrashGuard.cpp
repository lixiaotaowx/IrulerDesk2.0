#include "CrashGuard.h"
#include <cstdio>
#include <cstdarg>
#include <string>
 #ifdef _WIN32
 #define NOMINMAX
 #include <windows.h>
 #include <dbghelp.h>
 #pragma comment(lib, "dbghelp.lib")
 #endif
 
 namespace {
 #ifdef _WIN32
    static void vwritef(FILE *a, FILE *b, const char *fmt, va_list ap) {
        if (a) {
            va_list ap2;
            va_copy(ap2, ap);
            std::vfprintf(a, fmt, ap2);
            va_end(ap2);
        }
        if (b && b != a) {
            std::vfprintf(b, fmt, ap);
        }
        if (a) std::fflush(a);
        if (b && b != a) std::fflush(b);
    }

    static void writef(FILE *a, FILE *b, const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vwritef(a, b, fmt, ap);
        va_end(ap);
    }

    static LONG WINAPI UnhandledExceptionFilterFn(EXCEPTION_POINTERS *ep) {
        FILE *out = stderr;
        char exePathA[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, exePathA, MAX_PATH);
        std::string exeDir = exePathA;
        size_t pos = exeDir.find_last_of("\\/");
        if (pos != std::string::npos) exeDir = exeDir.substr(0, pos);

        SYSTEMTIME st;
        GetLocalTime(&st);
        char stamp[64] = {0};
        std::snprintf(stamp, sizeof(stamp), "%04u%02u%02u_%02u%02u%02u_%03u",
                      (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                      (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                      (unsigned)st.wMilliseconds);

        std::string logPath = exeDir + "\\crash_" + stamp + ".log";
        FILE *fileOut = std::fopen(logPath.c_str(), "wb");

        writef(out, fileOut, "\n[CrashGuard] Unhandled exception: code=0x%08lx\n", ep->ExceptionRecord->ExceptionCode);
 
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        DWORD pid = GetCurrentProcessId();

        std::string dmpPath = exeDir + "\\crash_" + stamp + ".dmp";
        HANDLE hFile = CreateFileA(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mdei;
            mdei.ThreadId = GetCurrentThreadId();
            mdei.ExceptionPointers = ep;
            mdei.ClientPointers = FALSE;
            const BOOL ok = MiniDumpWriteDump(process, pid, hFile, MiniDumpNormal, &mdei, nullptr, nullptr);
            CloseHandle(hFile);
            writef(out, fileOut, "[CrashGuard] MiniDumpWriteDump: %s\n", ok ? "ok" : "failed");
            if (!ok) {
                writef(out, fileOut, "[CrashGuard] MiniDumpWriteDump error=%lu\n", GetLastError());
            } else {
                writef(out, fileOut, "[CrashGuard] DumpPath: %s\n", dmpPath.c_str());
            }
        } else {
            writef(out, fileOut, "[CrashGuard] CreateFile(dmp) failed: %lu\n", GetLastError());
        }

        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);

        // 构造符号搜索路径：包含可执行所在目录、当前工作目录以及环境变量 _NT_SYMBOL_PATH
        char cwdA[MAX_PATH] = {0};
        GetCurrentDirectoryA(MAX_PATH, cwdA);

        char ntSymPath[4096] = {0};
        DWORD envLen = GetEnvironmentVariableA("_NT_SYMBOL_PATH", ntSymPath, sizeof(ntSymPath));
        std::string searchPath;
        if (envLen > 0) {
            searchPath = std::string(ntSymPath) + ";" + exeDir + ";" + std::string(cwdA);
        } else {
            searchPath = exeDir + ";" + std::string(cwdA);
        }

        BOOL symInitOk = SymInitialize(process, searchPath.c_str(), TRUE);
        if (!symInitOk) {
            writef(out, fileOut, "[CrashGuard] SymInitialize failed (%lu)\n", GetLastError());
            symInitOk = SymInitialize(process, nullptr, TRUE);
            if (symInitOk) {
                SymSetSearchPath(process, searchPath.c_str());
            }
        }
 
         CONTEXT ctx = *ep->ContextRecord; // 复制上下文
 #ifdef _M_X64
         DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
         STACKFRAME64 frame = {0};
         frame.AddrPC.Offset = ctx.Rip;
         frame.AddrPC.Mode = AddrModeFlat;
         frame.AddrFrame.Offset = ctx.Rbp;
         frame.AddrFrame.Mode = AddrModeFlat;
         frame.AddrStack.Offset = ctx.Rsp;
         frame.AddrStack.Mode = AddrModeFlat;
 #else
         DWORD machineType = IMAGE_FILE_MACHINE_I386;
         STACKFRAME64 frame = {0};
         frame.AddrPC.Offset = ctx.Eip;
         frame.AddrPC.Mode = AddrModeFlat;
         frame.AddrFrame.Offset = ctx.Ebp;
         frame.AddrFrame.Mode = AddrModeFlat;
         frame.AddrStack.Offset = ctx.Esp;
         frame.AddrStack.Mode = AddrModeFlat;
 #endif
 
        writef(out, fileOut, "[CrashGuard] Call stack (most recent first):\n");
        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(machineType, process, thread, &frame, &ctx, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                break;
            }
            DWORD64 addr = frame.AddrPC.Offset;
            if (addr == 0) break;
            char buffer[sizeof(SYMBOL_INFO) + 256] = {0};
            PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD lineDisplacement = 0;
            IMAGEHLP_LINE64 lineInfo = {0};
            lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

            if (SymFromAddr(process, addr, nullptr, symbol)) {
                if (SymGetLineFromAddr64(process, addr, &lineDisplacement, &lineInfo)) {
                    writef(out, fileOut, "#%02d  %s  (%s:%lu)\n", i, symbol->Name, lineInfo.FileName, (unsigned long)lineInfo.LineNumber);
                } else {
                    writef(out, fileOut, "#%02d  %s  [0x%llx]\n", i, symbol->Name, (unsigned long long)addr);
                }
            } else {
                writef(out, fileOut, "#%02d  [0x%llx]\n", i, (unsigned long long)addr);
            }
        }
        if (fileOut) {
            std::fclose(fileOut);
        }
        if (symInitOk) {
            SymCleanup(process);
        }
        ExitProcess(1);
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif
 }
 
namespace CrashGuard {
 
 void install() {
 #ifdef _WIN32
     SetUnhandledExceptionFilter(UnhandledExceptionFilterFn);
 #endif
 }

 bool writeMiniDump(const char *tag) {
 #ifdef _WIN32
     char exePathA[MAX_PATH] = {0};
     GetModuleFileNameA(nullptr, exePathA, MAX_PATH);
     std::string exeDir = exePathA;
     size_t pos = exeDir.find_last_of("\\/");
     if (pos != std::string::npos) exeDir = exeDir.substr(0, pos);
 
     SYSTEMTIME st;
     GetLocalTime(&st);
     char stamp[64] = {0};
     std::snprintf(stamp, sizeof(stamp), "%04u%02u%02u_%02u%02u%02u_%03u",
                   (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                   (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                   (unsigned)st.wMilliseconds);
 
     std::string tagStr = (tag && *tag) ? tag : "dump";
     std::string dmpPath = exeDir + "\\hang_" + tagStr + "_" + stamp + ".dmp";
     HANDLE hFile = CreateFileA(dmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
     if (hFile == INVALID_HANDLE_VALUE) {
         return false;
     }
 
     HANDLE process = GetCurrentProcess();
     DWORD pid = GetCurrentProcessId();
     const BOOL ok = MiniDumpWriteDump(process, pid, hFile,
                                       static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                                       nullptr, nullptr, nullptr);
     CloseHandle(hFile);
     return ok == TRUE;
 #else
     (void)tag;
     return false;
 #endif
 }
 
 }
