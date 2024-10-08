#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <windows.h>
#include <fltUser.h>
#include <fstream>
#include "userlogger.h"

constexpr DWORD LOGGER_DEFAULT_REQUEST_COUNT = 5;
constexpr DWORD LOGGER_DEFAULT_THREAD_COUNT = 2;
constexpr DWORD LOGGER_MAX_THREAD_COUNT = 64;

void Usage() {
    std::wcerr << L"Usage: <executable> [RequestCount] [ThreadCount]" << std::endl;
}

void LogProcessInfo(ULONG64 processId, const char* time) {
    std::ofstream logFile("process_log.txt", std::ios_base::app);
    if (logFile.is_open()) {
        logFile << " Process ID: " << processId
			<< ", open at : " << time << std::endl;
        logFile.close();
    }
    else {
        std::cerr << "Unable to open log file." << std::endl;
    }
}

DWORD WINAPI LoggerWorker(LPVOID context) {
/*++
Routine Description
	This is a worker thread that processes messages from the filter

Arguments
    Context  - This thread context has a pointer to the port handle we use to send/receive messages,
                and a completion port handle that was already associated with the comm. port by the caller

Return Value
    HRESULT indicating the status of thread exit.
--*/
    auto ctx = reinterpret_cast<LOGGER_THREAD_CONTEXT*>(context);
    PLOGGER_NOTIFICATION notification;
    PLOGGER_MESSAGE message;
    LPOVERLAPPED overlapped;
    BOOL result;
    DWORD bytesTransferred;
    HRESULT hr;
    ULONG_PTR completionKey;

    while (true) {
        result = GetQueuedCompletionStatus(
            ctx->Completion,
            &bytesTransferred,
            &completionKey,
            &overlapped,
            INFINITE
        );
        message = CONTAINING_RECORD(overlapped, LOGGER_MESSAGE, Ovlp);

		if (!result) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }
		notification = &message->Notification;
        printf("Received message, size %Id, ProcessId %I64u\n", overlapped->InternalHigh, message->Notification.ProcessId);
		printf("time: %s\n", message->Notification.Time);

		// Log process ID and time to a file
		LogProcessInfo(message->Notification.ProcessId, message->Notification.Time);

        memset(&message->Ovlp, 0, sizeof(OVERLAPPED));
        hr = FilterGetMessage(ctx->Port,
            &message->MessageHeader,
            FIELD_OFFSET(LOGGER_MESSAGE, Ovlp),
            &message->Ovlp);
        
        if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {

            break;
        }
    }
    if (!SUCCEEDED(hr)) {
        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)) {
            printf("Logger: Port is disconnected, probably due to LoggerFilter unloading.\n");
        }
        else {
            printf("Logger: Unknown error occured. Error = 0x%X\n", hr);
        }
    }

    return hr;
}


int main(int argc, char* argv[]) { 
    /*
	Main entry point for the userlogger application.
	argc - Number of command line arguments
	argv - Array of command line arguments
    */
    DWORD requestCount = LOGGER_DEFAULT_REQUEST_COUNT;
    DWORD threadCount = LOGGER_DEFAULT_THREAD_COUNT;
    std::vector<HANDLE> threads;  
    LOGGER_THREAD_CONTEXT context;
    HANDLE port = nullptr;
    HANDLE completion = nullptr;
    std::vector<LOGGER_MESSAGE> messages;
    HRESULT hr;

    // Check how many threads and per thread requests are desired.
    if (argc > 1) {
        requestCount = std::atoi(argv[1]);

        if (requestCount <= 0) {
            Usage();
            return 1;
        }

        if (argc > 2) {
            threadCount = std::atoi(argv[2]);
        }

        if (threadCount <= 0 || threadCount > LOGGER_MAX_THREAD_COUNT) {
            Usage();
            return 1;
        }
    }

    // Open a commuication channel to the filter
    std::wcout << L"LOGGER: Connecting to the filter..." << std::endl;

    hr = FilterConnectCommunicationPort(LOGGERPortName, 0, nullptr, 0, nullptr, &port);

    if (FAILED(hr)) {
        std::wcerr << L"ERROR: Connecting to filter port: 0x" << std::hex << hr << std::endl;
        return 2;
    }

    // Create a completion port to use GetQueuedCompletionStatus to get messages from the driver.
    completion = CreateIoCompletionPort(port, nullptr, 0, threadCount);

    if (!completion) {
        std::wcerr << L"ERROR: Creating completion port: " << GetLastError() << std::endl;
        CloseHandle(port);
		return 3; 
    }

    std::wcout << L"NULL: Port = " << port << L" Completion = " << completion << std::endl;

    context.Port = port;
    context.Completion = completion;

    // Allocate messages.
    try {
        messages.resize(static_cast<size_t>(threadCount) * requestCount);
    }
    catch (const std::bad_alloc&) {
        hr = E_OUTOFMEMORY;
        goto main_cleanup;
    }

    // Create specified number of threads.
    try {
        for (DWORD i = 0; i < threadCount; ++i) {
            threads.emplace_back(CreateThread(
                nullptr,
                0,
                LoggerWorker,
                &context,
                0,
                nullptr
            ));

            if (!threads.back()) {
                hr = GetLastError();
                std::wcerr << L"ERROR: Couldn't create thread: " << hr << std::endl;
                goto main_cleanup;
            }

			// Get messages for each thread.
            for (DWORD j = 0; j < requestCount; ++j) {
                LOGGER_MESSAGE msg = messages[i * requestCount + j];
                std::memset(&msg.Ovlp, 0, sizeof(OVERLAPPED));

                hr = FilterGetMessage(
                    port,
					&msg.MessageHeader,
                    FIELD_OFFSET(LOGGER_MESSAGE, Ovlp),
                    &msg.Ovlp
                );
                if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {
                    goto main_cleanup;
                }
            } 
        }
        hr = S_OK;
    }
    catch (...) {
        hr = E_FAIL;
    }

main_cleanup:

    for (auto& thread : threads) {
        if (thread) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
        }
    }

    std::wcout << L"NULL:  All done. Result = 0x" << std::hex << hr << std::endl;

    if (port) CloseHandle(port);
    if (completion) CloseHandle(completion);

    return hr;
}
