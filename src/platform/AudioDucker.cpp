#include "AudioDucker.h"
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <fstream>

using Microsoft::WRL::ComPtr;

namespace sq {

// Percorre as sessões do dispositivo padrão; para as do `pid`, lê o volume (original) e define o novo.
bool AudioDucker::apply(DWORD pid, float* originalOut, float setTo, bool setAbsolute, float factor) {
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return false;
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev))) return false;
    ComPtr<IAudioSessionManager2> mgr;
    if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)mgr.GetAddressOf()))) return false;
    ComPtr<IAudioSessionEnumerator> list;
    if (FAILED(mgr->GetSessionEnumerator(&list))) return false;
    int n = 0;
    list->GetCount(&n);
    bool any = false;
    for (int i = 0; i < n; ++i) {
        ComPtr<IAudioSessionControl> ctl;
        if (FAILED(list->GetSession(i, &ctl))) continue;
        ComPtr<IAudioSessionControl2> c2;
        if (FAILED(ctl.As(&c2))) continue;
        DWORD p = 0;
        if (FAILED(c2->GetProcessId(&p)) || p != pid) continue;
        ComPtr<ISimpleAudioVolume> vol;
        if (FAILED(ctl.As(&vol))) continue;
        float cur = 1.0f;
        vol->GetMasterVolume(&cur);
        if (originalOut && !any) *originalOut = cur;
        vol->SetMasterVolume(setAbsolute ? setTo : cur * factor, nullptr);
        any = true;
    }
    return any;
}

void AudioDucker::duck(DWORD pid, float factor) {
    if (ducked_ || !pid || factor >= 0.999f) return;
    float orig = 1.0f;
    if (!apply(pid, &orig, 0, false, factor)) return;
    ducked_ = true; pid_ = pid; original_ = orig;
    // Se já existe um registro (volume baixo que sobrou de antes e nunca foi restaurado), ele é o original de
    // verdade: o volume atual já está baixo. Não sobrescreve.
    std::error_code ec;
    if (std::filesystem::exists(pending_, ec)) {
        float saved = 0;
        std::ifstream in(pending_);
        if (in >> saved && saved > original_) original_ = saved;
    } else {
        std::filesystem::create_directories(pending_.parent_path(), ec);
        std::ofstream(pending_, std::ios::trunc) << orig;   // para recuperar após fechar/crash
    }
}

bool AudioDucker::restore() {
    if (!ducked_) return true;
    bool ok = apply(pid_, nullptr, original_, true, 1.0f);
    ducked_ = false;
    if (ok) {
        std::error_code ec;
        std::filesystem::remove(pending_, ec);
    }
    // se não achou a sessão (ex.: o jogo já fechou), o registro continua e recoverPending tenta de novo
    return ok;
}

void AudioDucker::recoverPending(DWORD pid) {
    std::error_code ec;
    if (ducked_ || !pid || !std::filesystem::exists(pending_, ec)) return;
    float v = 1.0f;
    std::ifstream in(pending_);
    if (!(in >> v)) { std::filesystem::remove(pending_, ec); return; }
    if (apply(pid, nullptr, v, true, 1.0f)) std::filesystem::remove(pending_, ec);
}

bool AudioDucker::setVolume(DWORD pid, float v) {
    if (!pid) return false;
    bool ok = apply(pid, nullptr, v, true, 1.0f);
    if (ok && !ducked_) {
        std::error_code ec;
        std::filesystem::remove(pending_, ec);
    }
    return ok;
}
}  // namespace sq
