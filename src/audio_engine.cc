#include "audio_engine.h"

#include <string.h>

#include "plib/gnw/debug.h"

#include <string.h>

#include <mutex>

#include <SDL.h>

namespace fallout {

#define AUDIO_ENGINE_SOUND_BUFFERS 8

struct AudioEngineSoundBuffer {
    bool active;
    unsigned int size;
    int bitsPerSample;
    int channels;
    int rate;
    void* data;
    int volume;
    bool playing;
    bool looping;
    unsigned int pos;
#ifdef __PS2__
    // Fractional source position (16.16) for the direct PS2 mixer.
    unsigned int frac;
#endif
    SDL_AudioStream* stream;
    std::recursive_mutex mutex;
};

extern bool GNW95_isActive;

static bool soundBufferIsValid(int soundBufferIndex);
static void audioEngineMixin(void* userData, Uint8* stream, int length);

static SDL_AudioSpec gAudioEngineSpec;
static SDL_AudioDeviceID gAudioEngineDeviceId = -1;
static AudioEngineSoundBuffer gAudioEngineSoundBuffers[AUDIO_ENGINE_SOUND_BUFFERS];

static bool audioEngineIsInitialized()
{
    return gAudioEngineDeviceId != -1;
}

static bool soundBufferIsValid(int soundBufferIndex)
{
    return soundBufferIndex >= 0 && soundBufferIndex < AUDIO_ENGINE_SOUND_BUFFERS;
}

#ifdef __PS2__
// Direct software mixer for the PS2.
//
// The generic path below converts each sound one sample frame at a time with
// SDL_AudioStreamPut/Get, which is far too slow for the EE and makes the audio
// thread miss its deadlines (crackling, stutter, wrong speed). This mixes every
// active buffer straight into the 16-bit stereo output in one pass, resampling
// with a 16.16 fixed-point step, with volume and clipping.
static void audioEngineMixinPs2(Uint8* stream, int length)
{
    Sint16* out = (Sint16*)stream;
    int outChannels = gAudioEngineSpec.channels;
    int outFrames = length / (2 * outChannels);

    static int accumulator[4096 * 2];
    if (outFrames > 4096) {
        outFrames = 4096;
    }
    memset(accumulator, 0, sizeof(int) * outFrames * 2);

    if (GNW95_isActive) {
        for (int index = 0; index < AUDIO_ENGINE_SOUND_BUFFERS; index++) {
            AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[index]);
            std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

            if (!soundBuffer->active || !soundBuffer->playing || soundBuffer->data == NULL) {
                continue;
            }

            int bytesPerSample = soundBuffer->bitsPerSample / 8;
            int frameSize = bytesPerSample * soundBuffer->channels;
            if (frameSize <= 0 || soundBuffer->size < (unsigned int)frameSize) {
                continue;
            }

            unsigned int totalFrames = soundBuffer->size / frameSize;
            unsigned int frame = soundBuffer->pos / frameSize;
            unsigned int frac = soundBuffer->frac;
            unsigned int step = (unsigned int)(((unsigned long long)soundBuffer->rate << 16) / gAudioEngineSpec.freq);
            int volume = soundBuffer->volume; // 0..SDL_MIX_MAXVOLUME (128)
            const unsigned char* data = (const unsigned char*)soundBuffer->data;
            bool stereo = soundBuffer->channels == 2;
            bool finished = false;

            for (int i = 0; i < outFrames; i++) {
                const unsigned char* src = data + frame * frameSize;
                int left;
                int right;
                if (bytesPerSample == 2) {
                    left = ((const Sint16*)src)[0];
                    right = stereo ? ((const Sint16*)src)[1] : left;
                } else {
                    left = ((int)((const Sint8*)src)[0]) << 8;
                    right = stereo ? (((int)((const Sint8*)src)[1]) << 8) : left;
                }

                accumulator[i * 2] += (left * volume) >> 7;
                accumulator[i * 2 + 1] += (right * volume) >> 7;

                frac += step;
                frame += frac >> 16;
                frac &= 0xFFFF;

                if (frame >= totalFrames) {
                    if (soundBuffer->looping) {
                        frame %= totalFrames;
                    } else {
                        finished = true;
                        frame = totalFrames;
                        break;
                    }
                }
            }

            soundBuffer->frac = frac;
            soundBuffer->pos = finished ? soundBuffer->size : frame * frameSize;
            if (finished) {
                // Match the original mixer: leave pos at the end of the data.
                soundBuffer->playing = false;
            }
        }
    }

    for (int i = 0; i < outFrames; i++) {
        int left = accumulator[i * 2];
        int right = accumulator[i * 2 + 1];
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        if (outChannels == 2) {
            out[i * 2] = (Sint16)left;
            out[i * 2 + 1] = (Sint16)right;
        } else {
            out[i] = (Sint16)((left + right) / 2);
        }
    }

    // Silence anything we didn't fill (oversized request).
    int written = outFrames * 2 * outChannels;
    if (written < length) {
        memset(stream + written, 0, length - written);
    }
}
#endif

static void audioEngineMixin(void* userData, Uint8* stream, int length)
{
#ifdef __PS2__
    if (gAudioEngineSpec.format == AUDIO_S16) {
        audioEngineMixinPs2(stream, length);
        return;
    }
#endif
    memset(stream, gAudioEngineSpec.silence, length);

    if (!GNW95_isActive) {
        return;
    }

    for (int index = 0; index < AUDIO_ENGINE_SOUND_BUFFERS; index++) {
        AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[index]);
        std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

        if (soundBuffer->active && soundBuffer->playing) {
            int srcFrameSize = soundBuffer->bitsPerSample / 8 * soundBuffer->channels;

            unsigned char buffer[1024];
            int pos = 0;
            while (pos < length) {
                int remaining = length - pos;
                if (remaining > sizeof(buffer)) {
                    remaining = sizeof(buffer);
                }

                // TODO: Make something better than frame-by-frame convertion.
                SDL_AudioStreamPut(soundBuffer->stream, (unsigned char*)soundBuffer->data + soundBuffer->pos, srcFrameSize);
                soundBuffer->pos += srcFrameSize;

                int bytesRead = SDL_AudioStreamGet(soundBuffer->stream, buffer, remaining);
                if (bytesRead == -1) {
                    break;
                }

                SDL_MixAudioFormat(stream + pos, buffer, gAudioEngineSpec.format, bytesRead, soundBuffer->volume);

                if (soundBuffer->pos >= soundBuffer->size) {
                    if (soundBuffer->looping) {
                        soundBuffer->pos %= soundBuffer->size;
                    } else {
                        soundBuffer->playing = false;
                        break;
                    }
                }

                pos += bytesRead;
            }
        }
    }
}

static bool audioEngineOpen()
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == -1) {
        return false;
    }

    SDL_AudioSpec desiredSpec;
    desiredSpec.freq = 22050;
    desiredSpec.format = AUDIO_S16;
    desiredSpec.channels = 2;
#ifdef __PS2__
    // Larger buffer so a slow frame doesn't starve the audio thread.
    desiredSpec.samples = 2048;
#else
    desiredSpec.samples = 1024;
#endif
    desiredSpec.callback = audioEngineMixin;

#ifdef __PS2__
    // Keep 16-bit samples so the direct mixer can be used; rate/channels may
    // still change and are handled by the mixer.
    gAudioEngineDeviceId = SDL_OpenAudioDevice(NULL, 0, &desiredSpec, &gAudioEngineSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    debug_printf("audio: device %d Hz, %d ch, format 0x%x, %d samples\n", gAudioEngineSpec.freq, gAudioEngineSpec.channels, gAudioEngineSpec.format, gAudioEngineSpec.samples);
#else
    gAudioEngineDeviceId = SDL_OpenAudioDevice(NULL, 0, &desiredSpec, &gAudioEngineSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
#endif
    // SDL reports failure as 0, not -1.
    if (gAudioEngineDeviceId == 0 || gAudioEngineDeviceId == (SDL_AudioDeviceID)-1) {
        gAudioEngineDeviceId = -1;
        return false;
    }

    SDL_PauseAudioDevice(gAudioEngineDeviceId, 0);

    return true;
}

#ifdef __PS2__
// On a PS2 opening audio loads the sound driver (audsrv) on the I/O
// processor and then waits for it to answer. If the driver can't start
// (a broken or unusual setup) that wait never ends and the game froze at
// boot. Open it on a helper thread instead and give up after a few seconds:
// the game then just runs without sound.
static SDL_sem* gAudioOpenDone = NULL;
static SDL_atomic_t gAudioOpenAbandoned;
static bool gAudioOpenResult = false;

static int audioEngineOpenThread(void*)
{
    bool ok = audioEngineOpen();
    if (SDL_AtomicGet(&gAudioOpenAbandoned) != 0) {
        // Too late: the game already went on without sound.
        if (ok) {
            SDL_CloseAudioDevice(gAudioEngineDeviceId);
            gAudioEngineDeviceId = -1;
        }
        return 0;
    }
    gAudioOpenResult = ok;
    SDL_SemPost(gAudioOpenDone);
    return 0;
}

bool audioEngineInit()
{
    gAudioOpenDone = SDL_CreateSemaphore(0);
    SDL_Thread* thread = gAudioOpenDone != NULL ? SDL_CreateThread(audioEngineOpenThread, "audio-open", NULL) : NULL;
    if (thread == NULL) {
        return audioEngineOpen();
    }
    if (SDL_SemWaitTimeout(gAudioOpenDone, 5000) != 0) {
        SDL_AtomicSet(&gAudioOpenAbandoned, 1);
        debug_printf("audio: the sound driver didn't answer in 5 s - playing without sound\n");
        SDL_DetachThread(thread);
        return false;
    }
    SDL_WaitThread(thread, NULL);
    SDL_DestroySemaphore(gAudioOpenDone);
    gAudioOpenDone = NULL;
    if (!gAudioOpenResult) {
        debug_printf("audio: couldn't open the sound device - playing without sound\n");
    }
    return gAudioOpenResult;
}
#else
bool audioEngineInit()
{
    return audioEngineOpen();
}
#endif

void audioEngineExit()
{
    if (audioEngineIsInitialized()) {
        SDL_CloseAudioDevice(gAudioEngineDeviceId);
        gAudioEngineDeviceId = -1;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void audioEnginePause()
{
    if (audioEngineIsInitialized()) {
        SDL_PauseAudioDevice(gAudioEngineDeviceId, 1);
    }
}

void audioEngineResume()
{
    if (audioEngineIsInitialized()) {
        SDL_PauseAudioDevice(gAudioEngineDeviceId, 0);
    }
}

int audioEngineCreateSoundBuffer(unsigned int size, int bitsPerSample, int channels, int rate)
{
    if (!audioEngineIsInitialized()) {
        return -1;
    }

    for (int index = 0; index < AUDIO_ENGINE_SOUND_BUFFERS; index++) {
        AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[index]);
        std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

        if (!soundBuffer->active) {
            soundBuffer->active = true;
            soundBuffer->size = size;
            soundBuffer->bitsPerSample = bitsPerSample;
            soundBuffer->channels = channels;
            soundBuffer->rate = rate;
            soundBuffer->volume = SDL_MIX_MAXVOLUME;
            soundBuffer->playing = false;
            soundBuffer->looping = false;
            soundBuffer->pos = 0;
#ifdef __PS2__
            soundBuffer->frac = 0;
#endif
            soundBuffer->data = malloc(size);
            soundBuffer->stream = SDL_NewAudioStream(bitsPerSample == 16 ? AUDIO_S16 : AUDIO_S8, channels, rate, gAudioEngineSpec.format, gAudioEngineSpec.channels, gAudioEngineSpec.freq);
            return index;
        }
    }

    return -1;
}

bool audioEngineSoundBufferRelease(int soundBufferIndex)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    soundBuffer->active = false;

    free(soundBuffer->data);
    soundBuffer->data = NULL;

    SDL_FreeAudioStream(soundBuffer->stream);
    soundBuffer->stream = NULL;

    return true;
}

bool audioEngineSoundBufferSetVolume(int soundBufferIndex, int volume)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    soundBuffer->volume = volume;

    return true;
}

bool audioEngineSoundBufferGetVolume(int soundBufferIndex, int* volumePtr)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    *volumePtr = soundBuffer->volume;

    return true;
}

bool audioEngineSoundBufferSetPan(int soundBufferIndex, int pan)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    // NOTE: Audio engine does not support sound panning. I'm not sure it's
    // even needed. For now this value is silently ignored.

    return true;
}

bool audioEngineSoundBufferPlay(int soundBufferIndex, unsigned int flags)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    soundBuffer->playing = true;

    if ((flags & AUDIO_ENGINE_SOUND_BUFFER_PLAY_LOOPING) != 0) {
        soundBuffer->looping = true;
    }

    return true;
}

bool audioEngineSoundBufferStop(int soundBufferIndex)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    soundBuffer->playing = false;

    return true;
}

bool audioEngineSoundBufferGetCurrentPosition(int soundBufferIndex, unsigned int* readPosPtr, unsigned int* writePosPtr)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    if (readPosPtr != NULL) {
        *readPosPtr = soundBuffer->pos;
    }

    if (writePosPtr != NULL) {
        *writePosPtr = soundBuffer->pos;

        if (soundBuffer->playing) {
            // 15 ms lead
            // See: https://docs.microsoft.com/en-us/previous-versions/windows/desktop/mt708925(v=vs.85)#remarks
            *writePosPtr += soundBuffer->rate / 150;
            *writePosPtr %= soundBuffer->size;
        }
    }

    return true;
}

bool audioEngineSoundBufferSetCurrentPosition(int soundBufferIndex, unsigned int pos)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    soundBuffer->pos = pos % soundBuffer->size;
#ifdef __PS2__
    soundBuffer->frac = 0;
#endif

    return true;
}

bool audioEngineSoundBufferLock(int soundBufferIndex, unsigned int writePos, unsigned int writeBytes, void** audioPtr1, unsigned int* audioBytes1, void** audioPtr2, unsigned int* audioBytes2, unsigned int flags)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    if (audioBytes1 == NULL) {
        return false;
    }

    if ((flags & AUDIO_ENGINE_SOUND_BUFFER_LOCK_FROM_WRITE_POS) != 0) {
        if (!audioEngineSoundBufferGetCurrentPosition(soundBufferIndex, NULL, &writePos)) {
            return false;
        }
    }

    if ((flags & AUDIO_ENGINE_SOUND_BUFFER_LOCK_ENTIRE_BUFFER) != 0) {
        writeBytes = soundBuffer->size;
    }

    if (writePos + writeBytes <= soundBuffer->size) {
        *(unsigned char**)audioPtr1 = (unsigned char*)soundBuffer->data + writePos;
        *audioBytes1 = writeBytes;

        if (audioPtr2 != NULL) {
            *audioPtr2 = NULL;
        }

        if (audioBytes2 != NULL) {
            *audioBytes2 = 0;
        }
    } else {
        unsigned int remainder = writePos + writeBytes - soundBuffer->size;
        *(unsigned char**)audioPtr1 = (unsigned char*)soundBuffer->data + writePos;
        *audioBytes1 = soundBuffer->size - writePos;

        if (audioPtr2 != NULL) {
            *(unsigned char**)audioPtr2 = (unsigned char*)soundBuffer->data;
        }

        if (audioBytes2 != NULL) {
            *audioBytes2 = writeBytes - (soundBuffer->size - writePos);
        }
    }

    // TODO: Mark range as locked.

    return true;
}

bool audioEngineSoundBufferUnlock(int soundBufferIndex, void* audioPtr1, unsigned int audioBytes1, void* audioPtr2, unsigned int audioBytes2)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    // TODO: Mark range as unlocked.

    return true;
}

bool audioEngineSoundBufferGetStatus(int soundBufferIndex, unsigned int* statusPtr)
{
    if (!audioEngineIsInitialized()) {
        return false;
    }

    if (!soundBufferIsValid(soundBufferIndex)) {
        return false;
    }

    AudioEngineSoundBuffer* soundBuffer = &(gAudioEngineSoundBuffers[soundBufferIndex]);
    std::lock_guard<std::recursive_mutex> lock(soundBuffer->mutex);

    if (!soundBuffer->active) {
        return false;
    }

    if (statusPtr == NULL) {
        return false;
    }

    *statusPtr = 0;

    if (soundBuffer->playing) {
        *statusPtr |= AUDIO_ENGINE_SOUND_BUFFER_STATUS_PLAYING;

        if (soundBuffer->looping) {
            *statusPtr |= AUDIO_ENGINE_SOUND_BUFFER_STATUS_LOOPING;
        }
    }

    return true;
}

} // namespace fallout
