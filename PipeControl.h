#pragma once

#include <Windows.h>
#include <string>

class CPipeControl
{
public:
    CPipeControl();
    ~CPipeControl();

    void SetChannel(int channelIndex);
    int GetChannel() const;

    bool SetPaused(bool paused);
    bool Purge();
    bool QueryReadyState(std::wstring *pState);
    bool SendTsData(const BYTE *pData, size_t dataSize);
    bool IsDataPipeConnected() const;

private:
    bool EnsureDataPipe();
    bool EnsureControlPipe();
    void CloseDataPipe();
    void CloseControlPipe();
    bool SendCommand(const std::wstring &command, std::wstring *pResponse);
    std::wstring GetDataPipeName() const;
    std::wstring GetControlPipeName() const;

    int m_ChannelIndex;
    HANDLE m_hDataPipe;
    HANDLE m_hCtrlPipe;
};
