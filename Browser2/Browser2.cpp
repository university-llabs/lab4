// Browser.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX        
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <limits>
#include <cstdlib>
#include <ctime>
#include <algorithm>   

static const char* kSemaphoreName = "DownloadSlots";
static const char* kMutexName = "LogAccessMutex";
static const char* kEventName = "BrowserClosingEvent";

struct SyncObjects {
    HANDLE hSemaphore = nullptr;
    HANDLE hMutex = nullptr;
    HANDLE hEvent = nullptr;
};

bool CreateNamedSyncObjects(int N, SyncObjects& out) {
    out.hSemaphore = CreateSemaphoreA(nullptr, N, N, kSemaphoreName);
    if (!out.hSemaphore) {
        std::cerr << "Browser: failed to create semaphore. Error=" << GetLastError() << "\n";
        return false;
    }

    out.hMutex = CreateMutexA(nullptr, FALSE, kMutexName);
    if (!out.hMutex) {
        std::cerr << "Browser: failed to create mutex. Error=" << GetLastError() << "\n";
        return false;
    }

    out.hEvent = CreateEventA(nullptr, TRUE, FALSE, kEventName);
    if (!out.hEvent) {
        std::cerr << "Browser: failed to create event. Error=" << GetLastError() << "\n";
        return false;
    }

    std::cout << "Browser: sync objects created successfully.\n";
    return true;
}

void CloseSyncObjects(SyncObjects& objs) {
    if (objs.hSemaphore) CloseHandle(objs.hSemaphore);
    if (objs.hMutex)     CloseHandle(objs.hMutex);
    if (objs.hEvent)     CloseHandle(objs.hEvent);
    objs = {};
}

std::string GenerateRandomFileName(int index) {
    const char* extensions[] = { ".jpg", ".pdf", ".zip", ".exe", ".mp4", ".txt", ".docx", ".png" };
    const char* prefixes[] = { "photo", "document", "archive", "setup", "video", "text", "file" };

    std::ostringstream oss;
    oss << prefixes[index % 7] << "_" << index << extensions[index % 8];
    return oss.str();
}

bool LaunchDownloaderProcesses(int M, std::vector<HANDLE>& outProcessHandles) {
    outProcessHandles.clear();
    outProcessHandles.reserve(M);

    for (int i = 0; i < M; ++i) {
        std::string fileName = GenerateRandomFileName(i);

        std::ostringstream oss;
        oss << "Downloader2.exe " << i << " \"" << fileName << "\"";
        std::string cmd = oss.str();

        std::vector<char> cmdline(cmd.begin(), cmd.end());
        cmdline.push_back('\0');

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        BOOL ok = CreateProcessA(
            nullptr,          // Имя приложения
            cmdline.data(),   // Командная строка
            nullptr,          // Атрибуты безопасности процесса
            nullptr,          // Атрибуты безопасности потока
            FALSE,            // Наследование дескрипторов
            0,                // Флаги создания
            nullptr,          // Окружение
            nullptr,          // Текущий каталог
            &si,
            &pi
        );

        if (!ok) {
            DWORD err = GetLastError();
            std::cerr << "Browser: failed to launch downloader " << i
                << ". Error=" << err << "\n";
            continue;
        }

        CloseHandle(pi.hThread);
        outProcessHandles.push_back(pi.hProcess);

        Sleep(10);
    }

    std::cout << "Browser: launched " << outProcessHandles.size() << " downloader processes.\n";
    return !outProcessHandles.empty();
}

DWORD WaitAllProcesses(const std::vector<HANDLE>& processes) {
    const DWORD kChunk = MAXIMUM_WAIT_OBJECTS;
    size_t total = processes.size();
    size_t offset = 0;

    while (offset < total) {
        size_t count = std::min(total - offset, static_cast<size_t>(kChunk));
        std::vector<HANDLE> slice(processes.begin() + offset,
            processes.begin() + offset + count);

        DWORD waitRes = WaitForMultipleObjects(
            static_cast<DWORD>(slice.size()),
            slice.data(),
            TRUE,
            INFINITE
        );

        if (waitRes == WAIT_FAILED) {
            DWORD err = GetLastError();
            std::cerr << "Browser: WaitForMultipleObjects failed. Error=" << err << "\n";
            return err;
        }
        offset += count;
    }
    return ERROR_SUCCESS;
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    int N = 0, M = 0;
    std::cout << "Enter N (max concurrent downloads): ";
    if (!(std::cin >> N) || N <= 0) {
        std::cerr << "Invalid N. Must be positive.\n";
        return 1;
    }

    std::cout << "Enter M (total queued files, must be > N): ";
    if (!(std::cin >> M) || M <= N) {
        std::cerr << "Invalid M. Must be greater than N.\n";
        return 1;
    }

    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    SyncObjects sync{};
    if (!CreateNamedSyncObjects(N, sync)) {
        CloseSyncObjects(sync);
        return 1;
    }

    std::vector<HANDLE> processHandles;
    if (!LaunchDownloaderProcesses(M, processHandles)) {
        std::cerr << "Browser: no downloader processes started.\n";
        CloseSyncObjects(sync);
        return 1;
    }

    std::cout << "\nBrowser is running. Press Enter to close...\n";
    std::cin.get();

    WaitForSingleObject(sync.hMutex, INFINITE);
    std::cout << "\nBrowser is closing. Sending termination signal to all downloads...\n";
    ReleaseMutex(sync.hMutex);

    if (!SetEvent(sync.hEvent)) {
        std::cerr << "Browser: failed to set event. Error=" << GetLastError() << "\n";
    }

    WaitAllProcesses(processHandles);

    for (HANDLE h : processHandles) {
        if (h) CloseHandle(h);
    }

    CloseSyncObjects(sync);

    std::cout << "\nBrowser shutdown complete.\n";
    return 0;
}