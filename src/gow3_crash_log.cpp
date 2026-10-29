// gow3 - ReXGlue Recompiled Project
//
// Logs where an unhandled host fault happened (exception code, address and its offset in gow3.exe),
// for the first few faults the runtime's own handlers pass on. The build writes gow3.map (linker
// map), which names the recompiled function at that offset: sub_<guest address>.

#include "gow3_crash_log.h"

#include <windows.h>

#include <atomic>

#include <rex/logging.h>

namespace rex::glue {
namespace {

std::atomic<int> g_logged{0};

LONG CALLBACK LogFault(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  // Faults, not the C++ / debugger / thread-name notifications that pass through here.
  if (code < 0xC0000000u || code == 0xE06D7363u) return EXCEPTION_CONTINUE_SEARCH;
  if (g_logged.fetch_add(1) >= 4) return EXCEPTION_CONTINUE_SEARCH;
  const uintptr_t at = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
  HMODULE module = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(at), &module)) {
    GetModuleFileNameA(module, name, MAX_PATH);
  }
  const uintptr_t offset = module ? at - reinterpret_cast<uintptr_t>(module) : 0;
  REXLOG_ERROR("CRASHLOG exception 0x{:08X} at 0x{:X} = {} + 0x{:X} (look up the offset in gow3.map)",
               code, at, name, offset);
  if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
    REXLOG_ERROR("CRASHLOG {} of 0x{:X}", info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                 info->ExceptionRecord->ExceptionInformation[1]);
  }
  // The callers: unwind the faulting context frame by frame (x64 unwind data), offsets in gow3.exe.
  CONTEXT context = *info->ContextRecord;
  const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  for (int frame = 0; frame < 24 && context.Rip; ++frame) {
    DWORD64 image_base = 0;
    RUNTIME_FUNCTION* function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
    if (image_base == exe) {
      REXLOG_ERROR("CRASHLOG   frame {:2} gow3.exe + 0x{:X}", frame, context.Rip - exe);
    }
    if (!function) break;
    void* handler_data = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data,
                     &establisher, nullptr);
  }
  rex::FlushLogging();
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashLog() {
  // Last in line: the runtime's handlers (guest memory, MMIO) see every fault first.
  AddVectoredExceptionHandler(0, &LogFault);
}

}  // namespace rex::glue
