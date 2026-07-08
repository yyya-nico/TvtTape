#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TvtTape.h"
#include "resource.h"

#include <algorithm>

TVTest::CTVTestPlugin *CreatePluginClass();

namespace {

constexpr wchar_t kPluginName[] = L"TvtTape";
constexpr wchar_t kPluginDescription[] = L"VCR 制御プラグイン BonDriver_Pipe 対応";
constexpr wchar_t kDefaultIconBitmap[] = L"TvtTapeButtons.bmp";
constexpr int kBitmapIconCount = 10;
constexpr int kStateWidth = 96;
constexpr int kTimeCodeWidth = 80;
constexpr int kButtonWidthFallback = 20;
constexpr COLORREF kPowerOnColor = RGB(0xCC, 0x00, 0x00);

enum UiItemId {
    UI_ITEM_DEVICE = 1,
    UI_ITEM_POWER,
    UI_ITEM_REW,
    UI_ITEM_PLAY_PAUSE,
    UI_ITEM_STOP,
    UI_ITEM_FF,
    UI_ITEM_RECORD,
    UI_ITEM_STATE,
    UI_ITEM_TIMECODE,
};

enum TransportAction {
    TRANSPORT_POWER = 0,
    TRANSPORT_REW,
    TRANSPORT_PLAY_PAUSE,
    TRANSPORT_STOP,
    TRANSPORT_FF,
    TRANSPORT_RECORD,
};

enum TransportIconIndex {
    ICON_VCR = 0,
    ICON_POWER_OFF,
    ICON_POWER_ON,
    ICON_REW,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_STOP,
    ICON_FF,
    ICON_RECORD,
    ICON_RECORD_STOP,
};

CTvtTape *g_TimerOwner = nullptr;

bool IsAbsolutePath(const std::wstring &path)
{
    return path.size() >= 2 && path[1] == L':' || (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
}

const wchar_t *GetFallbackLabel(int action, bool showPause)
{
    switch (action) {
    case TRANSPORT_POWER:
        return showPause ? L"電源オン" : L"電源オフ";
    case TRANSPORT_REW:
        return L"巻き戻し";
    case TRANSPORT_PLAY_PAUSE:
        return showPause ? L"一時停止" : L"再生";
    case TRANSPORT_STOP:
        return L"停止";
    case TRANSPORT_FF:
        return L"早送り";
    case TRANSPORT_RECORD:
        return showPause ? L"録画停止" : L"録画";
    default:
        return L"";
    }
}

class CDeviceStatusItem : public CStatusItem
{
public:
    CDeviceStatusItem(CStatusView *pStatus, CTvtTape *pOwner, int width)
        : CStatusItem(pStatus, UI_ITEM_DEVICE, width)
        , m_pOwner(pOwner)
    {
        m_MinWidth = width;
    }

    void Draw(HDC hdc, const RECT *pRect) override
    {
        if (!m_pOwner->DrawTransportIcon(hdc, *pRect, ICON_VCR, ::GetTextColor(hdc))) {
            RECT rc = *pRect;
            ::DrawTextW(hdc, L"VCR", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    void OnLButtonDown(int, int) override
    {
        POINT pt = {};
        UINT flags = 0;
        if (GetMenuPos(&pt, &flags))
            m_pOwner->ShowDeviceMenuAt(pt, flags | TPM_RIGHTBUTTON, m_pStatus->GetHandle());
    }

private:
    CTvtTape *m_pOwner;
};

class CTransportButtonItem : public CStatusItem
{
public:
    CTransportButtonItem(CStatusView *pStatus, CTvtTape *pOwner, int id, int action, int width)
        : CStatusItem(pStatus, id, width)
        , m_pOwner(pOwner)
        , m_Action(action)
    {
        m_MinWidth = width;
    }

    void Draw(HDC hdc, const RECT *pRect) override
    {
        int iconIndex = ICON_PLAY;
        COLORREF iconColor = ::GetTextColor(hdc);
        switch (m_Action) {
        case TRANSPORT_POWER:
            iconIndex = m_pOwner->IsPowered() ? ICON_POWER_ON : ICON_POWER_OFF;
            if (iconIndex == ICON_POWER_ON)
                iconColor = kPowerOnColor;
            break;
        case TRANSPORT_REW:
            iconIndex = ICON_REW;
            break;
        case TRANSPORT_PLAY_PAUSE:
            iconIndex = m_pOwner->IsPlaying() ? ICON_PAUSE : ICON_PLAY;
            break;
        case TRANSPORT_STOP:
            iconIndex = ICON_STOP;
            break;
        case TRANSPORT_FF:
            iconIndex = ICON_FF;
            break;
        case TRANSPORT_RECORD:
            iconIndex = m_pOwner->IsRecording() ? ICON_RECORD_STOP : ICON_RECORD;
            iconColor = m_pOwner->GetApp()->GetColor(L"StatusRecordingCircle");
            break;
        }

        if (!m_pOwner->DrawTransportIcon(hdc, *pRect, iconIndex, iconColor)) {
            RECT rc = *pRect;
            const bool state = m_Action == TRANSPORT_PLAY_PAUSE ? m_pOwner->IsPlaying() :
                m_Action == TRANSPORT_POWER ? m_pOwner->IsPowered() :
                m_Action == TRANSPORT_RECORD ? m_pOwner->IsRecording() : false;
            ::DrawTextW(hdc, GetFallbackLabel(m_Action, state), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    void OnLButtonDown(int, int) override
    {
        m_pOwner->ExecuteTransportAction(m_Action);
    }

private:
    CTvtTape *m_pOwner;
    int m_Action;
};

class CTimeCodeStatusItem : public CStatusItem
{
public:
    CTimeCodeStatusItem(CStatusView *pStatus, CTvtTape *pOwner, int width)
        : CStatusItem(pStatus, UI_ITEM_TIMECODE, width)
        , m_pOwner(pOwner)
    {
        m_MinWidth = 80;
    }

    void Draw(HDC hdc, const RECT *pRect) override
    {
        RECT rc = *pRect;
        ::DrawTextW(hdc, m_pOwner->GetTimeCodeText().c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

private:
    CTvtTape *m_pOwner;
};

class CTransportStateItem : public CStatusItem
{
public:
    CTransportStateItem(CStatusView *pStatus, CTvtTape *pOwner, int width)
        : CStatusItem(pStatus, UI_ITEM_STATE, width)
        , m_pOwner(pOwner)
    {
        m_MinWidth = 72;
    }

    void Draw(HDC hdc, const RECT *pRect) override
    {
        RECT rc = *pRect;
        ::DrawTextW(hdc, m_pOwner->GetStateText().c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

private:
    CTvtTape *m_pOwner;
};

} // namespace

CTvtTape::CTvtTape()
    : m_TimerID(0)
    , m_PollIntervalMs(500)
    , m_ButtonIconSize(0)
    , m_StatusViewInitialized(false)
    , m_SelectedDeviceIndex(-1)
    , m_StateText(L"初期化中...")
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
    pInfo->Flags = TVTest::PLUGIN_FLAG_NONE;
    pInfo->pszPluginName = kPluginName;
    pInfo->pszCopyright = L"2026 yyya_nico";
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
    InitializeStatusView();
    LoadButtonBitmap();
    RegisterStatusItems();
    SetStatusItemsVisible(m_pApp->IsPluginEnabled());

    if (m_pApp->IsPluginEnabled()) {
        m_VcrDevice.SetPreferredDeviceIndex(m_SelectedDeviceIndex);
        if (!m_VcrDevice.Open()) {
            m_StateText = L"デバイスのオープンに失敗しました";
            m_pApp->AddLog(L"TvtTape: デバイスのオープンに失敗しました", TVTest::LOG_TYPE_WARNING);
        }
        g_TimerOwner = this;
        m_TimerID = ::SetTimer(nullptr, 0, m_PollIntervalMs, TimerProc);
        if (!m_TimerID)
            m_pApp->AddLog(L"TvtTape: ステータス監視タイマーの開始に失敗しました", TVTest::LOG_TYPE_WARNING);
    } else {
        m_StateText = L"無効";
        m_TimeCodeText = L"--:--:--:--";
    }

    UpdateStatus();
    return true;
}

bool CTvtTape::Finalize()
{
    if (m_TimerID) {
        ::KillTimer(nullptr, m_TimerID);
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
        LoadButtonBitmap();
        m_VcrDevice.SetPreferredDeviceIndex(m_SelectedDeviceIndex);
        if (!m_VcrDevice.Open()) {
            m_StateText = L"デバイスのオープンに失敗しました";
            m_pApp->AddLog(L"TvtTape: デバイスのオープンに失敗しました", TVTest::LOG_TYPE_WARNING);
        }
        if (!m_TimerID) {
            g_TimerOwner = this;
            m_TimerID = ::SetTimer(nullptr, 0, m_PollIntervalMs, TimerProc);
            if (!m_TimerID)
                m_pApp->AddLog(L"TvtTape: ステータス監視タイマーの開始に失敗しました", TVTest::LOG_TYPE_WARNING);
        }
    } else {
        if (m_TimerID) {
            ::KillTimer(nullptr, m_TimerID);
            m_TimerID = 0;
        }
        if (g_TimerOwner == this)
            g_TimerOwner = nullptr;
        m_VcrDevice.Close();
        m_StateText = L"無効";
        m_TimeCodeText = L"--:--:--:--";
    }

    UpdateStatus();
    return true;
}

bool CTvtTape::OnStatusItemDraw(TVTest::StatusItemDrawInfo *pInfo)
{
    if (!pInfo || pInfo->ID != STATUS_ITEM_TRANSPORT_ROW)
        return false;

    if (pInfo->Flags & TVTest::STATUS_ITEM_DRAW_FLAG_PREVIEW) {
        m_pApp->ThemeDrawText(pInfo->pszStyle, pInfo->hdc, L"TvtTape ステータスバー", pInfo->DrawRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE, pInfo->Color);
        return true;
    }

    AdjustStatusLayout();
    m_pApp->ThemeDrawBackground(L"status-bar.item", pInfo->hdc, pInfo->ItemRect);

    RECT hotRect = {};
    if (m_StatusView.GetHotRect(pInfo->ItemRect, &hotRect)) {
        m_pApp->ThemeDrawBackground(L"status-bar.item.hot", pInfo->hdc, hotRect);
    }

    LOGFONTW logFont = {};
    if (m_pApp->GetFont(L"StatusBarFont", &logFont)) {
        m_StatusView.Draw(
            pInfo->hdc,
            pInfo->ItemRect,
            logFont,
            m_pApp->GetColor(L"StatusText"),
            m_pApp->GetColor(L"StatusBack"),
            m_pApp->GetColor(L"StatusHighlightText"),
            m_pApp->GetColor(L"StatusHighlightBack"));
    }
    return true;
}

bool CTvtTape::OnStatusItemMouseEvent(TVTest::StatusItemMouseEventInfo *pInfo)
{
    if (!pInfo || pInfo->ID != STATUS_ITEM_TRANSPORT_ROW)
        return false;

    AdjustStatusLayout();

    CStatusView::MOUSE_ACTION action = CStatusView::MOUSE_ACTION_NONE;
    switch (pInfo->Action) {
    case TVTest::STATUS_ITEM_MOUSE_ACTION_LDOWN:
        action = CStatusView::MOUSE_ACTION_LDOWN;
        break;
    case TVTest::STATUS_ITEM_MOUSE_ACTION_LUP:
        action = CStatusView::MOUSE_ACTION_LUP;
        break;
    case TVTest::STATUS_ITEM_MOUSE_ACTION_LDOUBLECLICK:
        action = CStatusView::MOUSE_ACTION_LDOUBLECLICK;
        break;
    case TVTest::STATUS_ITEM_MOUSE_ACTION_RDOWN:
        action = CStatusView::MOUSE_ACTION_RDOWN;
        break;
    case TVTest::STATUS_ITEM_MOUSE_ACTION_MOVE:
        action = CStatusView::MOUSE_ACTION_MOVE;
        break;
    default:
        break;
    }

    if (action == CStatusView::MOUSE_ACTION_NONE)
        return false;

    if (m_StatusView.OnMouseAction(action, pInfo->hwnd, pInfo->CursorPos, pInfo->ItemRect)) {
        RedrawStatusItems();
    }
    return true;
}

bool CTvtTape::OnStatusItemNotify(TVTest::StatusItemEventInfo *pInfo)
{
    if (!pInfo || pInfo->ID != STATUS_ITEM_TRANSPORT_ROW)
        return false;

    switch (pInfo->Event) {
    case TVTest::STATUS_ITEM_EVENT_ENTER:
        if (m_StatusView.OnViewEvent(CStatusView::VIEW_EVENT_ENTER))
            RedrawStatusItems();
        return true;

    case TVTest::STATUS_ITEM_EVENT_LEAVE:
        if (m_StatusView.OnViewEvent(CStatusView::VIEW_EVENT_LEAVE))
            RedrawStatusItems();
        return true;

    case TVTest::STATUS_ITEM_EVENT_CREATED:
    case TVTest::STATUS_ITEM_EVENT_SIZECHANGED:
    case TVTest::STATUS_ITEM_EVENT_STYLECHANGED:
    case TVTest::STATUS_ITEM_EVENT_FONTCHANGED:
        RedrawStatusItems();
        return true;

    case TVTest::STATUS_ITEM_EVENT_UPDATETIMER:
        UpdateStatus();
        return true;
    }

    return false;
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
    m_SelectedDeviceIndex = ::GetPrivateProfileIntW(L"Device", L"SelectedDeviceIndex", -1, m_IniPath.c_str());
    int interval = ::GetPrivateProfileIntW(L"Status", L"PollIntervalMs", 500, m_IniPath.c_str());
    interval = std::clamp(interval, 100, 5000);
    m_PollIntervalMs = static_cast<UINT>(interval);

    const int channel = ::GetPrivateProfileIntW(L"Pipe", L"Channel", 0, m_IniPath.c_str());
    m_PipeControl.SetChannel(channel);

    wchar_t iconPath[MAX_PATH] = {};
    ::GetPrivateProfileStringW(L"UI", L"IconBitmapPath", kDefaultIconBitmap, iconPath, MAX_PATH, m_IniPath.c_str());
    m_ButtonBitmapPath = iconPath;
}

void CTvtTape::SaveSettings() const
{
    wchar_t value[64] = {};

    swprintf_s(value, L"%d", m_SelectedDeviceIndex);
    ::WritePrivateProfileStringW(L"Device", L"SelectedDeviceIndex", value, m_IniPath.c_str());

    swprintf_s(value, L"%u", m_PollIntervalMs);
    ::WritePrivateProfileStringW(L"Status", L"PollIntervalMs", value, m_IniPath.c_str());

    swprintf_s(value, L"%d", m_PipeControl.GetChannel());
    ::WritePrivateProfileStringW(L"Pipe", L"Channel", value, m_IniPath.c_str());

    ::WritePrivateProfileStringW(L"UI", L"IconBitmapPath", m_ButtonBitmapPath.c_str(), m_IniPath.c_str());
}

std::wstring CTvtTape::GetIniPath() const
{
    HMODULE hModule = nullptr;
    wchar_t modulePath[MAX_PATH] = {};
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CreatePluginClass),
            &hModule)) {
        return L"TvtTape.ini";
    }

    const DWORD length = ::GetModuleFileNameW(hModule, modulePath, MAX_PATH);
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

std::wstring CTvtTape::ResolveUiFilePath(const std::wstring &fileName) const
{
    if (fileName.empty())
        return L"";
    if (IsAbsolutePath(fileName))
        return fileName;

    std::wstring basePath = m_IniPath.empty() ? GetIniPath() : m_IniPath;
    const size_t sepPos = basePath.find_last_of(L"\\/");
    if (sepPos == std::wstring::npos)
        return fileName;
    basePath.erase(sepPos + 1);
    return basePath + fileName;
}

void CTvtTape::InitializeStatusView()
{
    if (m_StatusViewInitialized)
        return;

    RECT margin = {4, 3, 4, 3};
    m_StatusView.SetItemMargin(margin);

    new CDeviceStatusItem(&m_StatusView, this, kButtonWidthFallback);
    new CTransportButtonItem(&m_StatusView, this, UI_ITEM_POWER, TRANSPORT_POWER, kButtonWidthFallback);
    new CTransportButtonItem(&m_StatusView, this, UI_ITEM_REW, TRANSPORT_REW, kButtonWidthFallback);
    new CTransportButtonItem(&m_StatusView, this, UI_ITEM_PLAY_PAUSE, TRANSPORT_PLAY_PAUSE, kButtonWidthFallback);
    new CTransportButtonItem(&m_StatusView, this, UI_ITEM_STOP, TRANSPORT_STOP, kButtonWidthFallback);
    new CTransportButtonItem(&m_StatusView, this, UI_ITEM_FF, TRANSPORT_FF, kButtonWidthFallback);
    new CTransportButtonItem(&m_StatusView, this, UI_ITEM_RECORD, TRANSPORT_RECORD, kButtonWidthFallback);
    new CTransportStateItem(&m_StatusView, this, kStateWidth);
    new CTimeCodeStatusItem(&m_StatusView, this, kTimeCodeWidth);

    m_StatusViewInitialized = true;
}

void CTvtTape::LoadButtonBitmap()
{
    m_ButtonIcons.Destroy();
    m_ButtonIconSize = 0;

    HMODULE hModule = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CreatePluginClass),
        &hModule);

    if (hModule)
        m_ButtonIcons.Load(hModule, IDB_TVTAPE_BUTTONS, LR_CREATEDIBSECTION);

    if (!m_ButtonIcons.IsCreated()) {
        const std::wstring path = ResolveUiFilePath(m_ButtonBitmapPath.empty() ? std::wstring(kDefaultIconBitmap) : m_ButtonBitmapPath);
        if (path.empty())
            return;
        if (!m_ButtonIcons.Load(nullptr, path.c_str(), LR_CREATEDIBSECTION | LR_LOADFROMFILE))
            return;
    }

    const int iconHeight = m_ButtonIcons.GetHeight();
    const int iconWidth = m_ButtonIcons.GetWidth();
    if (iconHeight <= 0 || iconWidth < iconHeight * kBitmapIconCount) {
        m_ButtonIcons.Destroy();
        return;
    }

    m_ButtonIconSize = iconHeight;
}

void CTvtTape::AdjustStatusLayout()
{
    CStatusItem *pDevice = m_StatusView.GetItemByID(UI_ITEM_DEVICE);
    CStatusItem *pPower = m_StatusView.GetItemByID(UI_ITEM_POWER);
    CStatusItem *pRew = m_StatusView.GetItemByID(UI_ITEM_REW);
    CStatusItem *pPlayPause = m_StatusView.GetItemByID(UI_ITEM_PLAY_PAUSE);
    CStatusItem *pStop = m_StatusView.GetItemByID(UI_ITEM_STOP);
    CStatusItem *pFf = m_StatusView.GetItemByID(UI_ITEM_FF);
    CStatusItem *pRecord = m_StatusView.GetItemByID(UI_ITEM_RECORD);
    CStatusItem *pState = m_StatusView.GetItemByID(UI_ITEM_STATE);
    CStatusItem *pTimeCode = m_StatusView.GetItemByID(UI_ITEM_TIMECODE);
    if (!pDevice || !pPower || !pRew || !pPlayPause || !pStop || !pFf || !pRecord || !pState || !pTimeCode)
        return;

    const int buttonWidth = GetButtonWidth();

    pDevice->SetWidth(buttonWidth);
    pPower->SetWidth(buttonWidth);
    pRew->SetWidth(buttonWidth);
    pPlayPause->SetWidth(buttonWidth);
    pStop->SetWidth(buttonWidth);
    pFf->SetWidth(buttonWidth);
    pRecord->SetWidth(buttonWidth);
    pState->SetWidth(kStateWidth);
    pTimeCode->SetWidth(kTimeCodeWidth);
}

void CTvtTape::RegisterStatusItems()
{
    TVTest::StatusItemInfo info = {};
    info.Size = sizeof(info);
    info.Flags = TVTest::STATUS_ITEM_FLAG_TIMERUPDATE;
    info.Style = TVTest::STATUS_ITEM_STYLE_FORCEFULLROW;
    info.ID = STATUS_ITEM_TRANSPORT_ROW;
    info.pszIDText = L"TvtTape.TransportRow";
    info.pszName = L"TvtTape ステータスバー";
    info.MinWidth = 0;
    info.MaxWidth = -1;
    info.DefaultWidth = TVTest::StatusItemWidthByFontSize(18);
    info.MinHeight = 0;
    m_pApp->RegisterStatusItem(&info);

    TVTest::StatusItemSetInfo setInfo = {};
    setInfo.Size = sizeof(setInfo);
    setInfo.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
    setInfo.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
    setInfo.State = TVTest::STATUS_ITEM_STATE_VISIBLE;
    setInfo.ID = STATUS_ITEM_TRANSPORT_ROW;
    m_pApp->SetStatusItem(&setInfo);
}

void CTvtTape::SetStatusItemsVisible(bool visible)
{
    TVTest::StatusItemSetInfo setInfo = {};
    setInfo.Size = sizeof(setInfo);
    setInfo.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
    setInfo.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
    setInfo.State = visible ? TVTest::STATUS_ITEM_STATE_VISIBLE : TVTest::STATUS_ITEM_STATE_NONE;
    setInfo.ID = STATUS_ITEM_TRANSPORT_ROW;
    m_pApp->SetStatusItem(&setInfo);
}

void CTvtTape::RedrawStatusItems()
{
    m_pApp->StatusItemNotify(STATUS_ITEM_TRANSPORT_ROW, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
}

void CTvtTape::UpdateStatus()
{
    if (!m_VcrDevice.IsOpen()) {
        m_DeviceNames.clear();
        if (m_VcrDevice.EnumDevices(&m_DeviceNames) && !m_DeviceNames.empty()) {
            m_StateText = L"デバイスが検出されました";
        } else {
            m_StateText = L"接続されていません";
        }

        m_TimeCodeText = L"--:--:--:--";
        RedrawStatusItems();
        return;
    }

    m_VcrDevice.UpdateTransportState();
    m_StateText = m_VcrDevice.GetTransportStateText();

    long hour = 0;
    long minute = 0;
    long second = 0;
    long frame = 0;
    if (m_VcrDevice.GetTimeCode(&hour, &minute, &second, &frame)) {
        wchar_t timeCode[32] = {};
        swprintf_s(timeCode, L"%02ld:%02ld:%02ld:%02ld", hour, minute, second, frame);
        m_TimeCodeText = timeCode;
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

    const ULONGLONG now = ::GetTickCount64();
    if (m_ZeroBitrateStartTick == 0) {
        m_ZeroBitrateStartTick = now;
        return;
    }

    if (!m_RecordStopTriggered && (now - m_ZeroBitrateStartTick) >= 5000ULL) {
        m_VcrDevice.Stop();
        m_PipeControl.Purge();
        if (m_pApp->StopRecord()) {
            m_pApp->AddLog(L"TvtTape: 録画停止 (ビットレート0Mbpsが5秒経過)", TVTest::LOG_TYPE_WARNING);
        } else {
            m_pApp->AddLog(L"TvtTape: 録画停止に失敗しました (ビットレート0Mbpsが5秒経過時)", TVTest::LOG_TYPE_WARNING);
        }
        m_RecordStopTriggered = true;
    }
}

bool CTvtTape::ShowDeviceMenuAt(const POINT &pt, UINT flags, HWND hwnd)
{
    if (hwnd == nullptr)
        return false;

    m_DeviceNames.clear();
    m_VcrDevice.EnumDevices(&m_DeviceNames);

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu)
        return false;

    ::AppendMenuW(hMenu, MF_STRING | (m_SelectedDeviceIndex < 0 ? MF_CHECKED : 0), MENU_DEVICE_AUTO, L"Auto select");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    for (size_t i = 0; i < m_DeviceNames.size(); ++i) {
        UINT menuFlags = MF_STRING;
        if (m_SelectedDeviceIndex == static_cast<int>(i))
            menuFlags |= MF_CHECKED;
        ::AppendMenuW(hMenu, menuFlags, MENU_DEVICE_FIRST + static_cast<UINT>(i), m_DeviceNames[i].c_str());
    }

    const UINT command = ::TrackPopupMenu(hMenu, TPM_NONOTIFY | TPM_RETURNCMD | flags, pt.x, pt.y, 0, hwnd, nullptr);
    ::DestroyMenu(hMenu);

    int selected = m_SelectedDeviceIndex;
    if (command == MENU_DEVICE_AUTO) {
        selected = -1;
    } else if (command >= MENU_DEVICE_FIRST) {
        selected = static_cast<int>(command - MENU_DEVICE_FIRST);
    }

    if (selected == m_SelectedDeviceIndex)
        return true;

    m_SelectedDeviceIndex = selected;
    SaveSettings();
    if (!ReopenDevice()) {
        m_pApp->AddLog(L"TvtTape: デバイスのオープンに失敗しました", TVTest::LOG_TYPE_WARNING);
        return false;
    }

    UpdateStatus();
    return true;
}

void CTvtTape::ExecuteTransportAction(int action)
{
    switch (action) {
    case TRANSPORT_POWER:
        if (!m_VcrDevice.IsOpen()) {
            m_VcrDevice.SetPreferredDeviceIndex(m_SelectedDeviceIndex);
            if (!m_VcrDevice.Open()) {
                m_pApp->AddLog(L"TvtTape: デバイスのオープンに失敗しました（電源制御用）", TVTest::LOG_TYPE_WARNING);
                break;
            }
        }

        if (m_VcrDevice.IsDevicePowerOn()) {
            TVTest::RecordStatusInfo recordStatus = {};
            if (m_pApp->GetRecordStatus(&recordStatus) && recordStatus.Status == TVTest::RECORD_STATUS_RECORDING) {
                if (!m_pApp->StopRecord()) {
                    m_pApp->AddLog(L"TvtTape: 電源オフ前の録画停止に失敗しました", TVTest::LOG_TYPE_WARNING);
                }
            }
            m_VcrDevice.Stop();
            m_PipeControl.Purge();
            if (!m_VcrDevice.SetDevicePower(false)) {
                m_pApp->AddLog(L"TvtTape: デバイスの電源オフに失敗しました", TVTest::LOG_TYPE_WARNING);
            }
        } else {
            if (!m_VcrDevice.SetDevicePower(true)) {
                m_pApp->AddLog(L"TvtTape: デバイスの電源オンに失敗しました", TVTest::LOG_TYPE_WARNING);
            }
        }
        break;

    case TRANSPORT_REW:
        if (m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::Rewind)
            m_VcrDevice.Play();
        else
            m_VcrDevice.Rewind();
        break;

    case TRANSPORT_PLAY_PAUSE:
        if (m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::Play) {
            m_VcrDevice.Pause();
            m_PipeControl.SetPaused(true);
        } else {
            m_VcrDevice.Play();
            m_PipeControl.SetPaused(false);
        }
        break;

    case TRANSPORT_STOP:
        m_VcrDevice.Stop();
        m_PipeControl.Purge();
        break;

    case TRANSPORT_FF:
        if (m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::FastForward)
            m_VcrDevice.Play();
        else
            m_VcrDevice.FastForward();
        break;

    case TRANSPORT_RECORD:
        {
            TVTest::RecordStatusInfo recordStatus = {};
            if (!m_pApp->GetRecordStatus(&recordStatus))
                break;

            if (recordStatus.Status == TVTest::RECORD_STATUS_RECORDING) {
                m_VcrDevice.Stop();
                m_PipeControl.Purge();
                if (!m_pApp->StopRecord()) {
                    m_pApp->AddLog(L"TvtTape: 録画停止に失敗しました", TVTest::LOG_TYPE_WARNING);
                }
            } else {
                if (!m_VcrDevice.IsOpen()) {
                    m_VcrDevice.SetPreferredDeviceIndex(m_SelectedDeviceIndex);
                    if (!m_VcrDevice.Open()) {
                        m_pApp->AddLog(L"TvtTape: 録画用の VCR デバイスのオープンに失敗しました", TVTest::LOG_TYPE_WARNING);
                        break;
                    }
                }
                if (!m_VcrDevice.IsDevicePowerOn() && !m_VcrDevice.SetDevicePower(true)) {
                    m_pApp->AddLog(L"TvtTape: 録画用の VCR デバイスの電源オンに失敗しました", TVTest::LOG_TYPE_WARNING);
                    break;
                }
                m_VcrDevice.Play();
                m_PipeControl.SetPaused(false);
                if (!m_pApp->StartRecord()) {
                    m_pApp->AddLog(L"TvtTape: 録画開始に失敗しました", TVTest::LOG_TYPE_WARNING);
                }
            }
        }
        break;
    }

    UpdateStatus();
}

bool CTvtTape::IsRecording() const
{
    TVTest::RecordStatusInfo recordStatus = {};
    return m_pApp->GetRecordStatus(&recordStatus) && recordStatus.Status == TVTest::RECORD_STATUS_RECORDING;
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

bool CTvtTape::DrawTransportIcon(HDC hdc, const RECT &rect, int iconIndex, COLORREF color) const
{
    if (!m_ButtonIcons.IsCreated() || m_ButtonIconSize <= 0 || iconIndex < 0 || iconIndex >= kBitmapIconCount)
        return false;

    return DrawUtil::DrawMonoColorDIB(
        hdc,
        rect.left,
        rect.top + ((rect.bottom - rect.top) - m_ButtonIconSize) / 2,
        m_ButtonIcons.GetHandle(),
        iconIndex * m_ButtonIconSize,
        0,
        m_ButtonIconSize,
        m_ButtonIconSize,
        color);
}

int CTvtTape::GetButtonWidth() const
{
    return m_ButtonIconSize > 0 ? m_ButtonIconSize + 2 : kButtonWidthFallback;
}

TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTvtTape();
}
