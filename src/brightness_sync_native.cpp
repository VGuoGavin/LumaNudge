#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <windows.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <wbemidl.h>
#include <taskschd.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <sddl.h>
#include <secext.h>
#include <shellapi.h>
#include <dbt.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

template <class T> struct Ptr {
    T* p = nullptr;
    ~Ptr() { if (p) p->Release(); }
    T** out() { if (p) { p->Release(); p = nullptr; } return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    Ptr() = default;
    Ptr(Ptr&& other) noexcept : p(other.p) { other.p = nullptr; }
    Ptr& operator=(Ptr&& other) noexcept {
        if (this != &other) {
            if (p) p->Release();
            p = other.p;
            other.p = nullptr;
        }
        return *this;
    }
};
struct Bstr {
    BSTR p;
    explicit Bstr(const wchar_t* s) : p(SysAllocString(s)) {}
    explicit Bstr(const std::wstring& s) : p(SysAllocString(s.c_str())) {}
    ~Bstr() { SysFreeString(p); }
    operator BSTR() const { return p; }
};
static VARIANT emptyVariant() { VARIANT v; VariantInit(&v); return v; }

static HANDLE outputHandle = nullptr;
static HANDLE inputHandle = nullptr;
static bool displayConfigurationChanged = false;
static LRESULT CALLBACK displayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DISPLAYCHANGE ||
        (message == WM_DEVICECHANGE &&
         (wParam == DBT_DEVNODES_CHANGED || wParam == DBT_CONFIGCHANGED ||
          wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)))
        displayConfigurationChanged = true;
    return DefWindowProcW(window, message, wParam, lParam);
}
static HWND createDisplayWindow() {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = displayWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"BrightnessSyncDisplayEvents";
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    return CreateWindowExW(0, windowClass.lpszClassName, L"", WS_POPUP,
                           0, 0, 0, 0, nullptr, nullptr, windowClass.hInstance, nullptr);
}
static void pumpDisplayMessages() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
static void waitForDisplayMessages(HWND window, DWORD timeout) {
    if (window) {
        MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        pumpDisplayMessages();
    } else {
        Sleep(timeout);
    }
}
static void openConsole() {
    outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    if (!outputHandle || outputHandle == INVALID_HANDLE_VALUE || !inputHandle || inputHandle == INVALID_HANDLE_VALUE) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}
static void print(const std::wstring& s) {
    if (!outputHandle || outputHandle == INVALID_HANDLE_VALUE) return;
    DWORD mode, written;
    if (GetConsoleMode(outputHandle, &mode)) {
        WriteConsoleW(outputHandle, s.c_str(), (DWORD)s.size(), &written, nullptr);
    } else {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string bytes(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &bytes[0], n, nullptr, nullptr);
            WriteFile(outputHandle, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
        }
    }
}
static void line(const std::wstring& s) { print(s + L"\r\n"); }
static std::wstring readLine() {
    wchar_t buf[128] = {};
    DWORD got = 0;
    DWORD mode = 0;
    if (inputHandle && GetConsoleMode(inputHandle, &mode)) {
        if (!ReadConsoleW(inputHandle, buf, 127, &got, nullptr)) return L"";
        std::wstring s(buf, got);
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n')) s.pop_back();
        return s;
    }
    std::wstring result;
    char byte = 0;
    while (inputHandle && ReadFile(inputHandle, &byte, 1, &got, nullptr) && got == 1) {
        if (byte == '\n') break;
        if (byte != '\r' && result.size() < 127) result.push_back((unsigned char)byte);
    }
    return result;
}
static std::wstring errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&buffer, 0, nullptr);
    std::wstring result = buffer ? buffer : L"Unknown error";
    if (buffer) LocalFree(buffer);
    return result;
}
static std::wstring envPath(const wchar_t* name) {
    DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
    if (!need) return L"";
    std::wstring result(need, L'\0');
    DWORD len = GetEnvironmentVariableW(name, &result[0], need);
    result.resize(len);
    return result;
}
static std::wstring selfPath() {
    std::wstring result(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &result[0], (DWORD)result.size());
    result.resize(n);
    return result;
}
static std::wstring userSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return L"";
    DWORD need = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &need);
    std::vector<BYTE> data(need);
    std::wstring sid;
    if (GetTokenInformation(token, TokenUser, data.data(), need, &need)) {
        LPWSTR value = nullptr;
        if (ConvertSidToStringSidW(((TOKEN_USER*)data.data())->User.Sid, &value)) {
            sid = value;
            LocalFree(value);
        }
    }
    CloseHandle(token);
    return sid;
}
static std::wstring accountName() {
    ULONG need = 0;
    GetUserNameExW(NameSamCompatible, nullptr, &need);
    std::wstring name(need + 1, L'\0');
    if (GetUserNameExW(NameSamCompatible, &name[0], &need)) { name.resize(need); return name; }
    return L"";
}
static std::wstring installedDir() { return envPath(L"USERPROFILE") + L"\\.BrightnessSync"; }
static std::wstring installedExe() { return installedDir() + L"\\BrightnessSync.exe"; }
static std::wstring taskName() { return L"BrightnessSync_" + userSid(); }
static std::wstring shortcutPath() { return envPath(L"APPDATA") + L"\\Microsoft\\Windows\\Start Menu\\Programs\\Brightness Sync (DDC-CI).lnk"; }

struct Reading {
    DWORD minimum = 0, current = 0, maximum = 0;
    bool lowLevel = false;
    bool valid = false;
    int percent() const {
        if (!valid || maximum <= minimum) return 0;
        return (int)std::lround(100.0 * (current - minimum) / (maximum - minimum));
    }
};
struct Display {
    HANDLE handle = nullptr;
    std::wstring device;
    std::wstring description;
    bool primary = false;
    bool internal = false;
    int index = 0;
    Reading reading;
};
static bool hasExternalDisplayPath() {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return true;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return true;
    for (UINT32 i = 0; i < pathCount; i++) {
        auto technology = paths[i].targetInfo.outputTechnology;
        if (technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL &&
            technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS &&
            technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED &&
            technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED) return true;
    }
    return false;
}
static std::vector<std::wstring> internalDisplayNames() {
    std::vector<std::wstring> names;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return names;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return names;
    for (UINT32 i = 0; i < pathCount; i++) {
        auto technology = paths[i].targetInfo.outputTechnology;
        if (technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL &&
            technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS &&
            technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED &&
            technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED) continue;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS)
            names.push_back(source.viewGdiDeviceName);
    }
    return names;
}
static Reading readBrightness(HANDLE handle) {
    Reading r;
    MC_VCP_CODE_TYPE kind;
    DWORD current = 0, maximum = 0;
    if (GetVCPFeatureAndVCPFeatureReply(handle, 0x10, &kind, &current, &maximum) && maximum > 0 && current <= maximum) {
        r.minimum = 0; r.current = current; r.maximum = maximum; r.lowLevel = true; r.valid = true;
        return r;
    }
    DWORD minimum = 0;
    if (GetMonitorBrightness(handle, &minimum, &current, &maximum) && maximum > minimum && current >= minimum && current <= maximum) {
        r.minimum = minimum; r.current = current; r.maximum = maximum; r.valid = true;
    }
    return r;
}
static bool writeBrightness(HANDLE handle, const Reading& reading, int percent) {
    if (!reading.valid) return false;
    percent = std::max(0, std::min(100, percent));
    DWORD value = reading.minimum + (DWORD)std::lround((reading.maximum - reading.minimum) * percent / 100.0);
    if (value == reading.current) return true;
    if (reading.lowLevel) return SetVCPFeature(handle, 0x10, value) != FALSE;
    return SetMonitorBrightness(handle, value) != FALSE;
}
struct DisplayScan { std::vector<Display>* displays; const std::vector<std::wstring>* internalNames; };
static BOOL CALLBACK collectDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    auto& scan = *(DisplayScan*)param;
    auto& displays = *scan.displays;
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0 || count > 16) return TRUE;
    std::vector<PHYSICAL_MONITOR> physical(count);
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physical.data())) return TRUE;
    for (DWORD i = 0; i < count; i++) {
        Display d;
        d.handle = physical[i].hPhysicalMonitor;
        d.device = info.szDevice;
        d.description = physical[i].szPhysicalMonitorDescription;
        d.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        for (const auto& name : *scan.internalNames) if (_wcsicmp(name.c_str(), info.szDevice) == 0) d.internal = true;
        d.index = (int)i;
        d.reading = readBrightness(d.handle);
        displays.push_back(std::move(d));
    }
    return TRUE;
}
static std::vector<Display> listDisplays() {
    std::vector<Display> result;
    auto names = internalDisplayNames();
    DisplayScan scan = { &result, &names };
    EnumDisplayMonitors(nullptr, nullptr, collectDisplay, (LPARAM)&scan);
    return result;
}
static void releaseDisplays(std::vector<Display>& displays) {
    for (auto& d : displays) if (d.handle) { DestroyPhysicalMonitor(d.handle); d.handle = nullptr; }
    displays.clear();
}

class WmiBrightness {
    Ptr<IWbemLocator> locator;
    Ptr<IWbemServices> services;
    Ptr<IEnumWbemClassObject> events;
    ULONGLONG nextSubscribe = 0;
public:
    bool open() {
        if (services) return true;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)locator.out()))) return false;
        if (FAILED(locator->ConnectServer(Bstr(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, services.out()))) return false;
        CoSetProxyBlanket(services.p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        return true;
    }
    bool read(int& percent) {
        if (!open()) return false;
        Ptr<IEnumWbemClassObject> items;
        HRESULT hr = services->ExecQuery(Bstr(L"WQL"), Bstr(L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active = TRUE"),
                                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, items.out());
        if (FAILED(hr) || !items) return false;
        Ptr<IWbemClassObject> object;
        ULONG found = 0;
        hr = items->Next(1000, 1, object.out(), &found);
        if (FAILED(hr) || found != 1 || !object) return false;
        VARIANT value = emptyVariant();
        hr = object->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr);
        if (FAILED(hr)) { VariantClear(&value); return false; }
        int result = -1;
        if (value.vt == VT_UI1) result = value.bVal;
        else if (value.vt == VT_I4) result = value.lVal;
        else if (value.vt == VT_UI4) result = (int)value.ulVal;
        VariantClear(&value);
        if (result < 0 || result > 100) return false;
        percent = result;
        return true;
    }
    bool subscribe() {
        if (events) return true;
        if (GetTickCount64() < nextSubscribe) return false;
        if (!open()) { nextSubscribe = GetTickCount64() + 30000; return false; }
        HRESULT hr = services->ExecNotificationQuery(Bstr(L"WQL"), Bstr(L"SELECT * FROM WmiMonitorBrightnessEvent"),
                                                     WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                                     nullptr, events.out());
        if (FAILED(hr) || !events) { nextSubscribe = GetTickCount64() + 30000; return false; }
        CoSetProxyBlanket(events.p, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        return true;
    }
    bool waitEvent(DWORD timeout, int& percent) {
        if (!subscribe()) { Sleep(timeout); return false; }
        bool received = false;
        for (int i = 0; i < 32; i++) {
            Ptr<IWbemClassObject> object;
            ULONG found = 0;
            HRESULT hr = events->Next(i == 0 ? timeout : 0, 1, object.out(), &found);
            if (FAILED(hr)) { events.out(); nextSubscribe = GetTickCount64() + 5000; break; }
            if (found != 1 || !object) break;
            VARIANT active = emptyVariant();
            if (SUCCEEDED(object->Get(L"Active", 0, &active, nullptr, nullptr))) {
                bool inactive = active.vt == VT_BOOL && active.boolVal == VARIANT_FALSE;
                VariantClear(&active);
                if (inactive) continue;
            }
            VARIANT brightness = emptyVariant();
            if (FAILED(object->Get(L"Brightness", 0, &brightness, nullptr, nullptr))) continue;
            int result = -1;
            if (brightness.vt == VT_UI1) result = brightness.bVal;
            else if (brightness.vt == VT_I4) result = brightness.lVal;
            else if (brightness.vt == VT_UI4) result = (int)brightness.ulVal;
            VariantClear(&brightness);
            if (result < 0 || result > 100) continue;
            percent = result;
            received = true;
        }
        return received;
    }
};
struct Plan {
    bool laptop = false;
    int source = -1;
    std::vector<int> targets;
};
static Plan makePlan(const std::vector<Display>& displays, bool internalAvailable) {
    Plan p;
    p.laptop = internalAvailable;
    if (internalAvailable) {
        for (size_t i = 0; i < displays.size(); i++) if (!displays[i].internal && displays[i].reading.valid) p.targets.push_back((int)i);
    } else {
        for (size_t i = 0; i < displays.size(); i++) if (displays[i].primary && displays[i].reading.valid) { p.source = (int)i; break; }
        if (p.source < 0) for (size_t i = 0; i < displays.size(); i++) if (displays[i].reading.valid) { p.source = (int)i; break; }
        if (p.source < 0) return p;
        for (size_t i = 0; i < displays.size(); i++) if ((int)i != p.source && !displays[i].internal && displays[i].reading.valid) p.targets.push_back((int)i);
    }
    return p;
}
static int runWorker() {
    std::wstring mutexName = L"Local\\BrightnessSync-" + userSid();
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 0; }
    WmiBrightness wmi;
    HWND displayWindow = createDisplayWindow();
    std::vector<Display> displays;
    Plan plan;
    std::wstring lastSource;
    int previousSourcePercent = -1;
    int sourcePercent = -1;
    ULONGLONG nextScan = 0;
    ULONGLONG nextFallbackRead = 0;
    ULONGLONG fastReconnectUntil = 0;
    for (;;) {
        pumpDisplayMessages();
        if (displayConfigurationChanged) {
            displayConfigurationChanged = false;
            nextScan = 0;
            fastReconnectUntil = GetTickCount64() + 10000;
        }
        if (GetTickCount64() >= nextScan) {
            if (plan.targets.empty() && !hasExternalDisplayPath()) {
                DWORD delay = GetTickCount64() < fastReconnectUntil ? 1000 : 5000;
                nextScan = GetTickCount64() + delay;
                waitForDisplayMessages(displayWindow, delay);
                continue;
            }
            releaseDisplays(displays);
            displays = listDisplays();
            int internalPercent = 0;
            plan = makePlan(displays, wmi.read(internalPercent));
            if (plan.targets.empty()) {
                // Keep a sleeping hotplug listener so reconnects do not wait for the minute task.
                releaseDisplays(displays);
                lastSource.clear();
                previousSourcePercent = -1;
                sourcePercent = -1;
                DWORD delay = GetTickCount64() < fastReconnectUntil ? 1000 : 5000;
                nextScan = GetTickCount64() + delay;
                waitForDisplayMessages(displayWindow, delay);
                continue;
            }
            if (plan.laptop) { sourcePercent = internalPercent; wmi.subscribe(); }
            else sourcePercent = -1;
            std::wstring sourceId = plan.laptop ? L"internal:WMI" :
                displays[plan.source].device + L":" + std::to_wstring(displays[plan.source].index) + L":" + displays[plan.source].description;
            if (sourceId != lastSource) { lastSource = sourceId; previousSourcePercent = -1; }
            nextScan = GetTickCount64() + 10000;
            nextFallbackRead = GetTickCount64() + 1000;
        } else if (plan.laptop) {
            int eventPercent = 0;
            if (wmi.waitEvent(250, eventPercent)) {
                // A queued event may predate the most recent scan; prefer the current platform value.
                int currentPercent = 0;
                sourcePercent = wmi.read(currentPercent) ? currentPercent : eventPercent;
            }
            else if (GetTickCount64() >= nextFallbackRead) {
                if (!wmi.read(sourcePercent)) { nextScan = 0; continue; }
                nextFallbackRead = GetTickCount64() + 1000;
            }
        }
        if (!plan.laptop) {
            Reading source = readBrightness(displays[plan.source].handle);
            if (!source.valid) { nextScan = 0; Sleep(500); continue; }
            sourcePercent = source.percent();
        }
        if (sourcePercent >= 0) {
            if (previousSourcePercent < 0) {
                previousSourcePercent = sourcePercent;
            } else if (sourcePercent != previousSourcePercent) {
                int delta = sourcePercent - previousSourcePercent;
                previousSourcePercent = sourcePercent;
                for (int i : plan.targets) {
                    Reading external = readBrightness(displays[i].handle);
                    if (!external.valid) continue;
                    int targetPercent = std::max(0, std::min(100, external.percent() + delta));
                    if (targetPercent != external.percent())
                        writeBrightness(displays[i].handle, external, targetPercent);
                }
            }
        }
        if (!plan.laptop) waitForDisplayMessages(displayWindow, 500);
    }
    releaseDisplays(displays);
    if (displayWindow) DestroyWindow(displayWindow);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

class Scheduler {
    Ptr<ITaskService> service;
    Ptr<ITaskFolder> root;
public:
    bool open() {
        HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)service.out());
        if (FAILED(hr)) return false;
        VARIANT v = emptyVariant();
        if (FAILED(service->Connect(v, v, v, v))) return false;
        return SUCCEEDED(service->GetFolder(Bstr(L"\\"), root.out()));
    }
    Ptr<IRegisteredTask> get() {
        Ptr<IRegisteredTask> task;
        if (root) root->GetTask(Bstr(taskName()), task.out());
        return task;
    }
    bool install(const std::wstring& path, HRESULT& result) {
        Ptr<ITaskDefinition> def;
        result = service->NewTask(0, def.out()); if (FAILED(result)) return false;
        Ptr<IRegistrationInfo> reg;
        if (SUCCEEDED(def->get_RegistrationInfo(reg.out()))) reg->put_Description(Bstr(L"Synchronize supported DDC/CI monitor brightness."));
        Ptr<IPrincipal> principal;
        result = def->get_Principal(principal.out()); if (FAILED(result)) return false;
        std::wstring account = accountName();
        if (account.empty()) { result = E_FAIL; return false; }
        principal->put_UserId(Bstr(account));
        principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        principal->put_RunLevel(TASK_RUNLEVEL_LUA);
        Ptr<ITaskSettings> settings;
        result = def->get_Settings(settings.out()); if (FAILED(result)) return false;
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->put_ExecutionTimeLimit(Bstr(L"PT0S"));
        settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
        Ptr<ITriggerCollection> triggers;
        result = def->get_Triggers(triggers.out()); if (FAILED(result)) return false;
        Ptr<ITrigger> baseLogon;
        result = triggers->Create(TASK_TRIGGER_LOGON, baseLogon.out()); if (FAILED(result)) return false;
        Ptr<ILogonTrigger> logon;
        result = baseLogon->QueryInterface(IID_ILogonTrigger, (void**)logon.out()); if (FAILED(result)) return false;
        logon->put_UserId(Bstr(account));
        Ptr<ITrigger> baseTime;
        result = triggers->Create(TASK_TRIGGER_TIME, baseTime.out()); if (FAILED(result)) return false;
        Ptr<ITimeTrigger> time;
        result = baseTime->QueryInterface(IID_ITimeTrigger, (void**)time.out()); if (FAILED(result)) return false;
        SYSTEMTIME now; GetLocalTime(&now);
        wchar_t boundary[64];
        swprintf(boundary, 64, L"%04u-%02u-%02uT%02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        time->put_StartBoundary(Bstr(boundary));
        Ptr<IRepetitionPattern> repeat;
        result = time->get_Repetition(repeat.out()); if (FAILED(result)) return false;
        repeat->put_Interval(Bstr(L"PT1M"));
        Ptr<IActionCollection> actions;
        result = def->get_Actions(actions.out()); if (FAILED(result)) return false;
        Ptr<IAction> action;
        result = actions->Create(TASK_ACTION_EXEC, action.out()); if (FAILED(result)) return false;
        Ptr<IExecAction> exec;
        result = action->QueryInterface(IID_IExecAction, (void**)exec.out()); if (FAILED(result)) return false;
        exec->put_Path(Bstr(path));
        exec->put_Arguments(Bstr(L"--run"));
        VARIANT empty = emptyVariant();
        Ptr<IRegisteredTask> registered;
        result = root->RegisterTaskDefinition(Bstr(taskName()), def.p, TASK_CREATE_OR_UPDATE,
                                              empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, registered.out());
        return SUCCEEDED(result);
    }
    bool remove() { return SUCCEEDED(root->DeleteTask(Bstr(taskName()), 0)); }
};

static bool createShortcut() {
    Ptr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)link.out()))) return false;
    link->SetPath(installedExe().c_str());
    link->SetArguments(L"--menu");
    link->SetWorkingDirectory(installedDir().c_str());
    Ptr<IPersistFile> persist;
    if (FAILED(link->QueryInterface(IID_IPersistFile, (void**)persist.out()))) return false;
    return SUCCEEDED(persist->Save(shortcutPath().c_str(), TRUE));
}
static bool installApp() {
    if (envPath(L"USERPROFILE").empty()) { line(L"无法找到当前用户目录。"); return false; }
    std::wstring dir = installedDir();
    if (dir.empty() || !CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        line(L"无法创建安装目录：" + errorText(GetLastError())); return false;
    }
    DWORD attributes = GetFileAttributesW(dir.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(dir.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
    Scheduler scheduler;
    if (!scheduler.open()) { line(L"无法连接任务计划服务。"); return false; }
    auto existing = scheduler.get();
    if (existing) existing->Stop(0);
    std::wstring target = installedExe(), source = selfPath();
    if (_wcsicmp(source.c_str(), target.c_str()) != 0 && !CopyFileW(source.c_str(), target.c_str(), FALSE)) {
        line(L"复制 EXE 失败：" + errorText(GetLastError())); return false;
    }
    HRESULT hr = S_OK;
    if (!scheduler.install(target, hr)) {
        line(L"创建自动启动任务失败，HRESULT=" + std::to_wstring((unsigned long)hr)); return false;
    }
    if (!createShortcut()) line(L"提示：开始菜单快捷方式创建失败，但自动同步已安装。");
    auto task = scheduler.get();
    if (task) {
        Ptr<IRunningTask> running;
        HRESULT start = task->Run(emptyVariant(), running.out());
        if (FAILED(start)) line(L"提示：立即运行失败，任务会在下次自动检查时重试。");
    }
    line(L"安装完成。开始菜单中可找到 Brightness Sync (DDC-CI)。");
    return true;
}
static void stopApp() {
    Scheduler scheduler;
    if (!scheduler.open()) { line(L"无法连接任务计划服务。"); return; }
    auto task = scheduler.get();
    if (!task) { line(L"尚未安装。"); return; }
    task->Stop(0);
    if (SUCCEEDED(task->put_Enabled(VARIANT_FALSE))) line(L"已暂停：自动检查和同步已关闭。");
    else line(L"暂停失败。");
}
static void startApp() {
    Scheduler scheduler;
    if (!scheduler.open()) { line(L"无法连接任务计划服务。"); return; }
    auto task = scheduler.get();
    if (!task) { line(L"尚未安装。"); return; }
    task->put_Enabled(VARIANT_TRUE);
    Ptr<IRunningTask> running;
    if (SUCCEEDED(task->Run(emptyVariant(), running.out()))) line(L"已恢复自动同步。");
    else line(L"自动检查已开启，立即运行失败；请稍后查看状态。");
}
static bool scheduleSelfCleanup() {
    wchar_t tempDir[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDir) || !GetTempFileNameW(tempDir, L"BSC", 0, tempFile)) return false;
    std::wstring script = tempFile;
    script.replace(script.size() - 4, 4, L".cmd");
    if (!MoveFileExW(tempFile, script.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tempFile); return false; }
    HANDLE file = CreateFileW(script.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { DeleteFileW(script.c_str()); return false; }
    const char* content =
        "@echo off\r\n"
        "setlocal DisableDelayedExpansion\r\n"
        "for /L %%n in (1,1,15) do (\r\n"
        "  del /f /q \"%USERPROFILE%\\.BrightnessSync\\BrightnessSync.exe\" >nul 2>&1\r\n"
        "  if not exist \"%USERPROFILE%\\.BrightnessSync\\BrightnessSync.exe\" goto done\r\n"
        "  ping -n 2 127.0.0.1 >nul\r\n"
        ")\r\n"
        ":done\r\n"
        "rmdir \"%USERPROFILE%\\.BrightnessSync\" >nul 2>&1\r\n"
        "del /f /q \"%~f0\" >nul 2>&1\r\n";
    DWORD written = 0;
    bool saved = WriteFile(file, content, (DWORD)strlen(content), &written, nullptr) && written == strlen(content);
    CloseHandle(file);
    if (!saved) { DeleteFileW(script.c_str()); return false; }
    std::wstring systemCmd = envPath(L"SystemRoot") + L"\\System32\\cmd.exe";
    std::wstring command = L"\"" + systemCmd + L"\" /d /s /c \"\"" + script + L"\"\"";
    STARTUPINFOW startup = {}; startup.cb = sizeof(startup); startup.wShowWindow = SW_HIDE; startup.dwFlags = STARTF_USESHOWWINDOW;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(systemCmd.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, tempDir, &startup, &process)) { DeleteFileW(script.c_str()); return false; }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
static void uninstallApp() {
    Scheduler scheduler;
    if (!scheduler.open()) { line(L"无法连接任务计划服务。"); return; }
    auto task = scheduler.get();
    if (task) { task->Stop(0); scheduler.remove(); }
    DeleteFileW(shortcutPath().c_str());
    std::wstring target = installedExe();
    if (!DeleteFileW(target.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        if (_wcsicmp(selfPath().c_str(), target.c_str()) == 0 && scheduleSelfCleanup())
            line(L"卸载完成，程序文件将在退出后自动删除。");
        else
            line(L"任务和快捷方式已移除；请手动删除：" + target);
    } else {
        RemoveDirectoryW(installedDir().c_str());
        line(L"卸载完成。");
    }
}
static void statusApp() {
    Scheduler scheduler;
    if (!scheduler.open()) { line(L"状态：任务计划不可用"); return; }
    auto task = scheduler.get();
    if (!task) { line(L"状态：未安装"); return; }
    TASK_STATE state;
    if (FAILED(task->get_State(&state))) { line(L"状态：已安装，无法读取运行状态"); return; }
    if (state == TASK_STATE_RUNNING) line(L"状态：正在同步");
    else if (state == TASK_STATE_DISABLED) line(L"状态：已暂停");
    else line(L"状态：自动检查已开启，等待显示器或下次检查");
}
static void listApp() {
    WmiBrightness wmi;
    int internal = 0;
    bool hasInternal = wmi.read(internal);
    if (hasInternal) line(L"内屏亮度：" + std::to_wstring(internal) + L"%");
    auto displays = listDisplays();
    if (displays.empty()) line(L"未检测到物理显示器。");
    for (const auto& d : displays) {
        std::wstring s = d.device + L"  " + d.description;
        if (d.primary) s += L"  [主屏]";
        if (d.internal) s += L"  [内屏]";
        s += d.reading.valid ? L"  " + std::to_wstring(d.reading.percent()) + L"%" : L"  [DDC/CI 亮度不可读]";
        line(s);
    }
    Plan plan = makePlan(displays, hasInternal);
    line(L"可同步目标数：" + std::to_wstring(plan.targets.size()));
    releaseDisplays(displays);
}
static void menu() {
    for (;;) {
        line(L"\n========== Brightness Sync 亮度同步 ==========");
        statusApp();
        line(L"1 安装并开启    2 暂停    3 继续    4 卸载");
        line(L"5 查看显示器    0 退出");
        print(L"请输入数字：");
        std::wstring choice = readLine();
        if (choice == L"0" || choice.empty()) return;
        if (choice == L"1") installApp();
        else if (choice == L"2") stopApp();
        else if (choice == L"3") startApp();
        else if (choice == L"4") uninstallApp();
        else if (choice == L"5") listApp();
        else line(L"无效选项。");
    }
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring mode = argc > 1 ? argv[1] : L"--menu";
    if (argv) LocalFree(argv);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT security = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                                             RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                                             nullptr, EOAC_NONE, nullptr);
    (void)security;
    if (mode == L"--run") { int code = runWorker(); CoUninitialize(); return code; }
    openConsole();
    if (mode == L"--menu") menu();
    else if (mode == L"--install") installApp();
    else if (mode == L"--start") startApp();
    else if (mode == L"--stop") stopApp();
    else if (mode == L"--uninstall") uninstallApp();
    else if (mode == L"--status") statusApp();
    else if (mode == L"--list") listApp();
    else line(L"用法：BrightnessSync.exe [--menu|--install|--start|--stop|--uninstall|--status|--list]");
    CoUninitialize();
    return 0;
}
