#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

static void showError(const wchar_t* message) {
    MessageBoxW(NULL, message, L"Brightness Sync", MB_OK | MB_ICONERROR);
}

static WORD hostMachine(void) {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    typedef BOOL (WINAPI *IsWow64Process2Fn)(HANDLE, USHORT*, USHORT*);
    IsWow64Process2Fn query = (IsWow64Process2Fn)GetProcAddress(kernel, "IsWow64Process2");
    if (query) {
        USHORT process = 0, native = 0;
        if (query(GetCurrentProcess(), &process, &native)) return native;
    }
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) return 0x8664;
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) return 0xaa64;
    return 0;
}

static BOOL stagePayload(const BYTE* payload, DWORD length, wchar_t stage[MAX_PATH]) {
    wchar_t profile[MAX_PATH] = {0};
    wchar_t temporary[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!n || n >= MAX_PATH - 2) return FALSE;
    if (profile[n - 1] != L'\\') { profile[n++] = L'\\'; profile[n] = 0; }
    for (int attempt = 0; attempt < 8; attempt++) {
        if (!GetTempFileNameW(profile, L"BSU", 0, temporary)) return FALSE;
        wcscpy(stage, temporary);
        size_t size = wcslen(stage);
        if (size < 4) { DeleteFileW(temporary); return FALSE; }
        wcscpy(stage + size - 4, L".exe");
        if (MoveFileExW(temporary, stage, 0)) break;
        DeleteFileW(temporary);
        if (attempt == 7) return FALSE;
    }
    HANDLE target = CreateFileW(stage, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_HIDDEN, NULL);
    if (target == INVALID_HANDLE_VALUE) { DeleteFileW(stage); return FALSE; }
    DWORD written = 0;
    BOOL okay = WriteFile(target, payload, length, &written, NULL) && written == length;
    CloseHandle(target);
    if (!okay) DeleteFileW(stage);
    return okay;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR unused, int show) {
    (void)instance; (void)previous; (void)unused; (void)show;
    WORD machine = hostMachine();
    if (machine != 0x8664 && machine != 0xaa64) {
        showError(L"此文件支持 x64 和 ARM64 Windows；无法识别当前系统架构。");
        return 2;
    }
    WORD resourceId = machine == 0xaa64 ? 102 : 101;
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(NULL, resource) : NULL;
    DWORD length = resource ? SizeofResource(NULL, resource) : 0;
    const BYTE* payload = loaded ? (const BYTE*)LockResource(loaded) : NULL;
    if (!payload || !length) { showError(L"启动器中的原生程序数据不完整。"); return 2; }
    wchar_t stage[MAX_PATH] = {0};
    BOOL extracted = stagePayload(payload, length, stage);
    if (!extracted) { showError(L"无法解压对应架构的原生程序。"); return 2; }
    const wchar_t* tail = GetCommandLineW();
    if (*tail == L'"') { tail++; while (*tail && *tail != L'"') tail++; if (*tail) tail++; }
    else { while (*tail && *tail != L' ' && *tail != L'\t') tail++; }
    while (*tail == L' ' || *tail == L'\t') tail++;
    size_t commandLength = wcslen(stage) + wcslen(tail) + 5;
    wchar_t* command = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, commandLength * sizeof(wchar_t));
    if (!command) { DeleteFileW(stage); showError(L"内存不足。"); return 2; }
    wcscpy(command, L"\"");
    wcscat(command, stage);
    wcscat(command, L"\"");
    if (*tail) { wcscat(command, L" "); wcscat(command, tail); }
    STARTUPINFOW startup = {0}; startup.cb = sizeof(startup);
    BOOL inheritHandles = FALSE;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    BOOL closeInput = FALSE;
    if (output && output != INVALID_HANDLE_VALUE &&
        SetHandleInformation(output, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
        if (!input || input == INVALID_HANDLE_VALUE ||
            !SetHandleInformation(input, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            SECURITY_ATTRIBUTES security = { sizeof(security), NULL, TRUE };
            input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            closeInput = input != INVALID_HANDLE_VALUE;
        }
        if (!error || error == INVALID_HANDLE_VALUE ||
            !SetHandleInformation(error, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) error = output;
    }
    if (input && input != INVALID_HANDLE_VALUE && output && output != INVALID_HANDLE_VALUE) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = output;
        startup.hStdError = error;
        inheritHandles = TRUE;
    }
    PROCESS_INFORMATION process = {0};
    BOOL started = CreateProcessW(stage, command, NULL, NULL, inheritHandles, 0, NULL, NULL, &startup, &process);
    if (closeInput) CloseHandle(input);
    HeapFree(GetProcessHeap(), 0, command);
    if (!started) { DeleteFileW(stage); showError(L"无法启动对应架构的原生程序。"); return 2; }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 2;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    DeleteFileW(stage);
    return (int)exitCode;
}
