#pragma once

#include <Windows.h>
#include <string>
#include <vector>

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

private:
    enum {
        STATUS_ITEM_STATE = 1,
        STATUS_ITEM_TIMECODE,
        STATUS_ITEM_BUTTONS,
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

    void RegisterStatusItems();
    void SetStatusItemsVisible(bool visible);
    void RedrawStatusItems();
    void UpdateStatus();
    void MonitorRecordingBitrate();
    bool ReopenDevice();
    bool ShowDeviceMenu(const TVTest::StatusItemMouseEventInfo *pInfo);

    CVcrDevice m_VcrDevice;
    CPipeControl m_PipeControl;
    UINT_PTR m_TimerID;
    UINT m_PollIntervalMs;

    int m_SelectedDeviceIndex;
    std::wstring m_IniPath;
    std::vector<std::wstring> m_DeviceNames;

    std::wstring m_StateText;
    std::wstring m_TimeCodeText;

    ULONGLONG m_ZeroBitrateStartTick;
    bool m_RecordStopTriggered;
};
