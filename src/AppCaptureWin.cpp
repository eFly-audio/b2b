// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

// WASAPI process loopback: capture only this process's audio (the DAW we run inside).
#include "AppCapture.h"
#if defined(_WIN32)
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace {
// from audioclientactivationparams.h (not shipped with mingw)
struct PlParams { DWORD TargetProcessId; DWORD ProcessLoopbackMode; }; // 0 = include target process tree
struct ActParams { DWORD ActivationType; PlParams ProcessLoopbackParams; }; // 1 = process loopback

class Handler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_FAIL;
    IAudioClient* client = nullptr;
    ~Handler() { CloseHandle(done); }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG) InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto r = InterlockedDecrement(&refs);
        if (r == 0) delete this;
        return (ULONG) r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (iid == __uuidof(IAgileObject))
            *out = static_cast<IAgileObject*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        IUnknown* unk = nullptr;
        HRESULT hr = E_FAIL;
        op->GetActivateResult(&hr, &unk);
        result = hr;
        if (SUCCEEDED(hr) && unk) unk->QueryInterface(__uuidof(IAudioClient), (void**) &client);
        if (unk) unk->Release();
        SetEvent(done);
        return S_OK;
    }

private:
    LONG refs = 1;
};

std::mutex gMtx;
std::thread gThread;
std::atomic<bool> gStop { false };
std::atomic<double> gRate { 0.0 };

void captureLoop(double rate, appcapture::Callback cb, std::promise<std::string> ready) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string err;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* cap = nullptr;
    HANDLE ev = nullptr;
    bool isFloat = true;
    do {
        ActParams p { 1, { GetCurrentProcessId(), 0 } };
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof(p);
        pv.blob.pBlobData = reinterpret_cast<BYTE*>(&p);
        auto* h = new Handler();
        IActivateAudioInterfaceAsyncOperation* op = nullptr;
        HRESULT hr = ActivateAudioInterfaceAsync(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &pv, h, &op);
        if (FAILED(hr) || WaitForSingleObject(h->done, 5000) != WAIT_OBJECT_0 || FAILED(h->result) || !h->client) {
            err = "App audio capture needs Windows 10 (version 2004) or newer";
            if (op) op->Release();
            h->Release();
            break;
        }
        client = h->client;
        if (op) op->Release();
        h->Release();

        WAVEFORMATEX wf {};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wf.nChannels = 2;
        wf.nSamplesPerSec = (DWORD) rate;
        wf.wBitsPerSample = 32;
        wf.nBlockAlign = 8;
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * 8;
        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                            | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &wf, nullptr);
        if (FAILED(hr)) { // some systems only take 16-bit PCM here
            isFloat = false;
            wf.wFormatTag = WAVE_FORMAT_PCM;
            wf.wBitsPerSample = 16;
            wf.nBlockAlign = 4;
            wf.nAvgBytesPerSec = wf.nSamplesPerSec * 4;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &wf, nullptr);
        }
        if (FAILED(hr)) { err = "Can't start app audio capture (error " + std::to_string((unsigned) hr) + ")"; break; }
        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        client->SetEventHandle(ev);
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**) &cap)) || FAILED(client->Start())) {
            err = "Can't start app audio capture";
            break;
        }
    } while (false);
    ready.set_value(err);
    if (err.empty()) {
        gRate = rate;
        std::vector<float> L, R;
        while (!gStop.load()) {
            WaitForSingleObject(ev, 50);
            UINT32 pkt = 0;
            while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt > 0) {
                BYTE* data = nullptr;
                UINT32 n = 0;
                DWORD fl = 0;
                if (FAILED(cap->GetBuffer(&data, &n, &fl, nullptr, nullptr))) break;
                L.resize(n);
                R.resize(n);
                if ((fl & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                    std::fill(L.begin(), L.end(), 0.0f);
                    std::fill(R.begin(), R.end(), 0.0f);
                } else if (isFloat) {
                    auto* f = reinterpret_cast<const float*>(data);
                    for (UINT32 i = 0; i < n; ++i) { L[i] = f[2 * i]; R[i] = f[2 * i + 1]; }
                } else {
                    auto* s = reinterpret_cast<const int16_t*>(data);
                    for (UINT32 i = 0; i < n; ++i) { L[i] = s[2 * i] / 32768.0f; R[i] = s[2 * i + 1] / 32768.0f; }
                }
                cap->ReleaseBuffer(n);
                if (n > 0 && cb) cb(L.data(), R.data(), (int) n);
            }
        }
        client->Stop();
    }
    if (cap) cap->Release();
    if (client) client->Release();
    if (ev) CloseHandle(ev);
    gRate = 0.0;
    CoUninitialize();
}
} // namespace

namespace appcapture {
std::string start(double sampleRate, Callback cb) {
    stop();
    std::lock_guard<std::mutex> l(gMtx);
    gStop = false;
    std::promise<std::string> ready;
    auto fut = ready.get_future();
    gThread = std::thread(captureLoop, sampleRate, std::move(cb), std::move(ready));
    auto err = fut.get();
    if (!err.empty() && gThread.joinable()) gThread.join();
    return err;
}
void stop() {
    std::lock_guard<std::mutex> l(gMtx);
    gStop = true;
    if (gThread.joinable()) gThread.join();
}
double actualRate() { return gRate.load(); }
} // namespace appcapture

#elif !defined(__APPLE__)
namespace appcapture {
std::string start(double, Callback) { return "App audio capture isn't available on this system"; }
void stop() {}
double actualRate() { return 0.0; }
}
#endif
