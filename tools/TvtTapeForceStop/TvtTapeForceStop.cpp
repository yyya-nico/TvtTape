#include <Windows.h>
#include <dshow.h>
#include <xprtdefs.h>

#include <algorithm>
#include <cwctype>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "strmiids.lib")

namespace {

struct DeviceInfo {
    std::wstring name;
    IMoniker *pMoniker = nullptr;
};

std::wstring ToLower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

bool ContainsCaseInsensitive(const std::wstring &text, const std::wstring &needle)
{
    if (needle.empty())
        return true;
    return ToLower(text).find(ToLower(needle)) != std::wstring::npos;
}

std::wstring GetFriendlyName(IMoniker *pMoniker)
{
    if (!pMoniker)
        return L"";

    std::wstring name;
    IPropertyBag *pBag = nullptr;
    if (SUCCEEDED(pMoniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void **>(&pBag))) && pBag) {
        VARIANT value;
        VariantInit(&value);
        if (SUCCEEDED(pBag->Read(L"FriendlyName", &value, nullptr)) && value.vt == VT_BSTR && value.bstrVal) {
            name.assign(value.bstrVal);
        }
        VariantClear(&value);
        pBag->Release();
    }

    return name;
}

bool EnumerateDevices(std::vector<DeviceInfo> &devices)
{
    ICreateDevEnum *pDevEnum = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, reinterpret_cast<void **>(&pDevEnum))))
        return false;

    const CLSID categories[] = {
        CLSID_VideoInputDeviceCategory,
        CLSID_LegacyAmFilterCategory,
    };

    for (const CLSID &category : categories) {
        IEnumMoniker *pEnum = nullptr;
        if (pDevEnum->CreateClassEnumerator(category, &pEnum, 0) != S_OK || !pEnum)
            continue;

        IMoniker *pMoniker = nullptr;
        while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
            IBaseFilter *pFilter = nullptr;
            if (SUCCEEDED(pMoniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void **>(&pFilter))) && pFilter) {
                IAMExtTransport *pTransport = nullptr;
                if (SUCCEEDED(pFilter->QueryInterface(IID_IAMExtTransport, reinterpret_cast<void **>(&pTransport))) && pTransport) {
                    pTransport->Release();

                    DeviceInfo info;
                    info.name = GetFriendlyName(pMoniker);
                    if (info.name.empty())
                        info.name = L"VCR device";
                    pMoniker->AddRef();
                    info.pMoniker = pMoniker;
                    devices.push_back(info);
                }
                pFilter->Release();
            }
            pMoniker->Release();
        }

        pEnum->Release();
    }

    pDevEnum->Release();
    return true;
}

void ReleaseDevices(std::vector<DeviceInfo> &devices)
{
    for (DeviceInfo &device : devices) {
        if (device.pMoniker) {
            device.pMoniker->Release();
            device.pMoniker = nullptr;
        }
    }
}

bool StopDevice(IMoniker *pMoniker, bool powerOff, std::wstring *pError)
{
    if (!pMoniker) {
        if (pError)
            *pError = L"invalid moniker";
        return false;
    }

    IBaseFilter *pFilter = nullptr;
    if (FAILED(pMoniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void **>(&pFilter))) || !pFilter) {
        if (pError)
            *pError = L"failed to bind the filter";
        return false;
    }

    bool ok = false;
    std::wstring error;

    IAMExtTransport *pTransport = nullptr;
    if (SUCCEEDED(pFilter->QueryInterface(IID_IAMExtTransport, reinterpret_cast<void **>(&pTransport))) && pTransport) {
        const HRESULT hr = pTransport->put_Mode(ED_MODE_STOP);
        if (SUCCEEDED(hr)) {
            ok = true;
        } else if (pError && error.empty()) {
            error = L"failed to send stop";
        }
        pTransport->Release();
    } else if (pError) {
        error = L"IAMExtTransport is unavailable";
    }

    if (powerOff) {
        IAMExtDevice *pDevice = nullptr;
        if (SUCCEEDED(pFilter->QueryInterface(IID_IAMExtDevice, reinterpret_cast<void **>(&pDevice))) && pDevice) {
            const HRESULT hr = pDevice->put_DevicePower(ED_POWER_OFF);
            if (SUCCEEDED(hr)) {
                ok = true;
            } else if (pError && error.empty()) {
                error = L"failed to send power off";
            }
            pDevice->Release();
        }
    }

    pFilter->Release();

    if (!ok && pError && error.empty())
        error = L"failed to send a stop command";

    if (pError)
        *pError = std::move(error);

    return ok;
}

void PrintUsage()
{
    std::wcout << L"TvtTapeForceStop\n"
               << L"  --list                list compatible devices\n"
               << L"  --all                 send stop to all compatible devices\n"
               << L"  --index N             stop only the Nth device\n"
               << L"  --name TEXT           stop devices whose name contains TEXT\n"
               << L"  --with-power-off      send stop and then power off\n"
               << L"  --help                show this help\n";
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    bool listOnly = false;
    bool powerOff = false;
    bool stopAll = true;
    bool hasIndex = false;
    int selectedIndex = -1;
    std::wstring nameFilter;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            PrintUsage();
            return 0;
        }
        if (arg == L"--list") {
            listOnly = true;
            stopAll = false;
            continue;
        }
        if (arg == L"--all") {
            stopAll = true;
            continue;
        }
        if (arg == L"--with-power-off") {
            powerOff = true;
            continue;
        }
        if (arg == L"--index" && i + 1 < argc) {
            selectedIndex = _wtoi(argv[++i]);
            hasIndex = true;
            stopAll = false;
            continue;
        }
        if (arg.rfind(L"--index=", 0) == 0) {
            selectedIndex = _wtoi(arg.c_str() + 8);
            hasIndex = true;
            stopAll = false;
            continue;
        }
        if (arg == L"--name" && i + 1 < argc) {
            nameFilter = argv[++i];
            stopAll = false;
            continue;
        }
        if (arg.rfind(L"--name=", 0) == 0) {
            nameFilter = arg.substr(7);
            stopAll = false;
            continue;
        }

        std::wcerr << L"unknown argument: " << arg << L"\n";
        PrintUsage();
        return 2;
    }

    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrInit)) {
        std::wcerr << L"COM initialization failed (HRESULT=0x" << std::hex << static_cast<unsigned long>(hrInit) << L")\n";
        return 3;
    }

    std::vector<DeviceInfo> devices;
    const bool enumerateOk = EnumerateDevices(devices);
    if (!enumerateOk) {
        std::wcerr << L"failed to enumerate compatible VCR devices\n";
        CoUninitialize();
        return 4;
    }

    if (devices.empty()) {
        std::wcout << L"no compatible devices found\n";
        CoUninitialize();
        return 0;
    }

    if (listOnly) {
        for (size_t i = 0; i < devices.size(); ++i)
            std::wcout << i << L": " << devices[i].name << L"\n";
        ReleaseDevices(devices);
        CoUninitialize();
        return 0;
    }

    std::vector<size_t> targets;
    if (hasIndex) {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(devices.size())) {
            std::wcerr << L"index out of range\n";
            ReleaseDevices(devices);
            CoUninitialize();
            return 5;
        }
        targets.push_back(static_cast<size_t>(selectedIndex));
    } else if (!nameFilter.empty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (ContainsCaseInsensitive(devices[i].name, nameFilter))
                targets.push_back(i);
        }
        if (targets.empty()) {
            std::wcerr << L"no device matched the name filter\n";
            ReleaseDevices(devices);
            CoUninitialize();
            return 6;
        }
    } else if (stopAll) {
        for (size_t i = 0; i < devices.size(); ++i)
            targets.push_back(i);
    }

    if (targets.empty()) {
        std::wcerr << L"no targets selected\n";
        ReleaseDevices(devices);
        CoUninitialize();
        return 7;
    }

    int failureCount = 0;
    for (size_t index : targets) {
        std::wstring error;
        const bool stopped = StopDevice(devices[index].pMoniker, powerOff, &error);
        std::wcout << L"[" << index << L"] " << devices[index].name << L": "
                   << (stopped ? L"OK" : L"NG")
                   << (powerOff ? L" (Stop + PowerOff)" : L" (Stop)")
                   << L"\n";
        if (!stopped) {
            ++failureCount;
            if (!error.empty())
                std::wcerr << L"  " << error << L"\n";
        }
    }

    ReleaseDevices(devices);
    CoUninitialize();
    return failureCount == 0 ? 0 : 10;
}
