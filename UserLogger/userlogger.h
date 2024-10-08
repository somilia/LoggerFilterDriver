#ifndef __USERLOGGER_H__
#define __USERLOGGER_H__
#pragma pack(1)


const PWSTR LOGGERPortName = L"\\LOGGERPort";

#pragma pack(push, 8)
 
typedef struct _LOGGER_NOTIFICATION {
    UINT64 ProcessId;
    CHAR Time[20];
    CHAR MessageData[256];

} LOGGER_NOTIFICATION, * PLOGGER_NOTIFICATION;

typedef struct _LOGGER_MESSAGE {

    FILTER_MESSAGE_HEADER MessageHeader;
    LOGGER_NOTIFICATION Notification;
    OVERLAPPED Ovlp;

} LOGGER_MESSAGE, * PLOGGER_MESSAGE;

struct LOGGER_THREAD_CONTEXT {

    HANDLE Port;
    HANDLE Completion;
};

#endif
