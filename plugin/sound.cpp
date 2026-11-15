#include "sound.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>
#include <vector>

namespace fpv {

namespace {

std::mutex sound_mutex;       // everything below
std::future<void> worker;
SDL_AudioDeviceID device = 0;
bool we_started_audio = false;
std::string loaded_path;      // the file in `wav` (set even when it could not be read, so it is not tried again)
SDL_AudioSpec wav_spec{};
Uint8 *wav = nullptr;
Uint32 wav_len = 0;
std::string status = "nothing played yet";

std::string error_of(const SoundApi &api) {
    const char *e = api.get_error ? api.get_error() : nullptr;
    return e && *e ? e : "unknown error";
}

void free_wav(const SoundApi &api) {
    if (wav)
        api.free_wav(wav);
    wav = nullptr;
    wav_len = 0;
}

void play_now(const SoundApi &api, const std::string &path, float volume) {
    std::lock_guard<std::mutex> lock(sound_mutex);
    if (!api.was_init(SDL_INIT_AUDIO)) {
        if (api.init_subsystem(SDL_INIT_AUDIO) != 0) {
            status = "SDL audio would not start: " + error_of(api);
            return;
        }
        we_started_audio = true;
    }
    if (path != loaded_path) {
        free_wav(api);
        loaded_path = path;
        SDL_RWops *rw = api.rw_from_file(path.c_str(), "rb");
        SDL_AudioSpec spec{};
        if (!rw || !api.load_wav_rw(rw, 1, &spec, &wav, &wav_len)) {
            wav = nullptr;
            wav_len = 0;
            status = "could not read " + path + ": " + error_of(api);
            return;
        }
        // a device opened for another file's format is opened again
        if (device && (spec.freq != wav_spec.freq || spec.format != wav_spec.format ||
                       spec.channels != wav_spec.channels)) {
            api.close_audio_device(device);
            device = 0;
        }
        wav_spec = spec;
    }
    if (!wav || !wav_len)
        return;
    if (!device) {
        SDL_AudioSpec want = wav_spec;
        want.callback = nullptr;   // fed with SDL_QueueAudio
        want.userdata = nullptr;
        want.samples = 4096;
        device = api.open_audio_device(nullptr, 0, &want, nullptr, 0);   // SDL converts to what the card takes
        if (!device) {
            status = "could not open an audio device: " + error_of(api);
            return;
        }
        api.pause_audio_device(device, 0);
    }
    const int vol = std::clamp(int(volume * SDL_MIX_MAXVOLUME + 0.5f), 0, SDL_MIX_MAXVOLUME);
    if (vol <= 0) {
        status = "volume is 0";
        return;
    }
    // the file mixed onto silence at the volume
    std::vector<Uint8> out(wav_len, wav_spec.silence);
    api.mix_audio_format(out.data(), wav, wav_spec.format, wav_len, vol);
    if (api.queue_audio(device, out.data(), wav_len) != 0) {
        status = "could not queue the sound: " + error_of(api);
        return;
    }
    status = "played " + path + " at volume " + std::to_string(vol) + "/128";
}

}   // namespace

bool SoundApi::ok() const {
    return was_init && init_subsystem && quit_subsystem && rw_from_file && load_wav_rw && free_wav &&
           open_audio_device && close_audio_device && pause_audio_device && queue_audio && mix_audio_format;
}

void play_sound(const SoundApi &api, const std::string &path, float volume) {
    if (!api.ok())
        return;
    if (worker.valid() && worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    worker = std::async(std::launch::async, [api, path, volume] { play_now(api, path, volume); });
}

void close_sound(const SoundApi &api) {
    if (worker.valid())
        worker.wait();
    if (!api.ok())
        return;
    std::lock_guard<std::mutex> lock(sound_mutex);
    if (device)
        api.close_audio_device(device);
    device = 0;
    free_wav(api);
    loaded_path.clear();
    if (we_started_audio)
        api.quit_subsystem(SDL_INIT_AUDIO);
    we_started_audio = false;
}

std::string sound_status() {
    std::lock_guard<std::mutex> lock(sound_mutex);
    return status;
}

}   // namespace fpv
