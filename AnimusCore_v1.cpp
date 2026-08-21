#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "pdh.lib")

// Control IDs
#define ID_BTN_START 1001
#define ID_BTN_STOP  1002
#define ID_EDIT_LOG  1003

// Global Handles & Flags
HWND hLogBox = NULL;
HWND hStatusLabel = NULL;
std::atomic<bool> g_IsMonitoring(false);
std::thread g_WorkerThread;

// Helper: Get Current Precise Timestamp [HH:MM:SS.mmm]
std::wstring GetCurrentTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buffer[32];
    swprintf_s(buffer, L"[%02d:%02d:%02d.%03d]", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::wstring(buffer);
}

// Helper: Append Text and Auto-Scroll to Bottom
void AppendLog(const std::wstring& text) {
    if (!hLogBox) return;

    std::wstring formattedText = GetCurrentTimestamp() + L" " + text + L"\r\n";

    int len = GetWindowTextLengthW(hLogBox);
    SendMessageW(hLogBox, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hLogBox, EM_REPLACESEL, FALSE, (LPARAM)formattedText.c_str());

    // Auto-scroll to the newest line
    SendMessageW(hLogBox, EM_SCROLLCARET, 0, 0);
}

// Background Worker: Telemetry Monitor Loop
void MonitoringWorker() {
    PDH_HQUERY cpuQuery;
    PDH_HCOUNTER cpuTotal;
    PdhOpenQuery(NULL, NULL, &cpuQuery);
    PdhAddEnglishCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal);
    PdhCollectQueryData(cpuQuery);

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);

    while (g_IsMonitoring) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // 1. Fetch CPU Usage
        PDH_FMT_COUNTERVALUE counterVal;
        PdhCollectQueryData(cpuQuery);
        PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
        double cpuPercent = counterVal.doubleValue;

        // 2. Fetch Memory Telemetry
        GlobalMemoryStatusEx(&memInfo);
        DWORDLONG physRamFreeMB = memInfo.ullAvailPhys / (1024 * 1024);
        DWORDLONG physRamTotalMB = memInfo.ullTotalPhys / (1024 * 1024);

        // 3. Construct and Append Telemetry Output
        std::wstringstream ss;
        ss << L"[TELEMETRY] CPU: " << std::fixed << std::setprecision(1) << cpuPercent
            << L"% | Avail RAM: " << physRamFreeMB << L"MB / " << physRamTotalMB << L"MB";

        AppendLog(ss.str());
    }

    PdhCloseQuery(cpuQuery);
}

// Window Procedure Callback
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        hStatusLabel = CreateWindowW(L"STATIC", L"Engine Idle. Click START to begin monitoring.",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, 20, 520, 20, hwnd, NULL, NULL, NULL);

        CreateWindowW(L"BUTTON", L"START ENGINE",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20, 50, 250, 35, hwnd, (HMENU)ID_BTN_START, NULL, NULL);

        CreateWindowW(L"BUTTON", L"STOP ENGINE",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            290, 50, 250, 35, hwnd, (HMENU)ID_BTN_STOP, NULL, NULL);

        hLogBox = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            20, 100, 520, 230, hwnd, (HMENU)ID_EDIT_LOG, NULL, NULL);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_START) {
            if (!g_IsMonitoring) {
                g_IsMonitoring = true;
                SetWindowTextW(hStatusLabel, L"Engine Active. Monitoring live telemetry...");
                AppendLog(L"[ANIMUS v1.0] Core engine telemetry stream initialized.");

                if (g_WorkerThread.joinable()) g_WorkerThread.join();
                g_WorkerThread = std::thread(MonitoringWorker);
            }
        }
        else if (LOWORD(wParam) == ID_BTN_STOP) {
            if (g_IsMonitoring) {
                g_IsMonitoring = false;
                if (g_WorkerThread.joinable()) g_WorkerThread.join();
                SetWindowTextW(hStatusLabel, L"Engine Idle. Click START to begin monitoring.");
                AppendLog(L"[ANIMUS v1.0] Core engine telemetry halted.");
            }
        }
        break;

    case WM_DESTROY:
        g_IsMonitoring = false;
        if (g_WorkerThread.joinable()) g_WorkerThread.join();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Win32 Entry Point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"AnimusDashboardClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Animus Engine Core - Security Dashboard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 580, 400,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
