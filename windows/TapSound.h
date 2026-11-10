#pragma once

#include "Common.h"
#include <map>
#include <vector>
#include <string>

namespace TinyHand {

class TapSoundPlayer {
public:
    static TapSoundPlayer& Instance();
    
    void LoadAll();
    void Play(TapSoundChoiceId choice);
    
private:
    TapSoundPlayer();
    ~TapSoundPlayer() = default;
    
    std::wstring FindSoundPath(const std::wstring& filename);
    bool LoadWav(const std::wstring& path, std::vector<BYTE>& buffer);
    
    std::map<TapSoundChoiceId, std::vector<BYTE>> m_buffers;
    std::wstring m_exeDir;
};

} // namespace TinyHand
