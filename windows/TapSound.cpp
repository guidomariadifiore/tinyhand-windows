#include "TapSound.h"
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <fstream>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")

namespace TinyHand {

TapSoundPlayer& TapSoundPlayer::Instance() {
    static TapSoundPlayer instance;
    return instance;
}

TapSoundPlayer::TapSoundPlayer() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring p = path;
    size_t lastSlash = p.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        m_exeDir = p.substr(0, lastSlash);
    }
    
    // Initialize WASAPI first to obtain the system's preferred mix rate and format
    InitWasapi();
    LoadAll();
}

TapSoundPlayer::~TapSoundPlayer() {
    ShutdownWasapi();
}

void TapSoundPlayer::InitWasapi() {
    m_hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    
    IMMDeviceEnumerator* pEnum = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
    if (FAILED(hr)) return;
    
    IMMDevice* pDevice = NULL;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    pEnum->Release();
    if (FAILED(hr)) return;
    
    IAudioClient* pAudioClient = NULL;
    WAVEFORMATEX* pMixFormat = NULL;
    
    // 1. Attempt ultra-low latency shared mode via IAudioClient3 (Windows 10/11)
    IAudioClient3* pClient3 = NULL;
    hr = pDevice->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, NULL, (void**)&pClient3);
    if (SUCCEEDED(hr) && pClient3) {
        hr = pClient3->GetMixFormat(&pMixFormat);
        if (SUCCEEDED(hr) && pMixFormat) {
            UINT32 defaultPeriod = 0, fundamentalPeriod = 0, minPeriod = 0, maxPeriod = 0;
            hr = pClient3->GetSharedModeEnginePeriod(pMixFormat, &defaultPeriod, &fundamentalPeriod, &minPeriod, &maxPeriod);
            if (SUCCEEDED(hr) && minPeriod > 0) {
                hr = pClient3->InitializeSharedAudioStream(
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    minPeriod,
                    pMixFormat,
                    NULL);
                if (SUCCEEDED(hr)) {
                    pAudioClient = pClient3;
                    m_isAudioClient3 = true;
                    m_periodFrames = minPeriod;
                }
            }
            if (!pAudioClient) {
                CoTaskMemFree(pMixFormat);
                pMixFormat = NULL;
                pClient3->Release();
                pClient3 = NULL;
            }
        } else {
            pClient3->Release();
            pClient3 = NULL;
        }
    }
    
    // 2. Fallback to standard IAudioClient shared mode if IAudioClient3 is not supported
    if (!pAudioClient) {
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
        if (FAILED(hr) || !pAudioClient) {
            pDevice->Release();
            return;
        }
        
        hr = pAudioClient->GetMixFormat(&pMixFormat);
        if (FAILED(hr) || !pMixFormat) {
            pAudioClient->Release();
            pDevice->Release();
            return;
        }
        
        // Request minimum standard period
        REFERENCE_TIME hnsDuration = 100000; // 10ms
        hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                     hnsDuration, 0, pMixFormat, NULL);
        if (FAILED(hr)) {
            CoTaskMemFree(pMixFormat);
            pAudioClient->Release();
            pDevice->Release();
            return;
        }
    }
    
    pDevice->Release();
    
    m_mixRate = pMixFormat->nSamplesPerSec;
    m_mixChannels = pMixFormat->nChannels;
    
    if (pMixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        m_isFloatFormat = true;
    } else if (pMixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE* pExt = (WAVEFORMATEXTENSIBLE*)pMixFormat;
        m_isFloatFormat = IsEqualGUID(pExt->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    } else {
        m_isFloatFormat = false;
    }
    
    HANDLE hAudioEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = pAudioClient->SetEventHandle(hAudioEvent);
    if (FAILED(hr)) {
        CloseHandle(hAudioEvent);
        CoTaskMemFree(pMixFormat);
        pAudioClient->Release();
        return;
    }
    
    IAudioRenderClient* pRenderClient = NULL;
    hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
    if (FAILED(hr)) {
        CloseHandle(hAudioEvent);
        CoTaskMemFree(pMixFormat);
        pAudioClient->Release();
        return;
    }
    
    pAudioClient->GetBufferSize(&m_bufferFrames);
    
    m_pAudioClient = pAudioClient;
    m_pRenderClient = pRenderClient;
    m_pMixFormat = pMixFormat;
    m_hAudioEvent = hAudioEvent;
    
    m_running.store(true);
    m_audioThread = std::thread(&TapSoundPlayer::AudioThreadProc, this);
    m_wasapiAvailable = true;
}

void TapSoundPlayer::ShutdownWasapi() {
    if (m_running.load()) {
        m_running.store(false);
        if (m_hAudioEvent) SetEvent(m_hAudioEvent);
        if (m_audioThread.joinable()) {
            m_audioThread.join();
        }
    }
    if (m_pRenderClient) {
        ((IAudioRenderClient*)m_pRenderClient)->Release();
        m_pRenderClient = nullptr;
    }
    if (m_pAudioClient) {
        ((IAudioClient*)m_pAudioClient)->Release();
        m_pAudioClient = nullptr;
    }
    if (m_pMixFormat) {
        CoTaskMemFree(m_pMixFormat);
        m_pMixFormat = nullptr;
    }
    if (m_hAudioEvent) {
        CloseHandle(m_hAudioEvent);
        m_hAudioEvent = NULL;
    }
    if (m_hrCom == S_OK || m_hrCom == S_FALSE) {
        CoUninitialize();
        m_hrCom = E_FAIL;
    }
    m_wasapiAvailable = false;
}

void TapSoundPlayer::AudioThreadProc() {
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    
    IAudioClient* pAudioClient = (IAudioClient*)m_pAudioClient;
    IAudioRenderClient* pRenderClient = (IAudioRenderClient*)m_pRenderClient;
    
    pAudioClient->Start();
    
    std::vector<float> mixBuffer;
    
    while (m_running.load(std::memory_order_relaxed)) {
        DWORD waitRes = WaitForSingleObject(m_hAudioEvent, 20);
        (void)waitRes;
        if (!m_running.load(std::memory_order_relaxed)) break;
        
        bool hasVoices = false;
        {
            std::lock_guard<std::mutex> lock(m_voiceMutex);
            hasVoices = !m_voices.empty();
        }
        
        if (!hasVoices) {
            // Drain queue so padding stays at 0 when idle.
            // Next tap starts instantly at offset 0 (<5ms latency) without queued silence delay.
            continue;
        }
        
        UINT32 padding = 0;
        HRESULT hr = pAudioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) continue;
        
        UINT32 framesNeeded = m_bufferFrames - padding;
        if (framesNeeded == 0) continue;
        
        BYTE* pData = NULL;
        hr = pRenderClient->GetBuffer(framesNeeded, &pData);
        if (FAILED(hr) || !pData) continue;
        
        mixBuffer.assign(framesNeeded * 2, 0.0f);
        
        {
            std::lock_guard<std::mutex> lock(m_voiceMutex);
            for (auto it = m_voices.begin(); it != m_voices.end(); ) {
                auto soundIt = m_decodedSounds.find(it->id);
                if (soundIt == m_decodedSounds.end()) {
                    it = m_voices.erase(it);
                    continue;
                }
                
                const auto& sound = soundIt->second;
                size_t framesToMix = (std::min)((size_t)framesNeeded, sound.frameCount - it->cursor);
                
                for (size_t f = 0; f < framesToMix; ++f) {
                    mixBuffer[f * 2 + 0] += sound.samples[(it->cursor + f) * 2 + 0];
                    mixBuffer[f * 2 + 1] += sound.samples[(it->cursor + f) * 2 + 1];
                }
                
                it->cursor += framesToMix;
                if (it->cursor >= sound.frameCount) {
                    it = m_voices.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Write to WASAPI hardware buffer with clamping
        if (m_isFloatFormat) {
            float* pFloat = (float*)pData;
            for (UINT32 f = 0; f < framesNeeded; ++f) {
                float sL = (std::max)(-1.0f, (std::min)(1.0f, mixBuffer[f * 2 + 0]));
                float sR = (std::max)(-1.0f, (std::min)(1.0f, mixBuffer[f * 2 + 1]));
                for (int ch = 0; ch < m_mixChannels; ++ch) {
                    *pFloat++ = (ch == 0) ? sL : ((ch == 1) ? sR : 0.0f);
                }
            }
        } else {
            int16_t* pInt = (int16_t*)pData;
            for (UINT32 f = 0; f < framesNeeded; ++f) {
                float sL = (std::max)(-1.0f, (std::min)(1.0f, mixBuffer[f * 2 + 0]));
                float sR = (std::max)(-1.0f, (std::min)(1.0f, mixBuffer[f * 2 + 1]));
                for (int ch = 0; ch < m_mixChannels; ++ch) {
                    float v = (ch == 0) ? sL : ((ch == 1) ? sR : 0.0f);
                    *pInt++ = (int16_t)(v * 32767.0f);
                }
            }
        }
        
        pRenderClient->ReleaseBuffer(framesNeeded, 0);
    }
    
    pAudioClient->Stop();
    timeEndPeriod(1);
}

std::wstring TapSoundPlayer::FindSoundPath(const std::wstring& filename) {
    std::vector<std::wstring> candidates = {
        L"assets\\sounds\\" + filename,
        m_exeDir + L"\\assets\\sounds\\" + filename,
        m_exeDir + L"\\..\\assets\\sounds\\" + filename,
        m_exeDir + L"\\" + filename,
        L"..\\assets\\sounds\\" + filename,
        filename
    };
    
    for (const auto& c : candidates) {
        DWORD attr = GetFileAttributesW(c.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return c;
        }
    }
    return L"";
}

bool TapSoundPlayer::LoadWav(const std::wstring& path, std::vector<BYTE>& buffer) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    buffer.resize((size_t)size);
    if (!file.read((char*)buffer.data(), size)) {
        buffer.clear();
        return false;
    }
    return true;
}

bool TapSoundPlayer::LoadWavAsFloat(const std::wstring& path, int targetSampleRate, DecodedSound& outSound) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    
    char riff[4];
    file.read(riff, 4);
    if (memcmp(riff, "RIFF", 4) != 0) return false;
    
    uint32_t fileSize;
    file.read((char*)&fileSize, 4);
    
    char wave[4];
    file.read(wave, 4);
    if (memcmp(wave, "WAVE", 4) != 0) return false;
    
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    std::vector<int16_t> raw16;
    
    while (file) {
        char chunkId[4];
        file.read(chunkId, 4);
        if (!file) break;
        uint32_t chunkSize = 0;
        file.read((char*)&chunkSize, 4);
        if (!file) break;
        
        if (memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t formatTag = 0;
            file.read((char*)&formatTag, 2);
            uint16_t numCh = 0;
            file.read((char*)&numCh, 2);
            channels = numCh;
            uint32_t sRate = 0;
            file.read((char*)&sRate, 4);
            sampleRate = sRate;
            uint32_t byteRate = 0;
            file.read((char*)&byteRate, 4);
            uint16_t blockAlign = 0;
            file.read((char*)&blockAlign, 2);
            uint16_t bps = 0;
            file.read((char*)&bps, 2);
            bitsPerSample = bps;
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (memcmp(chunkId, "data", 4) == 0) {
            if (bitsPerSample == 16) {
                raw16.resize(chunkSize / 2);
                file.read((char*)raw16.data(), chunkSize);
            } else {
                file.seekg(chunkSize, std::ios::cur);
            }
            break;
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }
    }
    
    if (raw16.empty() || channels == 0 || sampleRate == 0) return false;
    
    int srcFrames = (int)raw16.size() / channels;
    int dstFrames = (int)((int64_t)srcFrames * targetSampleRate / sampleRate);
    outSound.frameCount = (size_t)dstFrames;
    outSound.samples.resize(dstFrames * 2);
    
    for (int i = 0; i < dstFrames; ++i) {
        double srcIdx = (double)i * sampleRate / targetSampleRate;
        int i0 = (int)srcIdx;
        int i1 = (std::min)(i0 + 1, srcFrames - 1);
        double frac = srcIdx - i0;
        
        float s0 = 0.0f, s1 = 0.0f;
        if (channels == 1) {
            s0 = (float)raw16[i0] / 32768.0f;
            s1 = (float)raw16[i1] / 32768.0f;
        } else {
            s0 = (float)(raw16[i0 * 2] + raw16[i0 * 2 + 1]) / 65536.0f;
            s1 = (float)(raw16[i1 * 2] + raw16[i1 * 2 + 1]) / 65536.0f;
        }
        float val = (float)(s0 + (s1 - s0) * frac);
        outSound.samples[i * 2 + 0] = val;
        outSound.samples[i * 2 + 1] = val;
    }
    return true;
}

void TapSoundPlayer::LoadAll() {
    for (const auto& s : GetAllTapSounds()) {
        std::wstring filename = std::wstring(L"tap_") + s.rawName + L".wav";
        std::wstring path = FindSoundPath(filename);
        if (!path.empty()) {
            std::vector<BYTE> buf;
            if (LoadWav(path, buf)) {
                m_rawBuffers[s.id] = std::move(buf);
            }
            
            DecodedSound decoded;
            if (LoadWavAsFloat(path, m_mixRate, decoded)) {
                m_decodedSounds[s.id] = std::move(decoded);
            }
        }
    }
}

void TapSoundPlayer::Play(TapSoundChoiceId choice) {
    if (m_wasapiAvailable) {
        {
            std::lock_guard<std::mutex> lock(m_voiceMutex);
            if (m_voices.size() < 16) {
                m_voices.push_back({ choice, 0 });
            }
        }
        if (m_hAudioEvent) {
            SetEvent(m_hAudioEvent);
        }
        return;
    }
    
    // Fallback if WASAPI unavailable
    auto it = m_rawBuffers.find(choice);
    if (it != m_rawBuffers.end() && !it->second.empty()) {
        PlaySoundW((LPCWSTR)it->second.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
}

} // namespace TinyHand
