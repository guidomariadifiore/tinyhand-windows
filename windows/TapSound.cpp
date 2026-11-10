#include "TapSound.h"
#include <mmsystem.h>
#include <fstream>

#pragma comment(lib, "winmm.lib")

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
    LoadAll();
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

void TapSoundPlayer::LoadAll() {
    for (const auto& s : GetAllTapSounds()) {
        std::wstring filename = std::wstring(L"tap_") + s.rawName + L".wav";
        std::wstring path = FindSoundPath(filename);
        if (!path.empty()) {
            std::vector<BYTE> buf;
            if (LoadWav(path, buf)) {
                m_buffers[s.id] = std::move(buf);
            }
        }
    }
}

void TapSoundPlayer::Play(TapSoundChoiceId choice) {
    auto it = m_buffers.find(choice);
    if (it != m_buffers.end() && !it->second.empty()) {
        PlaySoundW((LPCWSTR)it->second.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
}

} // namespace TinyHand
