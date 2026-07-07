#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TvtTape.h"

#include <algorithm>

TVTest::CTVTestPlugin *CreatePluginClass();

namespace {

constexpr wchar_t kPluginName[] = L"TvtTape";
constexpr wchar_t kPluginDescription[] = L"VCR control plugin with BonDriver_Pipe integration";

const wchar_t *const kControlTokensPlay[6] = {
    L"\xD83D\xDCFC\xFE0E ",
    L"\x23F9\xFE0E ",
    L"\x23EA\xFE0E ",
    L"\x25B6\xFE0E ",
    L"\x23E9\xFE0E",
    L"\x23FA\xFE0E",
};

const wchar_t *const kControlTokensPause[6] = {
    L"\xD83D\xDCFC\xFE0E ",
    L"\x23F9\xFE0E ",
    L"\x23EA\xFE0E ",
    L"\x23F8\xFE0E ",
    L"\x23E9\xFE0E",
    L"\x23FA\xFE0E",
};

int g_ControlTokenWidthPlay[6] = {0, 0, 0, 0, 0, 0};
int g_ControlTokenWidthPause[6] = {0, 0, 0, 0, 0, 0};
bool g_ControlTokenWidthPlayValid = false;
bool g_ControlTokenWidthPauseValid = false;
CTvtTape *g_TimerOwner = nullptr;

const wchar_t *GetControlText(bool showPause)
{
    return showPause
        ? L"\xD83D\xDCFC\xFE0E \x23F9\xFE0E \x23EA\xFE0E \x23F8\xFE0E \x23E9\xFE0E \x23FA\xFE0E"
        : L"\xD83D\xDCFC\xFE0E \x23F9\xFE0E \x23EA\xFE0E \x25B6\xFE0E \x23E9\xFE0E \x23FA\xFE0E";
}

void UpdateControlTokenWidths(HDC hdc, bool showPause)
{
    if (hdc == nullptr)
        return;

    int *widths = showPause ? g_ControlTokenWidthPause : g_ControlTokenWidthPlay;
    bool *valid = showPause ? &g_ControlTokenWidthPauseValid : &g_ControlTokenWidthPlayValid;
    const wchar_t *const *tokens = showPause ? kControlTokensPause : kControlTokensPlay;

    bool ok = true;
    for (int i = 0; i < 6; ++i) {
        SIZE size = {};
        const int len = static_cast<int>(wcslen(tokens[i]));
        if (GetTextExtentPoint32W(hdc, tokens[i], len, &size) && size.cx > 0) {
            widths[i] = size.cx;
        } else {
            ok = false;
            break;
        }
    }

    if (ok)
        *valid = true;
}

int HitTestControlIndex(const TVTest::StatusItemMouseEventInfo *pInfo, bool showPause)
{
    if (!pInfo || !pInfo->hwnd)
        return -1;

    RECT rc = pInfo->ContentRect;
    rc.left += 4;
    rc.right -= 4;
    const int width = rc.right - rc.left;
    if (width <= 0)
        return -1;

    const int x = pInfo->CursorPos.x - rc.left;
    if (x < 0)
        return -1;

    int tokenWidth[6] = {0, 0, 0, 0, 0, 0};
    const int *cachedWidth = showPause ? g_ControlTokenWidthPause : g_ControlTokenWidthPlay;
    const bool cacheValid = showPause ? g_ControlTokenWidthPauseValid : g_ControlTokenWidthPlayValid;

    int fallbackWidth = width / 6;
    if (fallbackWidth <= 0)
        fallbackWidth = 1;

    for (int i = 0; i < 6; ++i) {
        tokenWidth[i] = (cacheValid && cachedWidth[i] > 0) ? cachedWidth[i] : fallbackWidth;
    }

    int left = 0;
    for (int i = 0; i < 6; ++i) {
        const int right = left + tokenWidth[i];
        if (x >= left && x < right)
            return i;
        left = right;
    }

    return -1;
}

} // namespace

CTvtTape::CTvtTape()
    : m_TimerID(0)
    , m_PollIntervalMs(500)
    , m_SelectedDeviceIndex(-1)
    , m_StateText(L"INIT")
    , m_TimeCodeText(L"--:--:--:--")
    , m_ZeroBitrateStartTick(0)
    , m_RecordStopTriggered(false)
{
}

CTvtTape::~CTvtTape()
{
}

bool CTvtTape::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
    if (!pInfo)
        return false;

    pInfo->Type = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags = 0;
    pInfo->pszPluginName = kPluginName;
    pInfo->pszCopyright = L"2026";
    pInfo->pszDescription = kPluginDescription;
    return true;
}

bool CTvtTape::Initialize()
{
    if (!m_pApp->SetEventCallback(EventCallback, this))
        return false;

    m_VcrDevice.SetTsDataCallback([this](const BYTE *pData, size_t size) {
        if (!m_PipeControl.SendTsData(pData, size)) {
            static bool logged = false;
            if (!logged) {
                m_pApp->AddLog(L"TvtTape: failed to send TS to BonDriver_Pipe data pipe", TVTest::LOG_TYPE_WARNING);
                logged = true;
            }
        }
    });

    LoadSettings();
    RegisterStatusItems();
    SetStatusItemsVisible(m_pApp->IsPluginEnabled());

    if (m_pApp->IsPluginEnabled()) {
        m_VcrDevice.SetPreferredDeviceIndex(m_SelectedDeviceIndex);
        if (!m_VcrDevice.Open()) {
            m_StateText = L"DEVICE OPEN FAILED";
            m_pApp->AddLog(L"TvtTape: failed to open VCR device", TVTest::LOG_TYPE_WARNING);
        }
        g_TimerOwner = this;
        m_TimerID = SetTimer(nullptr, 0, m_PollIntervalMs, TimerProc);
        if (!m_TimerID)
            m_pApp->AddLog(L"TvtTape: failed to start status poll timer", TVTest::LOG_TYPE_WARNING);
    } else {
        m_StateText = L"DISABLED";
        m_TimeCodeText = L"--:--:--:--";
    }

    UpdateStatus();
    return true;
}

bool CTvtTape::Finalize()
{
    if (m_TimerID) {
        KillTimer(nullptr, m_TimerID);
        m_TimerID = 0;
    }
    if (g_TimerOwner == this)
        g_TimerOwner = nullptr;

    m_VcrDevice.Close();
    SaveSettings();
    return true;
}

bool CTvtTape::OnPluginEnable(bool fEnable)
{
    SetStatusItemsVisible(fEnable);

    if (fEnable) {
        m_VcrDevice.SetPreferredDeviceIndex(m_SelectedDeviceIndex);
        if (!m_VcrDevice.Open()) {
            m_StateText = L"DEVICE OPEN FAILED";
            m_pApp->AddLog(L"TvtTape: failed to open VCR device", TVTest::LOG_TYPE_WARNING);
        }
        if (!m_TimerID) {
            g_TimerOwner = this;
            m_TimerID = SetTimer(nullptr, 0, m_PollIntervalMs, TimerProc);
            if (!m_TimerID)
                m_pApp->AddLog(L"TvtTape: failed to start status poll timer", TVTest::LOG_TYPE_WARNING);
        }
    } else {
        if (m_TimerID) {
            KillTimer(nullptr, m_TimerID);
            m_TimerID = 0;
        }
        if (g_TimerOwner == this)
            g_TimerOwner = nullptr;
        m_VcrDevice.Close();
        m_StateText = L"DISABLED";
        m_TimeCodeText = L"--:--:--:--";
    }

    RedrawStatusItems();
    return true;
}

bool CTvtTape::OnStatusItemDraw(TVTest::StatusItemDrawInfo *pInfo)
{
    if (!pInfo)
        return false;

    RECT rc = pInfo->DrawRect;
    rc.left += 4;
    rc.right -= 4;
    SetBkMode(pInfo->hdc, TRANSPARENT);
    SetTextColor(pInfo->hdc, pInfo->Color);

    if (pInfo->ID == STATUS_ITEM_STATE) {
        DrawTextW(pInfo->hdc, m_StateText.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return true;
    }

    if (pInfo->ID == STATUS_ITEM_TIMECODE) {
        DrawTextW(pInfo->hdc, m_TimeCodeText.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return true;
    }

    if (pInfo->ID == STATUS_ITEM_BUTTONS) {
        const bool showPause = m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::Play;
        UpdateControlTokenWidths(pInfo->hdc, showPause);
        // [tape menu] [stop] [rew] [play/pause toggle] [ff] [record]
        const wchar_t *controlText = GetControlText(showPause);
        DrawTextW(pInfo->hdc, controlText, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return true;
    }

    return false;
}

bool CTvtTape::OnStatusItemMouseEvent(TVTest::StatusItemMouseEventInfo *pInfo)
{
    if (!pInfo)
        return false;

    if (pInfo->Action != TVTest::STATUS_ITEM_MOUSE_ACTION_LDOWN &&
        pInfo->Action != TVTest::STATUS_ITEM_MOUSE_ACTION_LDOUBLECLICK)
        return false;

    if (pInfo->ID != STATUS_ITEM_BUTTONS)
        return false;

    const bool showPause = m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::Play;
    const int index = HitTestControlIndex(pInfo, showPause);
    if (index < 0)
        return true;

    switch (index) {
    case 0:
        ShowDeviceMenu(pInfo);
        break;
    case 1:
        m_VcrDevice.Stop();
        m_PipeControl.Purge();
        break;
    case 2:
        if (m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::Rewind)
            m_VcrDevice.Play();
        else
            m_VcrDevice.Rewind();
        break;
    case 3:
        if (m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::Play) {
            m_VcrDevice.Pause();
            m_PipeControl.SetPaused(true);
        } else {
            m_VcrDevice.Play();
            m_PipeControl.SetPaused(false);
        }
        break;
    case 4:
        if (m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::FastForward)
            m_VcrDevice.Play();
        else
            m_VcrDevice.FastForward();
        break;
    case 5:
        TVTest::RecordStatusInfo recordStatus = {};
        if (!m_pApp->GetRecordStatus(&recordStatus)) {
            break;
        }

        if (recordStatus.Status != TVTest::RECORD_STATUS_RECORDING) {
            m_VcrDevice.Play();
            m_PipeControl.SetPaused(false);
            if (m_pApp->StartRecord()) {
                m_pApp->AddLog(L"TvtTape: recording started", TVTest::LOG_TYPE_INFORMATION);
            } else {
                m_pApp->AddLog(L"TvtTape: failed to start recording", TVTest::LOG_TYPE_WARNING);
            }
        } else {
            m_VcrDevice.Stop();
            m_PipeControl.Purge();
            if (m_pApp->StopRecord()) {
                m_pApp->AddLog(L"TvtTape: recording stopped (bitrate was 0 for over 5 seconds)", TVTest::LOG_TYPE_WARNING);
            } else {
                m_pApp->AddLog(L"TvtTape: failed to stop recording after bitrate watchdog timeout", TVTest::LOG_TYPE_WARNING);
            }
        }
        break;
    }
    UpdateStatus();
    return true;
}

LRESULT CALLBACK CTvtTape::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
{
    auto *pThis = static_cast<CTvtTape*>(pClientData);
    return pThis ? pThis->HandleEvent(Event, lParam1, lParam2, pClientData) : 0;
}

void CALLBACK CTvtTape::TimerProc(HWND, UINT, UINT_PTR idEvent, DWORD)
{
    auto *pThis = g_TimerOwner;
    if (pThis && pThis->m_TimerID == idEvent)
        pThis->MonitorRecordingBitrate();
}

void CTvtTape::LoadSettings()
{
    m_IniPath = GetIniPath();
    m_SelectedDeviceIndex = GetPrivateProfileIntW(L"Device", L"SelectedDeviceIndex", -1, m_IniPath.c_str());
    int interval = GetPrivateProfileIntW(L"Status", L"PollIntervalMs", 500, m_IniPath.c_str());
    interval = std::clamp(interval, 100, 5000);
    m_PollIntervalMs = static_cast<UINT>(interval);
    const int channel = GetPrivateProfileIntW(L"Pipe", L"Channel", 0, m_IniPath.c_str());
    m_PipeControl.SetChannel(channel);
}

void CTvtTape::SaveSettings() const
{
    wchar_t value[32] = {};

    swprintf_s(value, L"%d", m_SelectedDeviceIndex);
    WritePrivateProfileStringW(L"Device", L"SelectedDeviceIndex", value, m_IniPath.c_str());

    swprintf_s(value, L"%u", m_PollIntervalMs);
    WritePrivateProfileStringW(L"Status", L"PollIntervalMs", value, m_IniPath.c_str());

    swprintf_s(value, L"%d", m_PipeControl.GetChannel());
    WritePrivateProfileStringW(L"Pipe", L"Channel", value, m_IniPath.c_str());
}

std::wstring CTvtTape::GetIniPath() const
{
    HMODULE hModule = nullptr;
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CreatePluginClass),
            &hModule)) {
        return L"TvtTape.ini";
    }

    DWORD length = GetModuleFileNameW(hModule, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return L"TvtTape.ini";

    std::wstring path(modulePath);
    const size_t extPos = path.find_last_of(L'.');
    const size_t sepPos = path.find_last_of(L"\\/");
    if (extPos != std::wstring::npos && (sepPos == std::wstring::npos || extPos > sepPos))
        path.erase(extPos);
    path += L".ini";
    return path;
}

void CTvtTape::RegisterStatusItems()
{
    TVTest::StatusItemInfo info = {};
    info.Size = sizeof(info);
    info.Flags = 0;
    info.Style = TVTest::STATUS_ITEM_STYLE_VARIABLEWIDTH;

    info.ID = STATUS_ITEM_STATE;
    info.pszIDText = L"TvtTape.State";
    info.pszName = L"TvtTape State";
    info.MinWidth = 30;
    info.MaxWidth = 200;
    info.DefaultWidth = 150;
    m_pApp->RegisterStatusItem(&info);

    info.ID = STATUS_ITEM_TIMECODE;
    info.pszIDText = L"TvtTape.TimeCode";
    info.pszName = L"TvtTape TimeCode";
    info.MinWidth = 30;
    info.MaxWidth = 100;
    info.DefaultWidth = 50;
    m_pApp->RegisterStatusItem(&info);

    info.ID = STATUS_ITEM_BUTTONS;
    info.pszIDText = L"TvtTape.Buttons";
    info.pszName = L"TvtTape Control";
    info.MinWidth = 100;
    info.MaxWidth = 180;
    info.DefaultWidth = 150;
    m_pApp->RegisterStatusItem(&info);

    TVTest::StatusItemSetInfo setInfo = {};
    setInfo.Size = sizeof(setInfo);
    setInfo.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
    setInfo.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
    setInfo.State = TVTest::STATUS_ITEM_STATE_VISIBLE;

    setInfo.ID = STATUS_ITEM_STATE;
    m_pApp->SetStatusItem(&setInfo);
    setInfo.ID = STATUS_ITEM_TIMECODE;
    m_pApp->SetStatusItem(&setInfo);
    setInfo.ID = STATUS_ITEM_BUTTONS;
    m_pApp->SetStatusItem(&setInfo);
}

void CTvtTape::SetStatusItemsVisible(bool visible)
{
    TVTest::StatusItemSetInfo setInfo = {};
    setInfo.Size = sizeof(setInfo);
    setInfo.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
    setInfo.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
    setInfo.State = visible ? TVTest::STATUS_ITEM_STATE_VISIBLE : TVTest::STATUS_ITEM_STATE_NONE;

    setInfo.ID = STATUS_ITEM_STATE;
    m_pApp->SetStatusItem(&setInfo);
    setInfo.ID = STATUS_ITEM_TIMECODE;
    m_pApp->SetStatusItem(&setInfo);
    setInfo.ID = STATUS_ITEM_BUTTONS;
    m_pApp->SetStatusItem(&setInfo);
}

void CTvtTape::RedrawStatusItems()
{
    m_pApp->StatusItemNotify(STATUS_ITEM_STATE, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
    m_pApp->StatusItemNotify(STATUS_ITEM_TIMECODE, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
    m_pApp->StatusItemNotify(STATUS_ITEM_BUTTONS, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
}

void CTvtTape::UpdateStatus()
{

    if (!m_VcrDevice.IsOpen()) {
        m_DeviceNames.clear();
        if (m_VcrDevice.EnumDevices(&m_DeviceNames) && !m_DeviceNames.empty()) {
            m_StateText = L"DEVICE DETECTED";
        } else {
            m_StateText = L"NOT CONNECTED";
        }

        m_TimeCodeText = L"--:--:--:--";
        RedrawStatusItems();
        return;
    }

    m_VcrDevice.UpdateTransportState();
    m_StateText = m_VcrDevice.GetTransportStateText();

    long hour = 0, minute = 0, second = 0, frame = 0;
    if (m_VcrDevice.GetTimeCode(&hour, &minute, &second, &frame)) {
        wchar_t tc[32] = {};
        swprintf_s(tc, L"%02ld:%02ld:%02ld:%02ld", hour, minute, second, frame);
        m_TimeCodeText = tc;
    } else {
        m_TimeCodeText = L"--:--:--:--";
    }

    RedrawStatusItems();
}

void CTvtTape::MonitorRecordingBitrate()
{
    TVTest::RecordStatusInfo recordStatus = {};
    if (!m_pApp->GetRecordStatus(&recordStatus)) {
        m_ZeroBitrateStartTick = 0;
        m_RecordStopTriggered = false;
        return;
    }

    if (recordStatus.Status != TVTest::RECORD_STATUS_RECORDING) {
        m_ZeroBitrateStartTick = 0;
        m_RecordStopTriggered = false;
        return;
    }

    TVTest::StatusInfo status = {};
    if (!m_pApp->GetStatus(&status)) {
        m_ZeroBitrateStartTick = 0;
        return;
    }

    if (status.BitRate > 0) {
        m_ZeroBitrateStartTick = 0;
        m_RecordStopTriggered = false;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (m_ZeroBitrateStartTick == 0) {
        m_ZeroBitrateStartTick = now;
        return;
    }

    if (!m_RecordStopTriggered && (now - m_ZeroBitrateStartTick) >= 5000ULL) {
        m_VcrDevice.Stop();
        m_PipeControl.Purge();
        if (m_pApp->StopRecord()) {
            m_pApp->AddLog(L"TvtTape: recording stopped (bitrate was 0 for over 5 seconds)", TVTest::LOG_TYPE_WARNING);
        } else {
            m_pApp->AddLog(L"TvtTape: failed to stop recording after bitrate watchdog timeout", TVTest::LOG_TYPE_WARNING);
        }
        m_RecordStopTriggered = true;
    }
}

bool CTvtTape::ReopenDevice()
{
    m_VcrDevice.Close();
    m_VcrDevice.SetPreferredDeviceIndex(m_SelectedDeviceIndex);
    if (!m_VcrDevice.Open())
        return false;

    UpdateStatus();
    return true;
}

bool CTvtTape::ShowDeviceMenu(const TVTest::StatusItemMouseEventInfo *pInfo)
{
    if (!pInfo || !pInfo->hwnd)
        return false;

    m_DeviceNames.clear();
    m_VcrDevice.EnumDevices(&m_DeviceNames);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
        return false;

    AppendMenuW(hMenu, MF_STRING | (m_SelectedDeviceIndex < 0 ? MF_CHECKED : 0), MENU_DEVICE_AUTO, L"Auto select");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    for (size_t i = 0; i < m_DeviceNames.size(); ++i) {
        UINT flags = MF_STRING;
        if (m_SelectedDeviceIndex == static_cast<int>(i))
            flags |= MF_CHECKED;
        AppendMenuW(hMenu, flags, MENU_DEVICE_FIRST + static_cast<UINT>(i), m_DeviceNames[i].c_str());
    }

    POINT pt = pInfo->CursorPos;
    ClientToScreen(pInfo->hwnd, &pt);
    const UINT command = TrackPopupMenu(
        hMenu,
        TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON,
        pt.x,
        pt.y,
        0,
        pInfo->hwnd,
        nullptr);

    DestroyMenu(hMenu);

    int selected = m_SelectedDeviceIndex;
    if (command == MENU_DEVICE_AUTO)
        selected = -1;
    else if (command >= MENU_DEVICE_FIRST)
        selected = static_cast<int>(command - MENU_DEVICE_FIRST);

    if (selected == m_SelectedDeviceIndex)
        return true;

    m_SelectedDeviceIndex = selected;
    SaveSettings();
    if (!ReopenDevice()) {
        m_pApp->AddLog(L"TvtTape: selected device open failed", TVTest::LOG_TYPE_WARNING);
        return false;
    }

    return true;
}

TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTvtTape();
}
