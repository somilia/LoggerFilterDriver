/*++
Module Name:
    LoggerFilter.c

Abstract:
    This module implements a minifilter that intercepts file create or open operations.
    The minifilter registers itself with the filter manager and sets up a communication
    port to interact with a user-mode application. Whenever a file creation or open operation
    is detected, the associated process information is gathered and a notification is sent
    to the user-mode application. The user-mode application then logs these events.

Environment:
    Kernel mode
--*/


#include <fltKernel.h>
#include <dontuse.h>
#include <ntstrsafe.h>
#include "loggerFilter.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


// Assign text sections for each routine.
#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, LoggerUnload)
#pragma alloc_text(PAGE, LoggerInstanceSetup)
#pragma alloc_text(PAGE, LoggerQueryTeardown)
#pragma alloc_text(PAGE, LoggerCreatePreRoutine)
#pragma alloc_text(PAGE, LoggerPortConnect)
#pragma alloc_text(PAGE, LoggerPortDisconnect)
#pragma alloc_text(PAGE, SendMessageToUserMode)
#endif


/*************************************************************************
	MiniFilter callback routines.
*************************************************************************/

FLT_PREOP_CALLBACK_STATUS
LoggerCreatePreRoutine(
    _Inout_ PFLT_CALLBACK_DATA data,
    _In_    PCFLT_RELATED_OBJECTS flt_object,
    _Out_   PVOID* completion_context
)
/*
Routine Description:
    This routine is called before a file is created or opened.
    It sends a notification to a user-mode application that logs the events.

Arguments:
    data - Structure containing information about the ongoing operation.
    flt_object - Structure containing opaque handles for the filter, instance, and volume.
    completion_context - Context for the post-operation routine (unused here).

Return Value:
    FLT_PREOP_SUCCESS_NO_CALLBACK - The operation does not require further monitoring.
*/
{
    UNREFERENCED_PARAMETER(flt_object);
    UNREFERENCED_PARAMETER(completion_context);
    NTSTATUS status = STATUS_SUCCESS;
    PFLT_FILE_NAME_INFORMATION name_info = NULL;
    PLOGGER_NOTIFICATION notification = NULL;
    PLOGGER_STREAM_HANDLE_CONTEXT context = NULL;

    status = FltGetFileNameInformation(data,
        FLT_FILE_NAME_NORMALIZED
        | FLT_FILE_NAME_QUERY_DEFAULT,
        &name_info);

    if (!NT_SUCCESS(status)) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    status = FltParseFileNameInformation(name_info);
    if (!NT_SUCCESS(status)) {
        FltReleaseFileNameInformation(name_info);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    // If the file is not the target file, we don't need to monitor it.
    if (RtlCompareUnicodeString(&name_info->Name, &TargetFilePath, TRUE) != 0) {
        FltReleaseFileNameInformation(name_info);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    KdPrint(("[LoggerFilter] " __FUNCTION__ " [%u] Start     to creat/open the file (%wZ)\n",
        PtrToUint(PsGetCurrentProcessId()),
        &name_info->FinalComponent));

    //  If no client port just ignore this write.
    if (LoggerFilterData.ClientPort == NULL) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    status = FltGetStreamHandleContext(flt_object->Instance,
        flt_object->FileObject,
        &context);

    try { // Use try-finally to cleanup

        if (PsGetCurrentProcessId() != NULL) {

            notification = ExAllocatePoolZero(NonPagedPool,
                sizeof(LOGGER_NOTIFICATION),
                'nacS');

            if (notification == NULL) {
                data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                data->IoStatus.Information = 0;
                //returnStatus = FLT_PREOP_COMPLETE;
                leave;
            }

            // Get the current process ID
            notification->ProcessId = (UINT64)(ULONG_PTR)PsGetCurrentProcessId();
            CHAR formattedTime[20];
            GetFormattedTime(formattedTime, sizeof(formattedTime));
            DbgPrint("Current Time: %s\n", formattedTime);

            // Assuming `formattedTime` is a string and `notification->Time` is a character array, use strcpy to copy the string.
            strcpy(notification->Time, formattedTime);
            KdPrint(("Current Time: %s\n", formattedTime));


            // Populate the message data
            RtlCopyMemory(notification->MessageData, "File accessed", sizeof("File accessed"));

            status = SendMessageToUserMode(notification, sizeof(LOGGER_NOTIFICATION));

            if (STATUS_SUCCESS == status) {
                DbgPrint("!!! LoggerFilter.sys --- sent message to user-mode\n");
            }
            else { // Couldn't send message. This sample will let the i/o through.
                DbgPrint("!!! LoggerFilter.sys --- couldn't send message to user-mode, status 0x%X\n", status);
            }
        }
    }
    finally {
        if (notification != NULL) {
            ExFreePoolWithTag(notification, 'nacS');
        }
        if (context) {
            FltReleaseContext(context);
        }
    }
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_OPERATION_REGISTRATION operations[] = {
    {
		IRP_MJ_CREATE,
        0,
        LoggerCreatePreRoutine,
        NULL,
	},
	{ IRP_MJ_OPERATION_END }
};

// This defines what we want to filter with FltMgr
CONST FLT_REGISTRATION FilterRegistration = {

    sizeof(FLT_REGISTRATION),           //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags
    ContextRegistration,                //  Context
    operations,                         //  Operation callbacks
    LoggerUnload,                       //  FilterUnload
    LoggerInstanceSetup,                //  InstanceSetup
    LoggerQueryTeardown,                //  InstanceQueryTeardown
    NULL,                               //  InstanceTeardownStart
    NULL,                               //  InstanceTeardownComplete
    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent
};


/*************************************************************************
    Filter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
/*
Routine Description:
    This is the initialization routine for this miniFilter driver. 
    This registers the miniFilter with FltMgr and initializes all 
    its global data structures.

Arguments:
    DriverObject - Pointer to driver object created by the system
        to represent this driver.
    RegistryPath - Unicode string identifying where the parameters
        for this driver are located in the registry.

Return Value:
    Returns STATUS_SUCCESS.
*/
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING portName;
    PSECURITY_DESCRIPTOR sd;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath );

    status = FltRegisterFilter(DriverObject,
                                &FilterRegistration,
                                &LoggerFilterData.FilterHandle);
	KdPrint(("[LoggerFilter] " __FUNCTION__ " FltRegisterFilter status: %x\n", status));
    FLT_ASSERT(NT_SUCCESS(status));

    RtlInitUnicodeString(&portName, LOGGERPortName);

    status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);

    if (NT_SUCCESS(status)) {

        InitializeObjectAttributes(&oa,
            &portName,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
            NULL,
            sd);

        status = FltCreateCommunicationPort(LoggerFilterData.FilterHandle,
            &LoggerFilterData.ServerPort,
            &oa,
            NULL,
            LoggerPortConnect,
            LoggerPortDisconnect,
            NULL,
            1);

		KdPrint(("[LoggerFilter] " __FUNCTION__ " FltCreateCommunicationPort status: %x\n", status));
        
        // The security descriptor is not needed once the call to FltCreateCommunicationPort() is made.
        FltFreeSecurityDescriptor(sd);

        if (NT_SUCCESS(status)) {

            status = FltStartFiltering(LoggerFilterData.FilterHandle); // Start filtering i/o
		    KdPrint(("[LoggerFilter] " __FUNCTION__ " FltStartFiltering status: %x\n", status));

            if (!NT_SUCCESS(status)) {
                FltUnregisterFilter(LoggerFilterData.FilterHandle);
            }
        }
	}
    return status;
}


NTSTATUS
LoggerInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
/*
Routine Description:
    This routine is called by the filter manager when a new instance is created.
    We specified in the registry that we only want for manual attachments,
    so that is all we should receive here.

Arguments:
    FltObjects - Describes the instance and volume which we are being asked 
        to setup.
    Flags - Flags describing the type of attachment this is.
    VolumeDeviceType - The DEVICE_TYPE for the volume to which this instance
        will attach.
    VolumeFileSystemType - The file system formatted on this volume.

Return Value:
    STATUS_SUCCESS            - we wish to attach to the volume
    STATUS_FLT_DO_NOT_ATTACH  - no, thank you

*/
{
    KdPrint(("[LoggerFilter] " __FUNCTION__ "  [%u] | \n", PtrToUint(PsGetCurrentProcessId())));
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);
    KdPrint(("VolumeDeviceType : %x | VolumeFilesystemType : %x \n", VolumeDeviceType, VolumeFilesystemType));

    PAGED_CODE();

    FLT_ASSERT(FltObjects->Filter == LoggerFilterData.FilterHandle);

    //  We don't attach to network volumes.
    if (VolumeDeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM) {
        return STATUS_FLT_DO_NOT_ATTACH;
    }

    return STATUS_SUCCESS;
}


NTSTATUS
LoggerUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
/*
Routine Description:
    This is the unload routine for this miniFilter driver. This is called
    when the minifilter is about to be unloaded. We can fail this unload
    request if this is not a mandatory unloaded indicated by the Flags
    parameter.

Arguments:
    Flags - Indicating if this is a mandatory unload.

Return Value:
    Returns the final status of this operation.
*/
{

    UNREFERENCED_PARAMETER(Flags);
    PAGED_CODE();
	FltCloseCommunicationPort(LoggerFilterData.ServerPort);
    FltUnregisterFilter(LoggerFilterData.FilterHandle);
    return STATUS_SUCCESS;
}


NTSTATUS
LoggerQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
/*
Routine Description:
    This is the instance detach routine for this miniFilter driver.
    This is called when an instance is being manually deleted by a
    call to FltDetachVolume or FilterDetach thereby giving us a
    chance to fail that detach request.

Arguments:
    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.
    Flags - Indicating where this detach request came from.

Return Value:
    Returns the status of this operation.
*/
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    PAGED_CODE();
    return STATUS_SUCCESS;
}


/*************************************************************************
	Communication Callbacks
*************************************************************************/

NTSTATUS
LoggerPortConnect(
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID* ConnectionCookie
)
/*
Routine Description:
    This routine is called when a connection is established between the driver and
    a user-mode application. The client port is used for sending messages between
    the filter and the user-mode application.

Arguments:
    ClientPort - The client communication port for the user-mode application.
    ServerPortCookie - The context associated with the server port (unused here).
    ConnectionContext - The connection context from the user-mode application (optional).
    SizeOfContext - The size of the connection context in bytes.
    ConnectionCookie - Context passed to the disconnection routine (unused here).

Return Value:
    STATUS_SUCCESS - Connection accepted.
*/
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionCookie = NULL);

    FLT_ASSERT(LoggerFilterData.ClientPort == NULL);
    FLT_ASSERT(LoggerFilterData.UserProcess == NULL);

    // Set the user process and port.
    LoggerFilterData.UserProcess = PsGetCurrentProcess();
    LoggerFilterData.ClientPort = ClientPort;

    DbgPrint("!!! LoggerFilter.sys --- connected, port=0x%p\n", ClientPort);

    return STATUS_SUCCESS;
}


VOID
LoggerPortDisconnect(
    _In_opt_ PVOID ConnectionCookie
)
/*
Routine Description
    This is called when the connection is torn-down.
    We use it to close our handle to the connection

Arguments
    ConnectionCookie - Context from the port connect routine

Return value
    None
*/
{
    UNREFERENCED_PARAMETER(ConnectionCookie);

    PAGED_CODE();

    DbgPrint("!!! LoggerFilter.sys --- disconnected, port=0x%p\n", LoggerFilterData.ClientPort);

    FltCloseClientPort(LoggerFilterData.FilterHandle, &LoggerFilterData.ClientPort);

    LoggerFilterData.UserProcess = NULL;
}

NTSTATUS SendMessageToUserMode(
    PVOID messageBuffer,
    ULONG messageSize
)
{
    NTSTATUS status = FltSendMessage(
        LoggerFilterData.FilterHandle,
        &LoggerFilterData.ClientPort,
        messageBuffer,
        messageSize,
        NULL,
        NULL,
        NULL
    );

    return status;
}


/*************************************************************************
	Utility routines
*************************************************************************/

void GetFormattedTime(CHAR* buffer, SIZE_T bufferSize)
/*
Routine Description:
    This function retrieves the current system time, converts it to local time,
    and formats it as "YYYY-MM-DD HH:MM:SS". The result is stored in the provided buffer.

Arguments:
    buffer - The buffer where the formatted time will be stored.
    bufferSize - The size of the buffer.

Notes:
    This function is used to timestamp file create or open events with local date
    and time information before they are logged.
*/
{
    LARGE_INTEGER systemTime;
    TIME_FIELDS timeFields;
    CHAR tempBuffer[50];

    KeQuerySystemTime(&systemTime);

    ExSystemTimeToLocalTime(&systemTime, &systemTime);

    RtlTimeToTimeFields(&systemTime, &timeFields);

    // Format the time as "YYYY-MM-DD HH:MM:SS"
    RtlStringCchPrintfA(tempBuffer, sizeof(tempBuffer),
        "%04d-%02d-%02d %02d:%02d:%02d",
        timeFields.Year,
        timeFields.Month,
        timeFields.Day,
        timeFields.Hour,
        timeFields.Minute,
        timeFields.Second);

    RtlStringCchCopyA(buffer, bufferSize, tempBuffer);
}
