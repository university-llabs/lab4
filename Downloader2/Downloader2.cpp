// Downloader.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <stack>

static const char* kSemaphoreName = "DownloadSlots";
static const char* kMutexName = "LogAccessMutex";
static const char* kEventName = "BrowserClosingEvent";

struct SyncObjects {
    HANDLE hSemaphore = nullptr;
    HANDLE hMutex = nullptr;
    HANDLE hEvent = nullptr;
};

bool OpenNamedSyncObjects(SyncObjects& out) {
    out.hSemaphore = OpenSemaphoreA(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE, kSemaphoreName);
    if (!out.hSemaphore) {
        std::cerr << "Downloader: failed to open semaphore. Error: " << GetLastError() << "\n";
        return false;
    }

    out.hMutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, kMutexName);
    if (!out.hMutex) {
        std::cerr << "Downloader: failed to open mutex. Error: " << GetLastError() << "\n";
        return false;
    }

    out.hEvent = OpenEventA(SYNCHRONIZE, FALSE, kEventName);
    if (!out.hEvent) {
        std::cerr << "Downloader: failed to open event. Error: " << GetLastError() << "\n";
        return false;
    }

    return true;
}

void CloseSyncObjects(SyncObjects& objs) {
    if (objs.hSemaphore) CloseHandle(objs.hSemaphore);
    if (objs.hMutex)     CloseHandle(objs.hMutex);
    if (objs.hEvent)     CloseHandle(objs.hEvent);
    objs = {};
}

bool CheckBracketsBalanced(const std::string& text) {
    std::stack<char> brackets;

    for (char c : text) {
        switch (c) {
        case '(':
        case '[':
        case '{':
            brackets.push(c);
            break;

        case ')':
            if (brackets.empty() || brackets.top() != '(')
                return false;
            brackets.pop();
            break;

        case ']':
            if (brackets.empty() || brackets.top() != '[')
                return false;
            brackets.pop();
            break;

        case '}':
            if (brackets.empty() || brackets.top() != '{')
                return false;
            brackets.pop();
            break;
        }
    }

    return brackets.empty();
}

std::string GenerateRandomText() {
    const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789()[]{} ";
    const int len = 50 + (std::rand() % 100);

    std::string result;
    result.reserve(len);

    for (int i = 0; i < len; ++i) {
        result.push_back(chars[std::rand() % (sizeof(chars) - 1)]);
    }

    return result;
}

int main(int argc, char* argv[]) {
    int index = 0;
    std::string fileName;

    if (argc >= 2) {
        index = std::atoi(argv[1]);
    }

    if (argc >= 3) {
        fileName = argv[2];
    }
    else {
        fileName = "file_" + std::to_string(index) + ".dat";
    }

    std::srand(static_cast<unsigned>(std::time(nullptr)) ^ GetCurrentProcessId());

    DWORD pid = GetCurrentProcessId();

    SyncObjects sync{};
    if (!OpenNamedSyncObjects(sync)) {
        std::cerr << "[PID: " << pid << "] Failed to open sync objects.\n";
        return 1;
    }

    HANDLE waitHandles[2] = { sync.hEvent, sync.hSemaphore };
    DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

    if (waitResult == WAIT_FAILED) {
        DWORD err = GetLastError();
        std::cerr << "[PID: " << pid << "] Wait failed. Error: " << err << "\n";
        CloseSyncObjects(sync);
        return 1;
    }

    if (waitResult == WAIT_OBJECT_0) {
        WaitForSingleObject(sync.hMutex, INFINITE);
        std::cout << "[PID: " << pid << "] Download interrupted: browser is closing.\n";
        ReleaseMutex(sync.hMutex);
        CloseSyncObjects(sync);
        return 0;
    }

    WaitForSingleObject(sync.hMutex, INFINITE);
    std::cout << "[PID: " << pid << "] Connection established. Starting download of '"
        << fileName << "'...\n";
    ReleaseMutex(sync.hMutex);

    int sleepTime = 1000 + (std::rand() % 2000);
    Sleep(sleepTime);

    std::string fileContent = GenerateRandomText();
    bool bracketsBalanced = CheckBracketsBalanced(fileContent);

    WaitForSingleObject(sync.hMutex, INFINITE);
    if (bracketsBalanced) {
        std::cout << "[PID: " << pid << "] File '" << fileName
            << "' processed successfully. Brackets are balanced.\n";
    }
    else {
        std::cout << "[PID: " << pid << "] File '" << fileName
            << "' processed with errors. Brackets NOT balanced.\n";
    }
    ReleaseMutex(sync.hMutex);

    if (!ReleaseSemaphore(sync.hSemaphore, 1, nullptr)) {
        DWORD err = GetLastError();
        WaitForSingleObject(sync.hMutex, INFINITE);
        std::cout << "[PID: " << pid << "] Warning: failed to release semaphore. Error: "
            << err << "\n";
        ReleaseMutex(sync.hMutex);
    }

    CloseSyncObjects(sync);

    return bracketsBalanced ? 0 : 1;
}