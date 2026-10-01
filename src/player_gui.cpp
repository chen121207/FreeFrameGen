#define UNICODE
#define _UNICODE
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
constexpr int IDC_DISPLAY = 1001;
constexpr int IDC_WINDOW = 1002;
constexpr int IDC_WIDTH = 1003;
constexpr int IDC_DEADLINE = 1004;
constexpr int IDC_REFRESH = 1005;
constexpr int IDC_START = 1006;
constexpr int IDC_STOP = 1007;
constexpr int IDC_DOCS = 1008;
constexpr int IDC_STATUS = 1009;
constexpr int IDC_DETAILS = 1010;
constexpr int IDC_FULLSCREEN = 1011;
constexpr int IDC_HDR = 1012;

struct Source
{
    int index = -1;
    std::wstring label;
};

struct AppState
{
    HWND window = nullptr;
    HWND display = nullptr;
    HWND sourceWindow = nullptr;
    HWND width = nullptr;
    HWND deadline = nullptr;
    HWND status = nullptr;
    HWND details = nullptr;
    HWND fullscreen = nullptr;
    HWND hdr = nullptr;
    HANDLE child = nullptr;
    DWORD childPid = 0;
    std::vector<Source> displays;
    std::vector<Source> windows;
};

AppState *g_state = nullptr;

std::wstring moduleDirectory()
{
    wchar_t path[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return L".";
    std::wstring value(path, n);
    const size_t slash = value.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : value.substr(0, slash);
}

std::wstring quote(const std::wstring &value)
{
    std::wstring result = L"\"";
    for (wchar_t c : value)
    {
        if (c == L'\"')
            result += L'\\';
        result += c;
    }
    result += L'\"';
    return result;
}

bool readPipe(const std::wstring &command, std::wstring &output, DWORD &exitCode)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, 0))
        return false;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write;
    startup.hStdError = write;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, moduleDirectory().c_str(),
                                        &startup, &process);
    CloseHandle(write);
    if (!created)
    {
        CloseHandle(read);
        return false;
    }
    std::string bytes;
    char buffer[4096];
    for (;;)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr))
            break;
        if (available == 0)
        {
            if (WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0)
                break;
            continue;
        }
        DWORD got = 0;
        if (!ReadFile(read, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr) || !got)
            break;
        bytes.append(buffer, buffer + got);
    }
    WaitForSingleObject(process.hProcess, 5000);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read);

    // ffg_capture writes ASCII-compatible output. Convert it without relying
    // on the process console code page so the GUI remains Unicode.
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                     static_cast<int>(bytes.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (!needed)
    {
        codePage = GetACP();
        needed = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
                                     nullptr, 0);
    }
    if (needed)
    {
        output.resize(static_cast<size_t>(needed));
        MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), output.data(),
                            needed);
    }
    return true;
}

std::vector<std::wstring> lines(const std::wstring &text)
{
    std::vector<std::wstring> result;
    std::wstringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (!line.empty())
            result.push_back(line);
    }
    return result;
}

bool parseIndex(const std::wstring &line, int &index, std::wstring &rest)
{
    size_t colon = line.find(L':');
    if (colon == std::wstring::npos || colon == 0)
        return false;
    try
    {
        size_t consumed = 0;
        index = std::stoi(line.substr(0, colon), &consumed);
        if (consumed != colon)
            return false;
    }
    catch (...)
    {
        return false;
    }
    rest = line.substr(colon + 1);
    while (!rest.empty() && std::iswspace(rest.front()))
        rest.erase(rest.begin());
    return true;
}

bool refreshSources(AppState &state)
{
    std::wstring output;
    DWORD exitCode = 1;
    const std::wstring capture = moduleDirectory() + L"\\ffg_capture.exe";
    if (!readPipe(quote(capture) + L" --list", output, exitCode) || exitCode != 0)
        return false;
    const std::wregex displayPattern(LR"(^(.+?)\s+(\d+)x(\d+)$)");
    state.displays.clear();
    for (const std::wstring &line : lines(output))
    {
        int index = -1;
        std::wstring rest;
        std::wsmatch match;
        if (parseIndex(line, index, rest) && std::regex_match(rest, match, displayPattern))
        {
            state.displays.push_back({index, rest});
        }
    }

    output.clear();
    exitCode = 1;
    state.windows.clear();
    if (readPipe(quote(capture) + L" --list-windows", output, exitCode) && exitCode == 0)
    {
        for (const std::wstring &line : lines(output))
        {
            int index = -1;
            std::wstring rest;
            if (!parseIndex(line, index, rest) || rest.find(L'[') == std::wstring::npos)
                continue;
            state.windows.push_back({index, rest});
        }
    }
    return !state.displays.empty();
}

void setStatus(AppState &state, const std::wstring &message)
{
    SetWindowTextW(state.status, message.c_str());
}

void fillSources(AppState &state)
{
    SendMessageW(state.display, CB_RESETCONTENT, 0, 0);
    for (const Source &source : state.displays)
        SendMessageW(state.display, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(source.label.c_str()));
    SendMessageW(state.display, CB_SETCURSEL, state.displays.empty() ? -1 : 0, 0);

    SendMessageW(state.sourceWindow, CB_RESETCONTENT, 0, 0);
    SendMessageW(state.sourceWindow, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"整块显示器 / Full display"));
    for (const Source &source : state.windows)
        SendMessageW(state.sourceWindow, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(source.label.c_str()));
    SendMessageW(state.sourceWindow, CB_SETCURSEL, 0, 0);
}

bool readNumber(HWND control, int minimum, int maximum, int &value)
{
    wchar_t buffer[64]{};
    GetWindowTextW(control, buffer, 64);
    try
    {
        size_t consumed = 0;
        int parsed = std::stoi(buffer, &consumed);
        if (buffer[consumed] != L'\0' || parsed < minimum || parsed > maximum)
            return false;
        value = parsed;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void closeChild(AppState &state)
{
    if (!state.child)
        return;
    DWORD result = STILL_ACTIVE;
    GetExitCodeProcess(state.child, &result);
    if (result == STILL_ACTIVE)
    {
        struct Context
        {
            DWORD pid;
        } context{state.childPid};
        EnumWindows(
            [](HWND hwnd, LPARAM data) -> BOOL {
                Context *context = reinterpret_cast<Context *>(data);
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == context->pid)
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&context));
        if (WaitForSingleObject(state.child, 1000) != WAIT_OBJECT_0)
            TerminateProcess(state.child, 1);
    }
    CloseHandle(state.child);
    state.child = nullptr;
    state.childPid = 0;
}

bool startCapture(AppState &state)
{
    if (state.child)
        return false;
    int displayIndex = static_cast<int>(SendMessageW(state.display, CB_GETCURSEL, 0, 0));
    int sourceIndex = static_cast<int>(SendMessageW(state.sourceWindow, CB_GETCURSEL, 0, 0));
    int width = 0, deadline = 0;
    if (displayIndex < 0 || displayIndex >= static_cast<int>(state.displays.size()) ||
        !readNumber(state.width, 320, 1920, width) || !readNumber(state.deadline, 4, 100, deadline))
    {
        setStatus(state, L"请输入有效的宽度 320–1920 和 deadline 4–100 ms。 / Enter valid values.");
        return false;
    }
    std::wstring command = quote(moduleDirectory() + L"\\ffg_capture.exe") + L" --output " +
                           std::to_wstring(state.displays[static_cast<size_t>(displayIndex)].index) +
                           L" --width " + std::to_wstring(width) + L" --deadline-ms " +
                           std::to_wstring(deadline);
    if (sourceIndex > 0 && sourceIndex - 1 < static_cast<int>(state.windows.size()))
        command += L" --window " +
                   std::to_wstring(state.windows[static_cast<size_t>(sourceIndex - 1)].index);
    if (SendMessageW(state.fullscreen, BM_GETCHECK, 0, 0) == BST_CHECKED)
        command += L" --replace";
    if (SendMessageW(state.hdr, BM_GETCHECK, 0, 0) == BST_CHECKED)
        command += L" --hdr-output";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        moduleDirectory().c_str(), &startup, &process))
    {
        setStatus(state, L"无法启动捕获程序。请确认 ffg_capture.exe 存在。 / Capture failed to start.");
        return false;
    }
    CloseHandle(process.hThread);
    state.child = process.hProcess;
    state.childPid = process.dwProcessId;
    EnableWindow(state.display, FALSE);
    EnableWindow(state.sourceWindow, FALSE);
    EnableWindow(state.width, FALSE);
    EnableWindow(state.deadline, FALSE);
    EnableWindow(state.fullscreen, FALSE);
    EnableWindow(state.hdr, FALSE);
    EnableWindow(GetDlgItem(state.window, IDC_REFRESH), FALSE);
    EnableWindow(GetDlgItem(state.window, IDC_START), FALSE);
    EnableWindow(GetDlgItem(state.window, IDC_STOP), TRUE);
    setStatus(state, L"捕获预览运行中。关闭预览窗口或点击停止。 / Preview is running.");
    SetTimer(state.window, 1, 250, nullptr);
    return true;
}

void stopCapture(AppState &state)
{
    closeChild(state);
    KillTimer(state.window, 1);
    EnableWindow(state.display, TRUE);
    EnableWindow(state.sourceWindow, TRUE);
    EnableWindow(state.width, TRUE);
    EnableWindow(state.deadline, TRUE);
    EnableWindow(state.fullscreen, TRUE);
    EnableWindow(state.hdr, TRUE);
    EnableWindow(GetDlgItem(state.window, IDC_REFRESH), TRUE);
    EnableWindow(GetDlgItem(state.window, IDC_START), TRUE);
    EnableWindow(GetDlgItem(state.window, IDC_STOP), FALSE);
    setStatus(state, L"已停止。 / Stopped.");
}

HFONT makeFont(int height, bool bold = false)
{
    return CreateFontW(-height, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

HWND label(HWND parent, const wchar_t *text, int x, int y, int width, int height, HFONT font)
{
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, height,
                                   parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

HWND button(HWND parent, const wchar_t *text, int id, int x, int y, int width, int height,
            HFONT font)
{
    HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   x, y, width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

HWND checkBox(HWND parent, const wchar_t *text, int id, int x, int y, int width, int height,
              HFONT font, bool checked = false)
{
    HWND control = CreateWindowExW(0, L"BUTTON", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y,
                                   width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return control;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    AppState *state = reinterpret_cast<AppState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        state = reinterpret_cast<AppState *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_REFRESH:
            if (!refreshSources(*state))
                setStatus(*state, L"读取显示器失败。请确认 D3D12 捕获程序可运行。 / Refresh failed.");
            else
            {
                fillSources(*state);
                setStatus(*state, L"已刷新捕获源。 / Sources refreshed.");
            }
            return 0;
        case IDC_START:
            startCapture(*state);
            return 0;
        case IDC_STOP:
            stopCapture(*state);
            return 0;
        case IDC_DOCS:
            ShellExecuteW(hwnd, L"open", L"https://github.com/chen121207/FreeFrameGen",
                          nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case IDC_DETAILS:
            MessageBoxW(hwnd,
                        L"捕获模式只在本机 GPU 上处理画面，不录像、不上传、不注入游戏。\n"
                        L"全屏替换输出是 FFG 自己的 borderless topmost 窗口；它不会接管游戏 swapchain。\n"
                        L"HDR 输出使用 FP16 scRGB 和 Windows 色彩空间标签，源捕获仍是 SDR BGRA8。\n"
                        L"Native 模式请使用 include/ffg 与 FGDS 协议。\n\n"
                        L"Capture stays local on the GPU. It does not record, upload or inject.\n"
                        L"Replacement output is an FFG-owned borderless topmost window, not game injection.\n"
                        L"HDR uses an FP16 scRGB swapchain; Desktop Duplication input remains SDR BGRA8.\n"
                        L"Use the FGDS headers for native engine integration.",
                        L"FreeFrameGen", MB_OK | MB_ICONINFORMATION);
            return 0;
        default:
            break;
        }
        break;
    case WM_TIMER:
        if (wParam == 1 && state->child)
        {
            DWORD code = STILL_ACTIVE;
            if (GetExitCodeProcess(state->child, &code) && code != STILL_ACTIVE)
                stopCapture(*state);
        }
        return 0;
    case WM_CLOSE:
        if (state->child)
            stopCapture(*state);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        closeChild(*state);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show)
{
    if (commandLine && std::wstring(commandLine).find(L"--help") != std::wstring::npos)
        return 0;
    INITCOMMONCONTROLSEX common{};
    common.dwSize = sizeof(common);
    common.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common);
    AppState state;
    g_state = &state;
    const wchar_t *className = L"FreeFrameGen.PlayerGui";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    RegisterClassW(&windowClass);

    HFONT normal = makeFont(15);
    HFONT heading = makeFont(25, true);
    HWND window = CreateWindowExW(0, className, L"FreeFrameGen — 玩家控制台 / Player Control",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 760, 520, nullptr, nullptr, instance,
                                 &state);
    if (!window)
        return 1;
    label(window, L"FreeFrameGen", 36, 26, 690, 38, heading);
    label(window, L"低延迟捕获插帧预览  /  Low-latency frame generation preview", 39, 69, 680, 24,
          normal);
    label(window, L"选择游戏所在显示器或窗口，然后启动预览。程序不会修改游戏文件。",
          39, 103, 680, 24, normal);
    label(window, L"Capture source / 捕获源", 39, 147, 250, 25, makeFont(16, true));
    label(window, L"显示器 / Display", 39, 184, 130, 25, normal);
    state.display = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST, 185, 180, 500, 250, window,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DISPLAY)), instance,
                                    nullptr);
    label(window, L"窗口 / Window", 39, 227, 130, 25, normal);
    state.sourceWindow = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                         CBS_DROPDOWNLIST, 185, 223, 500, 250, window,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_WINDOW)), instance,
                                         nullptr);
    label(window, L"处理宽度 / Width", 39, 270, 145, 25, normal);
    state.width = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"960",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 185, 266, 110, 28,
                                  window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_WIDTH)), instance,
                                  nullptr);
    label(window, L"deadline (ms)", 330, 270, 145, 25, normal);
    state.deadline = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"24",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 455, 266, 110, 28,
                                     window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DEADLINE)),
                                     instance, nullptr);
    button(window, L"刷新 / Refresh", IDC_REFRESH, 575, 264, 110, 32, normal);
    state.fullscreen = checkBox(window, L"全屏替换输出 / Borderless replacement", IDC_FULLSCREEN,
                                39, 310, 285, 28, normal, false);
    state.hdr = checkBox(window, L"HDR 输出 / scRGB FP16 output", IDC_HDR, 340, 310, 300, 28,
                         normal, false);
    label(window, L"替换模式覆盖所选显示器，不注入游戏；HDR 需要显示器支持 scRGB。",
          39, 335, 650, 22, normal);
    state.status = label(window, L"正在读取显示器… / Loading sources…", 39, 348, 650, 28, normal);
    state.details = label(window, L"", 39, 380, 650, 22, normal);
    button(window, L"开始 / Start", IDC_START, 405, 419, 105, 36, normal);
    button(window, L"停止 / Stop", IDC_STOP, 520, 419, 105, 36, normal);
    button(window, L"说明 / Help", IDC_DETAILS, 635, 419, 70, 36, normal);
    SendMessageW(state.display, WM_SETFONT, reinterpret_cast<WPARAM>(normal), TRUE);
    SendMessageW(state.sourceWindow, WM_SETFONT, reinterpret_cast<WPARAM>(normal), TRUE);
    SendMessageW(state.width, WM_SETFONT, reinterpret_cast<WPARAM>(normal), TRUE);
    SendMessageW(state.deadline, WM_SETFONT, reinterpret_cast<WPARAM>(normal), TRUE);
    EnableWindow(GetDlgItem(window, IDC_STOP), FALSE);
    if (refreshSources(state))
    {
        fillSources(state);
        setStatus(state, L"就绪。 / Ready.");
    }
    else
        setStatus(state, L"没有找到可捕获的显示器。 / No display found.");
    ShowWindow(window, show == 0 ? SW_SHOW : show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DeleteObject(normal);
    DeleteObject(heading);
    return static_cast<int>(message.wParam);
}
