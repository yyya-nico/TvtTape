#include "PipeControl.h"

#include <Windows.h>

CPipeControl::CPipeControl()
    : m_ChannelIndex(0)
    , m_hDataPipe(INVALID_HANDLE_VALUE)
    , m_hCtrlPipe(INVALID_HANDLE_VALUE)
{
}

CPipeControl::~CPipeControl()
{
    CloseControlPipe();
    CloseDataPipe();
}

void CPipeControl::SetChannel(int channelIndex)
{
    if (channelIndex < 0)
        channelIndex = 0;
    if (channelIndex > 9)
        channelIndex = 9;
    if (m_ChannelIndex != channelIndex) {
        m_ChannelIndex = channelIndex;
        CloseControlPipe();
        CloseDataPipe();
    }
}

int CPipeControl::GetChannel() const
{
    return m_ChannelIndex;
}

bool CPipeControl::SetPaused(bool paused)
{
    return SendCommand(paused ? L"PAUSE 1" : L"PAUSE 0", nullptr);
}

bool CPipeControl::Purge()
{
    return SendCommand(L"PURGE", nullptr);
}

bool CPipeControl::QueryReadyState(std::wstring *pState)
{
    return SendCommand(L"GET_READY_STATE", pState);
}

bool CPipeControl::SendTsData(const BYTE *pData, size_t dataSize)
{
    if (!pData || dataSize == 0)
        return false;

    if (!EnsureDataPipe())
        return false;

    DWORD written = 0;
    if (!WriteFile(m_hDataPipe, pData, static_cast<DWORD>(dataSize), &written, nullptr)) {
        CloseDataPipe();
        return false;
    }

    return written == static_cast<DWORD>(dataSize);
}

bool CPipeControl::IsDataPipeConnected() const
{
    return m_hDataPipe != INVALID_HANDLE_VALUE;
}

bool CPipeControl::EnsureDataPipe()
{
    if (m_hDataPipe != INVALID_HANDLE_VALUE)
        return true;

    const std::wstring pipeName = GetDataPipeName();
    if (!WaitNamedPipeW(pipeName.c_str(), NMPWAIT_USE_DEFAULT_WAIT))
        return false;

    m_hDataPipe = CreateFileW(
        pipeName.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    return m_hDataPipe != INVALID_HANDLE_VALUE;
}

bool CPipeControl::EnsureControlPipe()
{
    if (m_hCtrlPipe != INVALID_HANDLE_VALUE)
        return true;

    const std::wstring pipeName = GetControlPipeName();
    if (!WaitNamedPipeW(pipeName.c_str(), NMPWAIT_USE_DEFAULT_WAIT))
        return false;

    m_hCtrlPipe = CreateFileW(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (m_hCtrlPipe == INVALID_HANDLE_VALUE)
        return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(m_hCtrlPipe, &mode, nullptr, nullptr)) {
        CloseControlPipe();
        return false;
    }

    return true;
}

void CPipeControl::CloseDataPipe()
{
    if (m_hDataPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDataPipe);
        m_hDataPipe = INVALID_HANDLE_VALUE;
    }
}

void CPipeControl::CloseControlPipe()
{
    if (m_hCtrlPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hCtrlPipe);
        m_hCtrlPipe = INVALID_HANDLE_VALUE;
    }
}

bool CPipeControl::SendCommand(const std::wstring &command, std::wstring *pResponse)
{
    if (!EnsureControlPipe())
        return false;

    const DWORD requestBytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    wchar_t replyBuffer[256] = {};
    DWORD bytesRead = 0;
    BOOL ok = TransactNamedPipe(
        m_hCtrlPipe,
        const_cast<wchar_t *>(command.c_str()),
        requestBytes,
        replyBuffer,
        sizeof(replyBuffer),
        &bytesRead,
        nullptr);
    if (!ok) {
        CloseControlPipe();
        return false;
    }

    if (pResponse) {
        if (bytesRead >= sizeof(wchar_t)) {
            replyBuffer[(bytesRead / sizeof(wchar_t)) - 1] = L'\0';
            if (replyBuffer[0] == L'A' && replyBuffer[1] == L' ')
                *pResponse = replyBuffer + 2;
            else
                *pResponse = replyBuffer;
        } else {
            pResponse->clear();
        }
    }
    return true;
}

std::wstring CPipeControl::GetDataPipeName() const
{
    wchar_t name[64] = {};
    swprintf_s(name, L"\\\\.\\pipe\\BonDriver_Pipe%02d", m_ChannelIndex);
    return name;
}

std::wstring CPipeControl::GetControlPipeName() const
{
    wchar_t name[64] = {};
    swprintf_s(name, L"\\\\.\\pipe\\BonDriver_Pipe%02dCtrl", m_ChannelIndex);
    return name;
}
