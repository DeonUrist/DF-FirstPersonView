// Sounds the game does not make (thunder). The game plays its own through
// FMOD; these go through SDL's audio, which the game leaves unused, on a
// device of their own.
#pragma once

#include <string>

#include <SDL.h>   // SDL_WasInit and the subsystem calls
#include <SDL_audio.h>
#include <SDL_rwops.h>

namespace fpv {

// SDL's audio functions, looked up in the game's SDL2.dll; any of them missing: no sound
struct SoundApi {
    decltype(&SDL_WasInit) was_init = nullptr;
    decltype(&SDL_InitSubSystem) init_subsystem = nullptr;
    decltype(&SDL_QuitSubSystem) quit_subsystem = nullptr;
    decltype(&SDL_RWFromFile) rw_from_file = nullptr;
    decltype(&SDL_LoadWAV_RW) load_wav_rw = nullptr;
    decltype(&SDL_FreeWAV) free_wav = nullptr;
    decltype(&SDL_OpenAudioDevice) open_audio_device = nullptr;
    decltype(&SDL_CloseAudioDevice) close_audio_device = nullptr;
    decltype(&SDL_PauseAudioDevice) pause_audio_device = nullptr;
    decltype(&SDL_QueueAudio) queue_audio = nullptr;
    decltype(&SDL_MixAudioFormat) mix_audio_format = nullptr;
    decltype(&SDL_GetError) get_error = nullptr;
    bool ok() const;
};

// Plays a WAV file once at a volume 0..1. Opening the audio the first time
// can take a moment, so it is done on a worker thread; a play asked for while
// the worker is still busy is dropped. The file is read once and kept.
void play_sound(const SoundApi &api, const std::string &path, float volume);

// waits for the worker, closes the device and lets the file go (plugin unload)
void close_sound(const SoundApi &api);

// what happened last, for `fpview status`
std::string sound_status();

}   // namespace fpv
