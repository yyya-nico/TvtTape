#include "VcrDevice.h"

#include <algorithm>

DEFINE_GUID(CLSID_SampleGrabber, 0xC1F400A0, 0x3F08, 0x11d3, 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37);
DEFINE_GUID(IID_ISampleGrabber, 0x6B652FFF, 0x11FE, 0x4fce, 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F);
DEFINE_GUID(IID_ISampleGrabberCB, 0x0579154A, 0x2B53, 0x4994, 0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85);

interface __declspec(uuid("0579154A-2B53-4994-B0D0-E773148EFF85"))
ISampleGrabberCB : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double SampleTime, IMediaSample *pSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen) = 0;
};

interface ISampleGrabber : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *pBufferSize, long *pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB *pCallback, long WhichMethodToCallback) = 0;
};

#pragma comment(lib, "strmiids.lib")

namespace {

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

class CSampleGrabberCallback : public ISampleGrabberCB
{
public:
    explicit CSampleGrabberCallback(CVcrDevice *pOwner)
        : m_RefCount(1)
        , m_pOwner(pOwner)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override
    {
        if (!ppvObject)
            return E_POINTER;
        *ppvObject = nullptr;

        if (riid == IID_IUnknown || riid == IID_ISampleGrabberCB) {
            *ppvObject = static_cast<ISampleGrabberCB *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_RefCount);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG count = InterlockedDecrement(&m_RefCount);
        if (count == 0)
            delete this;
        return count;
    }

    STDMETHODIMP SampleCB(double, IMediaSample *) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP BufferCB(double, BYTE *pBuffer, long BufferLen) override
    {
        if (m_pOwner)
            m_pOwner->HandleTsBuffer(pBuffer, BufferLen);
        return S_OK;
    }

private:
    LONG m_RefCount;
    CVcrDevice *m_pOwner;
};

} // namespace

CVcrDevice::CVcrDevice()
    : m_pGraphBuilder(nullptr)
    , m_pCaptureGraphBuilder(nullptr)
    , m_pMediaControl(nullptr)
    , m_pSourceFilter(nullptr)
    , m_pGrabberFilter(nullptr)
    , m_TransportState(TransportState::Unknown)
    , m_PreferredDeviceIndex(-1)
    , m_ActiveDeviceIndex(-1)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

CVcrDevice::~CVcrDevice()
{
    Close();
    CoUninitialize();
}

bool CVcrDevice::EnumDevices(std::vector<std::wstring> *pNames)
{
    if (!pNames)
        return false;

    pNames->clear();

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
                    std::wstring name = GetFriendlyName(pMoniker);
                    if (name.empty())
                        name = L"VCR device";
                    pNames->push_back(name);
                }
                pFilter->Release();
            }
            pMoniker->Release();
        }
        pEnum->Release();
    }

    pDevEnum->Release();
    return !pNames->empty();
}

void CVcrDevice::SetPreferredDeviceIndex(int index)
{
    m_PreferredDeviceIndex = index;
}

int CVcrDevice::GetPreferredDeviceIndex() const
{
    return m_PreferredDeviceIndex;
}

int CVcrDevice::GetActiveDeviceIndex() const
{
    return m_ActiveDeviceIndex;
}

std::wstring CVcrDevice::GetActiveDeviceName() const
{
    return m_ActiveDeviceName;
}

void CVcrDevice::SetTsDataCallback(std::function<void(const BYTE *, size_t)> callback)
{
    m_TsDataCallback = std::move(callback);
}

bool CVcrDevice::Open()
{
    if (IsOpen())
        return true;
    return BuildGraph();
}

void CVcrDevice::Close()
{
    Stop();
    DestroyGraph();
    m_TransportState = TransportState::Unknown;
    m_ActiveDeviceIndex = -1;
    m_ActiveDeviceName.clear();
}

bool CVcrDevice::IsOpen() const
{
    return m_pGraphBuilder != nullptr;
}

bool CVcrDevice::Play()
{
    return SetTransportMode(ED_MODE_PLAY, TransportState::Play);
}

bool CVcrDevice::Stop()
{
    return SetTransportMode(ED_MODE_STOP, TransportState::Stop);
}

bool CVcrDevice::Pause()
{
    return SetTransportMode(ED_MODE_FREEZE, TransportState::Pause);
}

bool CVcrDevice::Rewind()
{
    return SetTransportMode(ED_MODE_REW, TransportState::Rewind);
}

bool CVcrDevice::FastForward()
{
    return SetTransportMode(ED_MODE_FF, TransportState::FastForward);
}

bool CVcrDevice::UpdateTransportState()
{
    if (!m_pSourceFilter)
        return false;

    IAMExtTransport *pTransport = nullptr;
    if (FAILED(m_pSourceFilter->QueryInterface(IID_IAMExtTransport, reinterpret_cast<void **>(&pTransport))) || !pTransport)
        return false;

    long mode = 0;
    const HRESULT hr = pTransport->get_Mode(&mode);
    pTransport->Release();
    if (FAILED(hr))
        return false;

    switch (mode) {
    case ED_MODE_STOP:
        m_TransportState = TransportState::Stop;
        break;
    case ED_MODE_PLAY:
        m_TransportState = TransportState::Play;
        break;
    case ED_MODE_FREEZE:
        m_TransportState = TransportState::Pause;
        break;
    case ED_MODE_REW:
        m_TransportState = TransportState::Rewind;
        break;
    case ED_MODE_FF:
        m_TransportState = TransportState::FastForward;
        break;
    case ED_MODE_RECORD:
        m_TransportState = TransportState::Record;
        break;
    default:
        m_TransportState = TransportState::Unknown;
        break;
    }

    return true;
}

CVcrDevice::TransportState CVcrDevice::GetTransportState() const
{
    return m_TransportState;
}

const wchar_t *CVcrDevice::GetTransportStateText() const
{
    switch (m_TransportState) {
    case TransportState::Stop:
        return L"STOP";
    case TransportState::Play:
        return L"PLAY";
    case TransportState::Pause:
        return L"PAUSE";
    case TransportState::Rewind:
        return L"REW";
    case TransportState::FastForward:
        return L"FF";
    case TransportState::Record:
        return L"REC";
    default:
        return L"UNKNOWN";
    }
}

bool CVcrDevice::GetTimeCode(long *pHour, long *pMinute, long *pSecond, long *pFrame)
{
    if (!m_pSourceFilter || !pHour || !pMinute || !pSecond || !pFrame)
        return false;

    IAMTimecodeReader *pReader = nullptr;
    if (FAILED(m_pSourceFilter->QueryInterface(IID_IAMTimecodeReader, reinterpret_cast<void **>(&pReader))) || !pReader)
        return false;

    TIMECODE_SAMPLE sample = {};
    const HRESULT hr = pReader->GetTimecode(&sample);
    pReader->Release();
    if (FAILED(hr))
        return false;

    const long frames = sample.timecode.dwFrames;
    *pHour = frames / (30 * 60 * 60);
    *pMinute = (frames / (30 * 60)) % 60;
    *pSecond = (frames / 30) % 60;
    *pFrame = frames % 30;
    return true;
}

bool CVcrDevice::BuildGraph()
{
    if (FAILED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, reinterpret_cast<void **>(&m_pGraphBuilder))))
        return false;

    if (FAILED(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, reinterpret_cast<void **>(&m_pCaptureGraphBuilder)))) {
        DestroyGraph();
        return false;
    }

    if (FAILED(m_pCaptureGraphBuilder->SetFiltergraph(m_pGraphBuilder))) {
        DestroyGraph();
        return false;
    }

    if (!SelectSourceFilter()) {
        DestroyGraph();
        return false;
    }

    if (FAILED(m_pGraphBuilder->AddFilter(m_pSourceFilter, L"VCR Source"))) {
        DestroyGraph();
        return false;
    }

    if (FAILED(CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, reinterpret_cast<void **>(&m_pGrabberFilter)))) {
        DestroyGraph();
        return false;
    }

    if (FAILED(m_pGraphBuilder->AddFilter(m_pGrabberFilter, L"Sample Grabber"))) {
        DestroyGraph();
        return false;
    }

    ISampleGrabber *pGrabber = nullptr;
    if (FAILED(m_pGrabberFilter->QueryInterface(IID_ISampleGrabber, reinterpret_cast<void **>(&pGrabber))) || !pGrabber) {
        DestroyGraph();
        return false;
    }

    AM_MEDIA_TYPE mediaType = {};
    mediaType.majortype = MEDIATYPE_Stream;
    mediaType.subtype = MEDIASUBTYPE_MPEG2_TRANSPORT;
    pGrabber->SetMediaType(&mediaType);
    pGrabber->SetBufferSamples(FALSE);
    pGrabber->SetOneShot(FALSE);

    CSampleGrabberCallback *pCallback = new CSampleGrabberCallback(this);
    pGrabber->SetCallback(pCallback, 1);
    pCallback->Release();
    pGrabber->Release();

    HRESULT hr = m_pCaptureGraphBuilder->RenderStream(
        &PIN_CATEGORY_CAPTURE,
        &MEDIATYPE_Stream,
        m_pSourceFilter,
        nullptr,
        m_pGrabberFilter);
    if (FAILED(hr)) {
        hr = m_pCaptureGraphBuilder->RenderStream(
            nullptr,
            &MEDIATYPE_Stream,
            m_pSourceFilter,
            nullptr,
            m_pGrabberFilter);
    }
    if (FAILED(hr)) {
        DestroyGraph();
        return false;
    }

    if (FAILED(m_pGraphBuilder->QueryInterface(IID_IMediaControl, reinterpret_cast<void **>(&m_pMediaControl)))) {
        DestroyGraph();
        return false;
    }

    m_pMediaControl->Run();
    m_TransportState = TransportState::Stop;
    return true;
}

void CVcrDevice::DestroyGraph()
{
    if (m_pMediaControl) {
        m_pMediaControl->Stop();
        m_pMediaControl->Release();
        m_pMediaControl = nullptr;
    }

    if (m_pGrabberFilter) {
        if (m_pGraphBuilder)
            m_pGraphBuilder->RemoveFilter(m_pGrabberFilter);
        m_pGrabberFilter->Release();
        m_pGrabberFilter = nullptr;
    }

    if (m_pSourceFilter) {
        if (m_pGraphBuilder)
            m_pGraphBuilder->RemoveFilter(m_pSourceFilter);
        m_pSourceFilter->Release();
        m_pSourceFilter = nullptr;
    }

    if (m_pCaptureGraphBuilder) {
        m_pCaptureGraphBuilder->Release();
        m_pCaptureGraphBuilder = nullptr;
    }

    if (m_pGraphBuilder) {
        m_pGraphBuilder->Release();
        m_pGraphBuilder = nullptr;
    }
}

bool CVcrDevice::SelectSourceFilter()
{
    ICreateDevEnum *pDevEnum = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, reinterpret_cast<void **>(&pDevEnum))))
        return false;

    const CLSID categories[] = {
        CLSID_VideoInputDeviceCategory,
        CLSID_LegacyAmFilterCategory,
    };

    std::vector<IMoniker*> candidates;
    std::vector<std::wstring> names;

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
                    pMoniker->AddRef();
                    candidates.push_back(pMoniker);
                    std::wstring name = GetFriendlyName(pMoniker);
                    if (name.empty())
                        name = L"VCR device";
                    names.push_back(name);
                }
                pFilter->Release();
            }
            pMoniker->Release();
        }
        pEnum->Release();
    }

    pDevEnum->Release();

    if (candidates.empty())
        return false;

    int selected = 0;
    if (m_PreferredDeviceIndex >= 0 && m_PreferredDeviceIndex < static_cast<int>(candidates.size()))
        selected = m_PreferredDeviceIndex;

    IBaseFilter *pSource = nullptr;
    const HRESULT hr = candidates[selected]->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void **>(&pSource));

    for (IMoniker *pMoniker : candidates)
        pMoniker->Release();

    if (FAILED(hr) || !pSource)
        return false;

    m_pSourceFilter = pSource;
    m_ActiveDeviceIndex = selected;
    m_ActiveDeviceName = names[selected];
    return true;
}

bool CVcrDevice::SetTransportMode(long mode, TransportState stateIfSuccess)
{
    if (!m_pSourceFilter)
        return false;

    IAMExtTransport *pTransport = nullptr;
    if (FAILED(m_pSourceFilter->QueryInterface(IID_IAMExtTransport, reinterpret_cast<void **>(&pTransport))) || !pTransport)
        return false;

    const HRESULT hr = pTransport->put_Mode(mode);
    pTransport->Release();
    if (FAILED(hr))
        return false;

    m_TransportState = stateIfSuccess;
    return true;
}

void CVcrDevice::HandleTsBuffer(const BYTE *pBuffer, long bufferLen)
{
    if (!pBuffer || bufferLen <= 0)
        return;

    if (m_TsDataCallback)
        m_TsDataCallback(pBuffer, static_cast<size_t>(bufferLen));
}
