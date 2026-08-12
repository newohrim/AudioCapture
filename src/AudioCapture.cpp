#include "AudioCapture.h"
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclientactivationparams.h>
#include <cstring>
#include <propkey.h>
#include <propvarutil.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <algorithm>

#include <iostream>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <comdef.h>


_COM_SMARTPTR_TYPEDEF(IMMDevice, __uuidof(IMMDevice));
_COM_SMARTPTR_TYPEDEF(IMMDeviceEnumerator, __uuidof(IMMDeviceEnumerator));
_COM_SMARTPTR_TYPEDEF(IAudioSessionManager2, __uuidof(IAudioSessionManager2));
_COM_SMARTPTR_TYPEDEF(IAudioSessionEnumerator, __uuidof(IAudioSessionEnumerator));
_COM_SMARTPTR_TYPEDEF(IAudioSessionControl, __uuidof(IAudioSessionControl));
_COM_SMARTPTR_TYPEDEF(IAudioSessionControl2, __uuidof(IAudioSessionControl2));
_COM_SMARTPTR_TYPEDEF(ISimpleAudioVolume, __uuidof(ISimpleAudioVolume));

bool MuteProcess(DWORD targetPid, bool bMute) {
    HRESULT hr = S_OK;

    // 1. Инициализация COM
    //hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    //if (FAILED(hr)) {
    //    std::cerr << "CoInitializeEx failed: " << hr << std::endl;
    //    return false;
    //}

    // 2. Создание перечислителя устройств
    IMMDeviceEnumeratorPtr pEnumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        std::cerr << "CoCreateInstance (MMDeviceEnumerator) failed" << std::endl;
        //CoUninitialize();
        return false;
    }

    // 3. Получение устройства вывода по умолчанию
    IMMDevicePtr pDevice;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) {
        std::cerr << "GetDefaultAudioEndpoint failed" << std::endl;
        //CoUninitialize();
        return false;
    }

    // 4. Активация интерфейса IAudioSessionManager2
    IAudioSessionManager2Ptr pSessionManager;
    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL,
                           (void**)&pSessionManager);
    if (FAILED(hr) || !pSessionManager) {
        std::cerr << "Activate (IAudioSessionManager2) failed" << std::endl;
        //CoUninitialize();
        return false;
    }

    // 5. Получение перечислителя аудиосессий
    IAudioSessionEnumeratorPtr pSessionEnumerator;
    hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
    if (FAILED(hr) || !pSessionEnumerator) {
        std::cerr << "GetSessionEnumerator failed" << std::endl;
        //CoUninitialize();
        return false;
    }

    // 6. Перебор всех сессий
    int sessionCount = 0;
    hr = pSessionEnumerator->GetCount(&sessionCount);
    if (FAILED(hr)) {
        std::cerr << "GetCount failed" << std::endl;
        //CoUninitialize();
        return false;
    }

    for (int i = 0; i < sessionCount; ++i) {
        IAudioSessionControlPtr pSessionControl;
        hr = pSessionEnumerator->GetSession(i, &pSessionControl);
        if (FAILED(hr) || !pSessionControl) {
            continue;
        }

        // Получаем IAudioSessionControl2, чтобы узнать PID процесса
        IAudioSessionControl2Ptr pSessionControl2 = pSessionControl;
        if (!pSessionControl2) {
            continue;
        }

        DWORD pid = 0;
        hr = pSessionControl2->GetProcessId(&pid);
        if (FAILED(hr)) {
            continue;
        }

        // Если PID совпадает с искомым
        if (pid == targetPid) {
            // Получаем интерфейс для управления громкостью
            ISimpleAudioVolumePtr pSimpleVolume = pSessionControl2;
            if (pSimpleVolume) {
                // Устанавливаем mute (true - отключить звук, false - включить)
                //hr = pSimpleVolume->SetMute(bMute ? TRUE : FALSE, NULL);
                hr = pSimpleVolume->SetMasterVolume(0.001f, NULL);
                if (SUCCEEDED(hr)) {
                    std::cout << "Successfully " << (bMute ? "muted" : "unmuted")
                              << " process with PID: " << targetPid << std::endl;
                    //CoUninitialize();
                    return true;
                } else {
                    std::cerr << "SetMute failed for PID: " << targetPid << std::endl;
                }
            }
        }
    }

    std::cerr << "No audio session found for PID: " << targetPid << std::endl;
    //CoUninitialize();
    return false;
}

// Функция для захвата звука с виртуального устройства
bool CaptureFromVirtualDevice(IMMDevice** outDevice, IAudioClient** outAudioClient, IAudioCaptureClient** outCaptureClient, WAVEFORMATEX** outWfx) {
    extern wchar_t* gSelectedOutputDeviceId;

    HRESULT hr;
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDeviceCollection* pCollection = NULL;
    IMMDevice* pDevice = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioCaptureClient* pCaptureClient = nullptr;
    WAVEFORMATEX* pwfx = nullptr;
    IPropertyStore* pProps = NULL;
    PROPVARIANT varName;

    // 1. Инициализация COM
    //CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // 1. Создаем перечислитель устройств
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        std::cout << "CoCreateInstance failed" << std::endl;
        return false;
    }

    // 2. Получаем коллекцию всех активных устройств вывода (eRender)
    //    Используем DEVICE_STATE_ACTIVE, чтобы получить только подключенные и включенные устройства[reference:3]
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        std::cout << "EnumAudioEndpoints failed" << std::endl;
        pEnumerator->Release();
        return false;
    }

    // 3. Получаем количество устройств в коллекции
    UINT count = 0;
    hr = pCollection->GetCount(&count);
    if (FAILED(hr)) {
        pCollection->Release();
        pEnumerator->Release();
        return false;
    }

    std::cout << "devices count: " << count << std::endl;

    // 4. Проходим по всем устройствам в коллекции
    for (UINT i = 0; i < count; i++) {
        // Получаем IMMDevice для i-го устройства
        hr = pCollection->Item(i, &pDevice);
        if (FAILED(hr)) continue;

        // --- ПОЛУЧАЕМ ID УСТРОЙСТВА (ЭТО ВАМ НУЖНО) ---
        LPWSTR pwszID = NULL;
        hr = pDevice->GetId(&pwszID); // <-- Здесь получаем ID[reference:4][reference:5]
        if (SUCCEEDED(hr)) {
            // Выводим ID (преобразуем в std::wstring для удобства)
            std::wstring deviceId(pwszID);
            std::wcout << L"  ID: " << deviceId << std::endl;

            // Освобождаем строку ID
            CoTaskMemFree(pwszID);
        }

        // --- ПОЛУЧАЕМ ЧЕЛОВЕЧЕСКОЕ ИМЯ УСТРОЙСТВА (для наглядности) ---
        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (SUCCEEDED(hr)) {
            PropVariantInit(&varName);
            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
                std::wcout << L"  name: " << varName.pwszVal << std::endl;
            }
            PropVariantClear(&varName);
            pProps->Release();
            pProps = NULL;
        }

        std::wcout << L"  ------------------------" << std::endl;

        pDevice->Release();
        pDevice = NULL;
    }

    // 5. Очистка ресурсов
    pCollection->Release();

    if (!gSelectedOutputDeviceId) {
        return false;
    }

    hr = pEnumerator->GetDevice(gSelectedOutputDeviceId, &pDevice);

    // 4. Активация IAudioClient
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, 
                           (void**)&pAudioClient);

    // 5. Получение формата звука (обязательно для Initialize)
    hr = pAudioClient->GetMixFormat(&pwfx);

    // 6. Инициализация клиента в режиме Loopback
    //    ВАЖНО: используем AUDCLNT_STREAMFLAGS_LOOPBACK
    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,          // Только разделяемый режим
        AUDCLNT_STREAMFLAGS_LOOPBACK,      // <-- Ключевой флаг для loopback
        10000000,                          // Буфер на 1 секунду (в 100-нс единицах)
        0,                                 // Период обработки (0 = используем буфер)
        pwfx,                              // Формат звука
        NULL);                             // Идентификатор сессии

    // 7. Получение IAudioCaptureClient для чтения данных
    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), 
                                  (void**)&pCaptureClient);

    *outDevice = pDevice;
    *outAudioClient = pAudioClient;
    *outCaptureClient = pCaptureClient;
    *outWfx = pwfx;

    pEnumerator->Release();
    
    return true;

    // 8. Запуск захвата
    //hr = pAudioClient->Start();

    // 9. Цикл чтения данных из буфера
    /*
    std::cout << "Захват звука начат...\n";
    for (int i = 0; i < 100; ++i) { // Пример: 100 итераций
        UINT32 packetLength = 0;
        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        while (packetLength > 0) {
            BYTE* pData = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            // Получение буфера с данными
            hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, 
                                           NULL, NULL);
            if (SUCCEEDED(hr)) {
                // Здесь pData указывает на PCM-данные.
                // Их можно передать в вашу middleware (FMOD/Wwise).
                // Размер данных в байтах: numFramesAvailable * pwfx->nBlockAlign
                // Например, можно сохранить в файл или отправить в движок.
                // printf("Получено %u фреймов\n", numFramesAvailable);

                // Освобождение буфера
                hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
            }
            // Проверка, есть ли ещё пакеты
            hr = pCaptureClient->GetNextPacketSize(&packetLength);
        }
        Sleep(100); // Имитация обработки
    }
    */

    // 10. Остановка и очистка ресурсов
    //pAudioClient->Stop();
    //pCaptureClient->Release();
    //pAudioClient->Release();
    //pDevice->Release();
    //pEnumerator->Release();
    //CoTaskMemFree(pwfx);
    //CoUninitialize();
}

//=============================================================================
// AudioClientActivationHandler Implementation
//=============================================================================

AudioClientActivationHandler::AudioClientActivationHandler(DWORD _targetPid)
    : m_refCount(1)
    , m_completionEvent(nullptr)
    , m_audioClient(nullptr)
    , m_activationResult(E_FAIL)
    , targetPid(_targetPid)
{
    // Use manual-reset event (TRUE) like app2clap, not auto-reset
    m_completionEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_completionEvent) {
        // Event creation failed - set error state
        m_activationResult = E_OUTOFMEMORY;
    }
}

AudioClientActivationHandler::~AudioClientActivationHandler() {
    if (m_audioClient) {
        m_audioClient->Release();
    }
    if (m_completionEvent) {
        CloseHandle(m_completionEvent);
    }
}

STDMETHODIMP AudioClientActivationHandler::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }

    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
        *ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IAgileObject)) {
        *ppvObject = static_cast<IAgileObject*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) AudioClientActivationHandler::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) AudioClientActivationHandler::Release() {
    LONG refCount = InterlockedDecrement(&m_refCount);
    if (refCount == 0) {
        delete this;
    }
    return refCount;
}

STDMETHODIMP AudioClientActivationHandler::ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) {
    if (!operation) {
        m_activationResult = E_INVALIDARG;
        SetEvent(m_completionEvent);
        return E_INVALIDARG;
    }

    // Get the activation result
    IUnknown* audioInterface = nullptr;
    HRESULT hr = operation->GetActivateResult(&m_activationResult, &audioInterface);

    if (SUCCEEDED(hr) && SUCCEEDED(m_activationResult) && audioInterface) {
        std::cout << "success!!!" << std::endl;

        // Get the IAudioClient interface
        hr = audioInterface->QueryInterface(__uuidof(IAudioClient), (void**)&m_audioClient);
        audioInterface->Release();
        if (FAILED(hr)) {
            m_activationResult = hr;
        }

        //MuteProcess(targetPid, true);
    } else {
        std::cout << "failed to activate audio capture: " << m_activationResult << std::endl;
    }

    // Signal completion
    SetEvent(m_completionEvent);
    return S_OK;
}

bool AudioClientActivationHandler::WaitForCompletion(DWORD timeoutMs) {
    if (!m_completionEvent) {
        return false;
    }

    // Use CoWaitForMultipleHandles to pump messages while waiting (required for STA)
    HANDLE handles[1] = { m_completionEvent };
    DWORD index = 0;
    HRESULT hr = CoWaitForMultipleHandles(
        COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
        timeoutMs,
        1,
        handles,
        &index);

    return (hr == S_OK) && SUCCEEDED(m_activationResult);
}

IAudioClient* AudioClientActivationHandler::GetAudioClient() {
    if (m_audioClient) {
        m_audioClient->AddRef();
    }
    return m_audioClient;
}

//=============================================================================
// AudioCapture Implementation
//=============================================================================

AudioCapture::AudioCapture()
    : m_deviceEnumerator(nullptr)
    , m_device(nullptr)
    , m_audioClient(nullptr)
    , m_captureClient(nullptr)
    , m_waveFormat(nullptr)
    , m_isCapturing(false)
    , m_isPaused(false)
    , m_targetProcessId(0)
    , m_volumeMultiplier(1.0f)  // Default to 100% volume
    , m_isProcessSpecific(false)
    , m_isInputDevice(false)
    , m_passthroughEnabled(false)
    , m_renderDevice(nullptr)
    , m_renderClient(nullptr)
    , m_audioRenderClient(nullptr)
    , m_renderBufferFrameCount(0)
{
    // COM should already be initialized by the main thread
}

AudioCapture::~AudioCapture() {
    Stop();
    DisablePassthrough();

    if (m_waveFormat) {
        CoTaskMemFree(m_waveFormat);
    }
    if (m_captureClient) {
        m_captureClient->Release();
    }
    if (m_audioClient) {
        m_audioClient->Release();
    }
    if (m_device) {
        m_device->Release();
    }
    if (m_deviceEnumerator) {
        m_deviceEnumerator->Release();
    }

    // Don't call CoUninitialize - main thread will handle it
}

bool AudioCapture::Initialize(DWORD processId) {
    m_targetProcessId = processId;

    // Clean up any existing state from previous initialization
    if (m_captureClient) {
        m_captureClient->Release();
        m_captureClient = nullptr;
    }
    if (m_audioClient) {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }
    if (m_waveFormat) {
        CoTaskMemFree(m_waveFormat);
        m_waveFormat = nullptr;
    }
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    if (m_deviceEnumerator) {
        m_deviceEnumerator->Release();
        m_deviceEnumerator = nullptr;
    }

    // Create device enumerator
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&m_deviceEnumerator);

    if (FAILED(hr)) {
        return false;
    }

    return CaptureFromVirtualDevice(&m_device, &m_audioClient, &m_captureClient, &m_waveFormat);

    // Get default audio endpoint
    //hr = m_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);

    //if (FAILED(hr)) {
    //    return false;
    //}

    //// Try process-specific capture for non-zero process IDs
    //if (processId != 0) {
    //    if (InitializeProcessSpecificCapture(processId)) {
    //        m_isProcessSpecific = true;
    //        return true;
    //    }
    //}

    //// Fall back to system-wide capture
    //m_isProcessSpecific = false;
    //return InitializeSystemWideCapture();
}

bool AudioCapture::InitializeProcessSpecificCapture(DWORD processId) {
    //MuteProcess(processId, true);

    // Check Windows version using RtlGetVersion
    typedef LONG (WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionPtr RtlGetVersion = nullptr;

    DWORD dwMajorVersion = 0;
    DWORD dwBuild = 0;

    if (hNtdll) {
        RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = { 0 };
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (RtlGetVersion(&osvi) == 0) {
                dwMajorVersion = osvi.dwMajorVersion;
                dwBuild = osvi.dwBuildNumber;
            }
        }
    }

    // Use the virtual device for process loopback (not a physical device)
    LPCWSTR deviceId = VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK;

    // Setup activation parameters for process loopback
    AUDIOCLIENT_ACTIVATION_PARAMS activationParams = {};
    activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    activationParams.ProcessLoopbackParams.TargetProcessId = processId;

    PROPVARIANT activateParams = {};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(activationParams);
    activateParams.blob.pBlobData = (BYTE*)&activationParams;

    // Create completion handler (starts with refcount = 1)
    AudioClientActivationHandler* handler = new (std::nothrow) AudioClientActivationHandler(processId);
    if (!handler || !handler->IsValid()) {
        if (handler) {
            handler->Release();
        }
        return false;
    }

    // Dynamically load ActivateAudioInterfaceAsync to support older Windows versions
    // This function only exists on Windows 8+ with WinRT
    typedef HRESULT (STDAPICALLTYPE *ActivateAudioInterfaceAsyncPtr)(
        LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);

    HMODULE hMmDevApi = LoadLibraryW(L"Mmdevapi.dll");
    if (!hMmDevApi) {
        handler->Release();
        return false;
    }

    ActivateAudioInterfaceAsyncPtr pActivateAudioInterfaceAsync =
        (ActivateAudioInterfaceAsyncPtr)GetProcAddress(hMmDevApi, "ActivateAudioInterfaceAsync");

    if (!pActivateAudioInterfaceAsync) {
        // Function not available (Windows 7 or earlier) - fail gracefully
        FreeLibrary(hMmDevApi);
        handler->Release();
        return false;
    }

    // Start async activation
    IActivateAudioInterfaceAsyncOperation* asyncOp = nullptr;
    HRESULT hr = pActivateAudioInterfaceAsync(
        deviceId,
        __uuidof(IAudioClient),
        &activateParams,
        handler,
        &asyncOp);

    FreeLibrary(hMmDevApi);

    if (FAILED(hr) || !asyncOp) {
        handler->Release();
        return false;
    }

    // Wait for activation to complete (blocks with timeout)
    bool success = handler->WaitForCompletion(5000);  // 5 second timeout

    // Release the async operation
    asyncOp->Release();

    if (!success) {
        handler->Release();
        return false;
    }

    // Get the audio client (this AddRefs it for us)
    m_audioClient = handler->GetAudioClient();

    if (!m_audioClient) {
        handler->Release();
        return false;
    }

    // Release ownership so handler doesn't Release our audio client in its destructor
    handler->ReleaseOwnership();

    // Release our reference to the handler (may delete it if refcount reaches 0)
    handler->Release();

    // Get mix format - for process loopback, GetMixFormat returns E_NOTIMPL
    // so we need to use the default device's format as a template
    hr = m_audioClient->GetMixFormat(&m_waveFormat);

    if (FAILED(hr)) {
        std::cout << "failed to init wav format" << std::endl;
        // GetMixFormat not supported on process loopback device
        // Get format from default device and use it as template
        if (m_device) {
            std::cout << "found device" << std::endl;
            IAudioClient* tempClient = nullptr;
            hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&tempClient);

            if (SUCCEEDED(hr) && tempClient) {
                std::cout << "activated" << std::endl;
                WAVEFORMATEX* defaultFormat = nullptr;
                hr = tempClient->GetMixFormat(&defaultFormat);

                if (SUCCEEDED(hr) && defaultFormat) {
                    std::cout << "got mix format" << std::endl;
                    // Ask the process loopback client what format it will actually use
                    WAVEFORMATEX* closestMatch = nullptr;
                    hr = m_audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, defaultFormat, &closestMatch);

                    if (hr == S_OK && defaultFormat) {
                        // Exact match - use the default format
                        m_waveFormat = defaultFormat;
                        defaultFormat = nullptr; // Don't free it
                    } else if (hr == S_FALSE && closestMatch) {
                        // Close match provided
                        m_waveFormat = closestMatch;
                        closestMatch = nullptr; // Don't free it
                    } else {
                        // No match - use default anyway
                        m_waveFormat = defaultFormat;
                        defaultFormat = nullptr;
                    }

                    if (closestMatch) CoTaskMemFree(closestMatch);
                    if (defaultFormat) CoTaskMemFree(defaultFormat);
                }
                tempClient->Release();
            }
        }

        if (!m_waveFormat) {
            std::cout << "failed anyway" << std::endl;
            m_audioClient->Release();
            m_audioClient = nullptr;
            return false;
        }
    }

    // Validate before Initialize
    if (!m_audioClient || !m_waveFormat) {
        std::cout << "failed validate before init" << std::endl;
        if (m_audioClient) {
            m_audioClient->Release();
            m_audioClient = nullptr;
        }
        return false;
    }

    // Initialize audio client for loopback capture
    const REFERENCE_TIME hnsRequestedDuration = 10000000; // 1 second
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        hnsRequestedDuration,
        0,
        m_waveFormat,
        nullptr);

    if (FAILED(hr)) {
        std::cout << "failed to init audio client" << std::endl;
        CoTaskMemFree(m_waveFormat);
        m_waveFormat = nullptr;
        m_audioClient->Release();
        m_audioClient = nullptr;
        return false;
    }

    // Get capture client
    hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient),
        (void**)&m_captureClient);

    if (FAILED(hr) || !m_captureClient) {
        std::cout << "failed to get service" << std::endl;
        CoTaskMemFree(m_waveFormat);
        m_waveFormat = nullptr;
        m_audioClient->Release();
        m_audioClient = nullptr;
        return false;
    }

    return true;
}

bool AudioCapture::InitializeSystemWideCapture() {
    // Activate audio client (legacy method - captures all system audio)
    HRESULT hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, (void**)&m_audioClient);
    if (FAILED(hr)) {
        return false;
    }

    // Get mix format
    hr = m_audioClient->GetMixFormat(&m_waveFormat);
    if (FAILED(hr)) {
        return false;
    }

    // Initialize audio client for loopback capture
    const REFERENCE_TIME hnsRequestedDuration = 10000000; // 1 second
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsRequestedDuration,
        0,
        m_waveFormat,
        nullptr);
    if (FAILED(hr)) {
        return false;
    }

    // Get capture client
    hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient),
        (void**)&m_captureClient);
    if (FAILED(hr)) {
        return false;
    }

    //MuteProcess(m_targetProcessId, true);

    return true;
}

bool AudioCapture::InitializeFromDevice(const std::wstring& deviceId, bool isInputDevice) {
    m_isInputDevice = isInputDevice;
    m_isProcessSpecific = false;

    // Clean up any existing state from previous initialization
    if (m_captureClient) {
        m_captureClient->Release();
        m_captureClient = nullptr;
    }
    if (m_audioClient) {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }
    if (m_waveFormat) {
        CoTaskMemFree(m_waveFormat);
        m_waveFormat = nullptr;
    }
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    if (m_deviceEnumerator) {
        m_deviceEnumerator->Release();
        m_deviceEnumerator = nullptr;
    }

    // Create device enumerator
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&m_deviceEnumerator);

    if (FAILED(hr)) {
        return false;
    }

    // Get the specified device by ID
    hr = m_deviceEnumerator->GetDevice(deviceId.c_str(), &m_device);
    if (FAILED(hr)) {
        return false;
    }

    // Activate audio client
    hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, (void**)&m_audioClient);
    if (FAILED(hr)) {
        return false;
    }

    // Get mix format
    hr = m_audioClient->GetMixFormat(&m_waveFormat);
    if (FAILED(hr)) {
        return false;
    }

    // Initialize audio client
    const REFERENCE_TIME hnsRequestedDuration = 10000000; // 1 second
    DWORD streamFlags = 0;

    // For input devices (microphones), don't use loopback mode
    // For output devices, use loopback mode to capture what's playing
    if (!isInputDevice) {
        streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
    }

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        hnsRequestedDuration,
        0,
        m_waveFormat,
        nullptr);
    if (FAILED(hr)) {
        return false;
    }

    // Get capture client
    hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient),
        (void**)&m_captureClient);
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

bool AudioCapture::Start() {
    if (m_isCapturing) {
        return false;
    }

    if (!m_audioClient || !m_captureClient) {
        return false;
    }

    // Start audio client
    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        return false;
    }

    //MuteProcess(m_targetProcessId, true);

    m_isCapturing = true;
    m_captureThread = std::thread(&AudioCapture::CaptureThread, this);

    return true;
}

void AudioCapture::Stop() {
    if (!m_isCapturing) {
        return;
    }

    //MuteProcess(m_targetProcessId, false);

    // Stop the audio client FIRST, before stopping the thread
    if (m_audioClient) {
        m_audioClient->Stop();
    }

    // Then signal the thread to stop
    m_isCapturing = false;
    m_isPaused = false;

    // Wait for thread to finish
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
}

void AudioCapture::Pause() {
    if (!m_isCapturing || m_isPaused) {
        return;
    }

    //MuteProcess(m_targetProcessId, false);

    m_isPaused = true;

    // Pause the audio client (stops reading data but keeps the stream open)
    if (m_audioClient) {
        m_audioClient->Stop();
    }

    // Pause passthrough if enabled
    if (m_passthroughEnabled && m_renderClient) {
        m_renderClient->Stop();
    }
}

void AudioCapture::Resume() {
    if (!m_isCapturing || !m_isPaused) {
        return;
    }

    //MuteProcess(m_targetProcessId, true);

    m_isPaused = false;

    // Resume the audio client
    if (m_audioClient) {
        m_audioClient->Start();
    }

    // Resume passthrough if enabled
    if (m_passthroughEnabled && m_renderClient) {
        m_renderClient->Start();
    }
}

void AudioCapture::ApplyVolumeToBuffer(BYTE* data, UINT32 size) {
    if (!data || !m_waveFormat || size == 0) {
        return;
    }

    if (m_volumeMultiplier >= 1.0f) {
        return; // No adjustment needed
    }

    // Check if this is WAVEFORMATEXTENSIBLE
    bool isExtensible = (m_waveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                         m_waveFormat->cbSize >= 22);

    // Determine the actual format
    bool isFloat = false;
    if (isExtensible) {
        WAVEFORMATEXTENSIBLE* wfex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_waveFormat);
        isFloat = (wfex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    } else {
        isFloat = (m_waveFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    }

    if (isFloat && m_waveFormat->wBitsPerSample == 32) {
        // 32-bit float PCM
        float* samples = reinterpret_cast<float*>(data);
        UINT32 numSamples = size / sizeof(float);
        for (UINT32 i = 0; i < numSamples; i++) {
            samples[i] = samples[i] * m_volumeMultiplier;
        }
    }
    else if (m_waveFormat->wBitsPerSample == 16) {
        // 16-bit PCM
        int16_t* samples = reinterpret_cast<int16_t*>(data);
        UINT32 numSamples = size / sizeof(int16_t);
        for (UINT32 i = 0; i < numSamples; i++) {
            samples[i] = static_cast<int16_t>(samples[i] * m_volumeMultiplier);
        }
    }
}

void AudioCapture::CaptureThread() {
    // Validate required members
    if (!m_captureClient || !m_waveFormat) {
        return;
    }

    // Set thread priority for audio
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(L"Audio", &taskIndex);

    // Poll every 10ms
    const DWORD sleepMs = 10;

    while (m_isCapturing) {
        Sleep(sleepMs);

        // Check if we should stop
        if (!m_isCapturing) {
            break;
        }

        UINT32 packetLength = 0;
        HRESULT hr = m_captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            break;
        }

        while (packetLength > 0 && m_isCapturing) {
            BYTE* data = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            hr = m_captureClient->GetBuffer(&data, &numFramesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }

            // Calculate buffer size
            UINT32 bufferSize = numFramesAvailable * m_waveFormat->nBlockAlign;

            // Send data to callback - even if silent, send zeros to keep stream continuous
            if (m_dataCallback && bufferSize > 0) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    // For silent buffers, send zeros
                    std::vector<BYTE> silence(bufferSize, 0);
                    m_dataCallback(silence.data(), bufferSize);
                }
                else if (data) {
                    // Apply volume adjustment
                    std::vector<BYTE> buffer(data, data + bufferSize);
                    ApplyVolumeToBuffer(buffer.data(), bufferSize);
                    m_dataCallback(buffer.data(), bufferSize);

                    // If passthrough is enabled, also send to render device
                    if (m_passthroughEnabled && m_audioRenderClient && m_renderClient) {
                        // Get padding (how much is already in the buffer)
                        UINT32 numFramesPadding = 0;
                        if (SUCCEEDED(m_renderClient->GetCurrentPadding(&numFramesPadding))) {
                            // Calculate how many frames we can write
                            UINT32 renderFramesAvailable = m_renderBufferFrameCount - numFramesPadding;

                            // Calculate how many frames we have from capture
                            UINT32 captureFrames = numFramesAvailable;

                            // Only write as many frames as we have available, and don't exceed buffer space
                            UINT32 framesToWrite = std::min(renderFramesAvailable, captureFrames);

                            if (framesToWrite > 0) {
                                BYTE* renderBuffer = nullptr;
                                if (SUCCEEDED(m_audioRenderClient->GetBuffer(framesToWrite, &renderBuffer))) {
                                    // Copy audio data to render buffer
                                    UINT32 bytesToCopy = framesToWrite * m_waveFormat->nBlockAlign;
                                    memcpy(renderBuffer, buffer.data(), bytesToCopy);

                                    m_audioRenderClient->ReleaseBuffer(framesToWrite, 0);
                                }
                            }
                        }
                    }
                }
            }

            hr = m_captureClient->ReleaseBuffer(numFramesAvailable);
            if (FAILED(hr)) {
                break;
            }

            // Check if we should stop before getting next packet
            if (!m_isCapturing) {
                break;
            }

            hr = m_captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                break;
            }
        }
    }

    if (hTask) {
        AvRevertMmThreadCharacteristics(hTask);
    }
}

bool AudioCapture::EnablePassthrough(const std::wstring& deviceId) {
    // Disable any existing passthrough
    DisablePassthrough();

    if (!m_waveFormat || !m_deviceEnumerator) {
        return false;
    }

    // Get the render device
    HRESULT hr = m_deviceEnumerator->GetDevice(deviceId.c_str(), &m_renderDevice);
    if (FAILED(hr) || !m_renderDevice) {
        return false;
    }

    // Activate audio client for rendering
    hr = m_renderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, (void**)&m_renderClient);
    if (FAILED(hr)) {
        m_renderDevice->Release();
        m_renderDevice = nullptr;
        return false;
    }

    // Initialize the render client with the same format as capture
    // Use smaller buffer for lower latency (100ms instead of 1 second)
    const REFERENCE_TIME hnsRequestedDuration = 1000000; // 100ms
    hr = m_renderClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        hnsRequestedDuration,
        0,
        m_waveFormat,
        nullptr);

    if (FAILED(hr)) {
        m_renderClient->Release();
        m_renderClient = nullptr;
        m_renderDevice->Release();
        m_renderDevice = nullptr;
        return false;
    }

    // Get buffer size
    hr = m_renderClient->GetBufferSize(&m_renderBufferFrameCount);
    if (FAILED(hr)) {
        m_renderClient->Release();
        m_renderClient = nullptr;
        m_renderDevice->Release();
        m_renderDevice = nullptr;
        return false;
    }

    // Get render client service
    hr = m_renderClient->GetService(__uuidof(IAudioRenderClient),
        (void**)&m_audioRenderClient);

    if (FAILED(hr) || !m_audioRenderClient) {
        m_renderClient->Release();
        m_renderClient = nullptr;
        m_renderDevice->Release();
        m_renderDevice = nullptr;
        return false;
    }

    // Pre-fill half the buffer with silence to reduce latency while avoiding underruns
    UINT32 prefillFrames = m_renderBufferFrameCount / 2;
    BYTE* renderBuffer = nullptr;
    hr = m_audioRenderClient->GetBuffer(prefillFrames, &renderBuffer);
    if (SUCCEEDED(hr) && renderBuffer) {
        // Fill with silence
        memset(renderBuffer, 0, prefillFrames * m_waveFormat->nBlockAlign);
        m_audioRenderClient->ReleaseBuffer(prefillFrames, 0);
    }

    // Start the render client
    hr = m_renderClient->Start();
    if (FAILED(hr)) {
        m_audioRenderClient->Release();
        m_audioRenderClient = nullptr;
        m_renderClient->Release();
        m_renderClient = nullptr;
        m_renderDevice->Release();
        m_renderDevice = nullptr;
        return false;
    }

    m_passthroughEnabled = true;
    return true;
}

void AudioCapture::DisablePassthrough() {
    if (!m_passthroughEnabled) {
        return;
    }

    if (m_renderClient) {
        m_renderClient->Stop();
    }

    if (m_audioRenderClient) {
        m_audioRenderClient->Release();
        m_audioRenderClient = nullptr;
    }

    if (m_renderClient) {
        m_renderClient->Release();
        m_renderClient = nullptr;
    }

    if (m_renderDevice) {
        m_renderDevice->Release();
        m_renderDevice = nullptr;
    }

    m_renderBufferFrameCount = 0;
    m_passthroughEnabled = false;
}
