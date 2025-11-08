#include "CrashGuard.h"
#include <cstdio>
#include <string>
 #ifdef _WIN32
 #define NOMINMAX
 #include <windows.h>
 #include <dbghelp.h>
 #pragma comment(lib, "dbghelp.lib")
 #endif
 
 namespace {
 #ifdef _WIN32
     static LONG WINAPI UnhandledExceptionFilterFn(EXCEPTION_POINTERS *ep) {
         FILE *out = stderr;
         std::fprintf(out, "\n[CrashGuard] Unhandled exception: code=0x%08lx\n", ep->ExceptionRecord->ExceptionCode);
 
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);

        // 构造符号搜索路径：包含可执行所在目录、当前工作目录以及环境变量 _NT_SYMBOL_PATH
        char exePathA[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, exePathA, MAX_PATH);
        std::string exeDir = exePathA;
        size_t pos = exeDir.find_last_of("\\/");
        if (pos != std::string::npos) exeDir = exeDir.substr(0, pos);

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

        if (!SymInitialize(process, searchPath.c_str(), TRUE)) {
            std::fprintf(out, "[CrashGuard] SymInitialize failed (%lu)\n", GetLastError());
            std::fflush(out);
            return EXCEPTION_EXECUTE_HANDLER;
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
 
         std::fprintf(out, "[CrashGuard] Call stack (most recent first):\n");
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
                     std::fprintf(out, "#%02d  %s  (%s:%lu)\n", i, symbol->Name, lineInfo.FileName, (unsigned long)lineInfo.LineNumber);
                 } else {
                     std::fprintf(out, "#%02d  %s  [0x%llx]\n", i, symbol->Name, (unsigned long long)addr);
                 }
             } else {
                 std::fprintf(out, "#%02d  [0x%llx]\n", i, (unsigned long long)addr);
             }
         }
         std::fflush(out);
         SymCleanup(process);
         return EXCEPTION_EXECUTE_HANDLER; // 防止系统弹窗
     }
 #endif
 }
 
 namespace CrashGuard {
 
 void install() {
 #ifdef _WIN32
     SetUnhandledExceptionFilter(UnhandledExceptionFilterFn);
 #endif
 }
 
 }