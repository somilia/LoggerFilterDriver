#ifndef __LOGGERFILTER_H__
#define __LOGGERFILTER_H__


// Path to the file that we want to monitor : C:\Temp\file.txt
UNICODE_STRING TargetFilePath = RTL_CONSTANT_STRING(L"\\Device\\HarddiskVolume3\\Temp\\file.txt");
// Name of port used to communicate
const PWSTR LOGGERPortName = L"\\LOGGERPort";

typedef struct _LOGGER_NOTIFICATION {
    UINT64 ProcessId;
	CHAR Time[20];
    CHAR MessageData[256];

} LOGGER_NOTIFICATION, * PLOGGER_NOTIFICATION;


//---------------------------------------------------------------------------
//      Global variables
//---------------------------------------------------------------------------

typedef struct _LOGGER_FILTER_DATA {

    // The filter handle that results from a call to FltRegisterFilter.
    PFLT_FILTER FilterHandle;

    // The object that identifies this driver.
    PDRIVER_OBJECT DriverObject;

    // Listens for incoming connections
    PFLT_PORT ServerPort;

    // User process that connected to the port
    PEPROCESS UserProcess;

    // Client port for a connection to user-mode
    PFLT_PORT ClientPort;

} LOGGER_FILTER_DATA, * PLOGGER_FILTER_DATA;

// Structure that contains all the global data structures used throughout LoggerFilter.
LOGGER_FILTER_DATA LoggerFilterData;


//---------------------------------------------------------------------------
// Context definitions
//---------------------------------------------------------------------------

typedef struct _LOGGER_STREAM_HANDLE_CONTEXT {

    BOOLEAN RescanRequired;

} LOGGER_STREAM_HANDLE_CONTEXT, * PLOGGER_STREAM_HANDLE_CONTEXT;

const FLT_CONTEXT_REGISTRATION ContextRegistration[] = {

    { FLT_STREAMHANDLE_CONTEXT,
      0,
      NULL,
      sizeof(LOGGER_STREAM_HANDLE_CONTEXT),
      'chBS' },

    { FLT_CONTEXT_END }
};


/*************************************************************************
    Prototypes for the startup and unload routines  
    Implementation in LoggerFilter.c
*************************************************************************/

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
LoggerUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

NTSTATUS
LoggerInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
);

NTSTATUS
LoggerQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
LoggerCreatePreRoutine(
    _Inout_ PFLT_CALLBACK_DATA data,
    _In_    PCFLT_RELATED_OBJECTS flt_object,
    _Out_   PVOID* completion_context
);

/*************************************************************************
	Prototypes for the minifilter communication port routines
	Implementation in LoggerFilter.c
*************************************************************************/

NTSTATUS
LoggerPortConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID* ConnectionCookie
);

VOID
LoggerPortDisconnect(
    _In_opt_ PVOID ConnectionCookie
);

NTSTATUS SendMessageToUserMode(
    PVOID messageBuffer,
    ULONG messageSize
);

/*************************************************************************/

void GetFormattedTime(CHAR* buffer, SIZE_T bufferSize);

#endif
