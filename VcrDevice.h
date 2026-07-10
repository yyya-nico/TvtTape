#pragma once

#include <Windows.h>
#include <dshow.h>
#include <functional>
#include <string>
#include <vector>

class CVcrDevice
{
public:
    enum class TransportState {
        Stop,
        Play,
        Pause,
        Rewind,
        FastForward,
        PlayFastestReverse,
        PlayFastestForward,
        Record,
        Unknown,
    };

    CVcrDevice();
    ~CVcrDevice();

    bool EnumDevices(std::vector<std::wstring> *pNames);
    void SetPreferredDeviceIndex(int index);
    int GetPreferredDeviceIndex() const;
    int GetActiveDeviceIndex() const;
    std::wstring GetActiveDeviceName() const;
    void SetTsDataCallback(std::function<void(const BYTE *, size_t)> callback);

    bool Open();
    void Close();
    bool IsOpen() const;

    bool Play();
    bool Stop();
    bool Pause();
    bool Rewind();
    bool FastForward();
    bool PlayFastestForward();
    bool PlayFastestReverse();
    bool SetDevicePower(bool powerOn);
    bool UpdateDevicePowerState();
    bool IsDevicePowerOn() const;
    bool UpdateTransportState();
    TransportState GetTransportState() const;
    const wchar_t *GetTransportStateText() const;

    bool GetTimeCode(long *pHour, long *pMinute, long *pSecond, long *pFrame, bool *isNegative);
    void HandleTsBuffer(const BYTE *pBuffer, long bufferLen);

private:
    bool BuildGraph();
    void DestroyGraph();
    bool SelectSourceFilter();
    bool SetTransportMode(long mode, TransportState stateIfSuccess);

    IGraphBuilder *m_pGraphBuilder;
    ICaptureGraphBuilder2 *m_pCaptureGraphBuilder;
    IMediaControl *m_pMediaControl;
    IBaseFilter *m_pSourceFilter;
    IBaseFilter *m_pGrabberFilter;
    TransportState m_TransportState;
    bool m_DevicePowerOn;
    int m_PreferredDeviceIndex;
    int m_ActiveDeviceIndex;
    std::wstring m_ActiveDeviceName;
    std::function<void(const BYTE *, size_t)> m_TsDataCallback;
};
