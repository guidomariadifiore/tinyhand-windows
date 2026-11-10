#pragma once

#include "Common.h"
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>

namespace TinyHand {

struct DecodedSound {
    std::vector<float> samples; // Interleaved stereo floats (L, R, L, R...)
    size_t frameCount = 0;
};

struct ActiveVoice {
    TapSoundChoiceId id;
    size_t cursor = 0;
};

class TapSoundPlayer {
public:
    static TapSoundPlayer& Instance();
    
    void LoadAll();
    void Play(TapSoundChoiceId choice);
    bool IsWasapiActive() const { return m_wasapiAvailable; }
    bool IsAudioClient3() const { return m_isAudioClient3; }
    size_t GetLoadedSoundCount() const { return m_decodedSounds.size(); }
    UINT32 GetBufferFrames() const { return m_bufferFrames; }
    UINT32 GetPeriodFrames() const { return m_periodFrames; }
    int GetMixRate() const { return m_mixRate; }
    int GetMixChannels() const { return m_mixChannels; }
    
private:
    TapSoundPlayer();
    ~TapSoundPlayer();
    
    void InitWasapi();
    void ShutdownWasapi();
    void AudioThreadProc();
    
    std::wstring FindSoundPath(const std::wstring& filename);
    bool LoadWav(const std::wstring& path, std::vector<BYTE>& buffer);
    bool LoadWavAsFloat(const std::wstring& path, int targetSampleRate, DecodedSound& outSound);
    
    std::wstring m_exeDir;
    std::map<TapSoundChoiceId, std::vector<BYTE>> m_rawBuffers;
    std::map<TapSoundChoiceId, DecodedSound> m_decodedSounds;
    
    HRESULT m_hrCom = E_FAIL;
    bool m_wasapiAvailable = false;
    bool m_isAudioClient3 = false;
    bool m_isFloatFormat = true;
    int m_mixRate = 48000;
    int m_mixChannels = 2;
    UINT32 m_bufferFrames = 0;
    UINT32 m_periodFrames = 0;
    
    void* m_pAudioClient = nullptr;     // IAudioClient*
    void* m_pRenderClient = nullptr;    // IAudioRenderClient*
    void* m_pMixFormat = nullptr;       // WAVEFORMATEX*
    HANDLE m_hAudioEvent = NULL;
    
    std::atomic<bool> m_running{ false };
    std::thread m_audioThread;
    
    std::mutex m_voiceMutex;
    std::vector<ActiveVoice> m_voices;
};

} // namespace TinyHand
