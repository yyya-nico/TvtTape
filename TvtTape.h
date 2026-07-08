#pragma once

#include <Windows.h>
#include <string>
#include <vector>

#include "DrawUtil.h"
#include "StatusView.h"

#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,14)
#include "TVTestPlugin.h"

#include "PipeControl.h"
#include "VcrDevice.h"

class CTvtTape : public TVTest::CTVTestPlugin, public TVTest::CTVTestEventHandler
{
public:
    CTvtTape();
    ~CTvtTape() override;

    bool GetPluginInfo(TVTest::PluginInfo *pInfo) override;
    bool Initialize() override;
    bool Finalize() override;
    bool OnPluginEnable(bool fEnable) override;
    bool OnStatusItemDraw(TVTest::StatusItemDrawInfo *pInfo) override;
    bool OnStatusItemMouseEvent(TVTest::StatusItemMouseEventInfo *pInfo) override;
    bool OnStatusItemNotify(TVTest::StatusItemEventInfo *pInfo) override;

    bool ShowDeviceMenuAt(const POINT &pt, UINT flags, HWND hwnd);
    void ExecuteTransportAction(int action);
    const std::wstring &GetStateText() const { return m_StateText; }
    const std::wstring &GetDeviceText() const { return m_DeviceText; }
    const std::wstring &GetTimeCodeText() const { return m_TimeCodeText; }
    bool IsPowered() const { return m_VcrDevice.IsDevicePowerOn(); }
    bool IsPlaying() const { return m_VcrDevice.GetTransportState() == CVcrDevice::TransportState::Play; }
    bool IsRecording() const;
    bool DrawTransportIcon(HDC hdc, const RECT &rect, int iconIndex) const;

private:
    enum {
        STATUS_ITEM_TRANSPORT_ROW = 1,
    };

    enum {
        MENU_DEVICE_AUTO = 1,
        MENU_DEVICE_FIRST = 100,
    };

    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);
    static void CALLBACK TimerProc(HWND, UINT, UINT_PTR idEvent, DWORD);

    void LoadSettings();
    void SaveSettings() const;
    std::wstring GetIniPath() const;
    std::wstring ResolveUiFilePath(const std::wstring &fileName) const;
    void InitializeStatusView();
    void LoadButtonBitmap();
    void AdjustStatusLayout();

    void RegisterStatusItems();
    void SetStatusItemsVisible(bool visible);
    void RedrawStatusItems();
    void UpdateStatus();
    void MonitorRecordingBitrate();
    bool ReopenDevice();
    int GetButtonWidth() const;

    CVcrDevice m_VcrDevice;
    CPipeControl m_PipeControl;
    CStatusView m_StatusView;
    DrawUtil::CBitmap m_ButtonIcons;
    UINT_PTR m_TimerID;
    UINT m_PollIntervalMs;
    int m_ButtonIconSize;
    bool m_StatusViewInitialized;

    int m_SelectedDeviceIndex;
    std::wstring m_IniPath;
    std::vector<std::wstring> m_DeviceNames;

    std::wstring m_StateText;
    std::wstring m_DeviceText;
    std::wstring m_TimeCodeText;
    std::wstring m_ButtonBitmapPath;

    ULONGLONG m_ZeroBitrateStartTick;
    bool m_RecordStopTriggered;
};
