#include "audio_capture.h"

#include <cstdio>

audio_async::audio_async(int len_ms) {
    m_len_ms = len_ms;

    m_running = false;
}

audio_async::~audio_async() {
    if (m_dev_id_in) {
        SDL_CloseAudioDevice(m_dev_id_in);
    }
}

// Initialize the audio capture device and SDL subsystem.
//
// Args:
//   capture_id  - Index of the capture device to open (0-based), or -1 to use
//                 the system default capture device. Valid indices are listed
//                 by calling init (devices are logged to stderr).
//   sample_rate - Desired sample rate in Hz (e.g., 16000 for Whisper compatibility).
//                 SDL may negotiate a different rate depending on the device; the
//                 actual rate is stored in m_sample_rate after this call.
//
// Returns true on success, false if SDL initialization or device opening fails.
//
// Side effects:
//   - Initializes SDL's audio subsystem.
//   - Opens the selected capture device and stores its handle in m_dev_id_in.
//   - Resizes the circular buffer (m_audio) to hold m_len_ms milliseconds of audio
//     at the obtained sample rate: buffer_samples = sample_rate * m_len_ms / 1000.
//     For example, with sample_rate=16000 and m_len_ms=1000, the buffer holds
//     16000 samples (~1 second of mono audio).
bool audio_async::init(int capture_id, int sample_rate) {
    // Set SDL logging priority so audio-related messages are visible
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    // Initialize SDL's audio subsystem; abort if it fails
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    // Configure SDL to use "medium" quality resampling when converting between sample rates
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_RESAMPLING_MODE, "medium", SDL_HINT_OVERRIDE);

    // Enumerate available capture devices for diagnostic logging
    {
        int nDevices = SDL_GetNumAudioDevices(SDL_TRUE);
        fprintf(stderr, "%s: found %d capture devices:\n", __func__, nDevices);
        for (int i = 0; i < nDevices; i++) {
            fprintf(stderr, "%s:    - Capture device #%d: '%s'\n", __func__, i, SDL_GetAudioDeviceName(i, SDL_TRUE));
        }
    }

    // Declare structs for the requested and actually-obtained audio specifications
    SDL_AudioSpec capture_spec_requested;
    SDL_AudioSpec capture_spec_obtained;

    // Zero out both structs to avoid uninitialized fields
    SDL_zero(capture_spec_requested);
    SDL_zero(capture_spec_obtained);

    // Configure the desired audio format:
    //   freq     - target sample rate (e.g., 16000 Hz for Whisper)
    //   format   - 32-bit floating-point PCM samples
    //   channels - mono audio
    //   samples  - number of samples per callback frame (1024 = ~63ms at 16kHz)
    capture_spec_requested.freq     = sample_rate;
    capture_spec_requested.format   = AUDIO_F32;
    capture_spec_requested.channels = 1;
    capture_spec_requested.samples  = 1024;

    // Set the callback that SDL will invoke each time a new frame of audio is captured.
    // The lambda casts the userdata pointer back to an audio_async* and forwards the call.
    capture_spec_requested.callback = [](void * userdata, uint8_t * stream, int len) {
        audio_async * audio = (audio_async *) userdata;
        audio->callback(stream, len);
    };
    // Pass "this" as userdata so the callback can reach this instance
    capture_spec_requested.userdata = this;

    // Open the audio capture device: either a specific device by index, or the system default
    if (capture_id >= 0) {
        fprintf(stderr, "%s: attempt to open capture device %d : '%s' ...\n", __func__, capture_id, SDL_GetAudioDeviceName(capture_id, SDL_TRUE));
        m_dev_id_in = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(capture_id, SDL_TRUE), SDL_TRUE, &capture_spec_requested, &capture_spec_obtained, 0);
    } else {
        fprintf(stderr, "%s: attempt to open default capture device ...\n", __func__);
        m_dev_id_in = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &capture_spec_requested, &capture_spec_obtained, 0);
    }

    // Check if the device was opened successfully
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: couldn't open an audio device for capture: %s!\n", __func__, SDL_GetError());
        m_dev_id_in = 0;

        return false;
    } else {
        // Log the actual audio spec SDL provided (may differ from what was requested)
        fprintf(stderr, "%s: obtained spec for input device (SDL Id = %d):\n", __func__, m_dev_id_in);
        fprintf(stderr, "%s:     - sample rate:       %d\n",                   __func__, capture_spec_obtained.freq);
        fprintf(stderr, "%s:     - format:            %d (required: %d)\n",    __func__, capture_spec_obtained.format,
                capture_spec_requested.format);
        fprintf(stderr, "%s:     - channels:          %d (required: %d)\n",    __func__, capture_spec_obtained.channels,
                capture_spec_requested.channels);
        fprintf(stderr, "%s:     - samples per frame: %d\n",                   __func__, capture_spec_obtained.samples);
    }

    // Store the actual sample rate SDL provided (used later for time-to-sample conversions)
    m_sample_rate = capture_spec_obtained.freq;

    // Resize the circular buffer to hold m_len_ms milliseconds of audio at the obtained sample rate
    m_audio.resize((m_sample_rate*m_len_ms)/1000);

    return true;
}

bool audio_async::resume() {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to resume!\n", __func__);
        return false;
    }

    if (m_running) {
        fprintf(stderr, "%s: already running!\n", __func__);
        return false;
    }

    SDL_PauseAudioDevice(m_dev_id_in, 0);

    m_running = true;

    return true;
}

bool audio_async::pause() {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to pause!\n", __func__);
        return false;
    }

    if (!m_running) {
        fprintf(stderr, "%s: already paused!\n", __func__);
        return false;
    }

    SDL_PauseAudioDevice(m_dev_id_in, 1);

    m_running = false;

    return true;
}

bool audio_async::clear() {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to clear!\n", __func__);
        return false;
    }

    if (!m_running) {
        fprintf(stderr, "%s: not running!\n", __func__);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_audio_pos = 0;
        m_audio_len = 0;
    }

    return true;
}

// Consume the oldest ms milliseconds of buffered audio, preserving newer
// samples that arrived during inference.  If ms >= the buffered duration the
// effect is the same as clear().
bool audio_async::advance(int ms) {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to advance!\n", __func__);
        return false;
    }

    if (!m_running) {
        fprintf(stderr, "%s: not running!\n", __func__);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        size_t n_samples = (m_sample_rate * ms) / 1000;
        if (n_samples >= m_audio_len) {
            // Consumed everything that's buffered — equivalent to clear()
            m_audio_pos = 0;
            m_audio_len = 0;
        } else {
            // Shrink from the front: only the length counter changes.
            // m_audio_pos (write head) stays where it is so incoming audio
            // continues to be written at the correct offset.  The next get()
            // will clamp its read to the new m_audio_len and therefore skips
            // the portion we just "consumed".
            m_audio_len -= n_samples;
        }
    }

    return true;
}

// SDL audio callback — invoked by SDL on a timer (roughly every 1024 samples,
// or ~63ms at 16kHz) to process each captured frame. The callback runs on SDL's
// audio thread, separate from the main thread, so all access to shared state
// must be synchronized via the mutex.
//
// The capture device continuously feeds raw PCM samples regardless of audio
// content — silence is simply a sequence of values near zero, not an absence
// of data. Each frame is written into the circular buffer (m_audio), which
// wraps around and overwrites the oldest data once full.
//
// Args:
//   stream - Pointer to the frame of raw audio samples (32-bit float PCM)
//            provided by SDL. This is the incoming data to be copied.
//   len    - Length of the stream buffer in bytes (not samples). Converted
//            to a sample count by dividing by sizeof(float).
//
// Note: This function must be fast and non-blocking, as it runs on SDL's
// real-time audio thread. Prolonged blocking here will cause audio glitches.
void audio_async::callback(uint8_t * stream, int len) {
    // Guard: if capture is paused, discard the incoming data immediately
    if (!m_running) {
        return;
    }

    // Convert the byte length to a sample count (each sample is a 32-bit float)
    size_t n_samples = len / sizeof(float);

    // Safety clamp: if the incoming chunk is larger than our circular buffer,
    // trim it to fit. Adjust the stream pointer to the start of the trimmed portion.
    if (n_samples > m_audio.size()) {
        n_samples = m_audio.size();

        stream += (len - (n_samples * sizeof(float)));
    }

    //fprintf(stderr, "%s: %zu samples, pos %zu, len %zu\n", __func__, n_samples, m_audio_pos, m_audio_len);

    // Thread-safe section: lock the mutex before touching shared buffer state
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Check if writing these samples would wrap around the end of the circular buffer
        if (m_audio_pos + n_samples > m_audio.size()) {
            // Calculate how many samples fit before reaching the buffer end
            const size_t n0 = m_audio.size() - m_audio_pos;

            // First memcpy: fill from the current position to the end of the buffer
            memcpy(&m_audio[m_audio_pos], stream, n0 * sizeof(float));
            // Second memcpy: wrap around to the beginning for the remaining samples
            memcpy(&m_audio[0], stream + n0 * sizeof(float), (n_samples - n0) * sizeof(float));
        } else {
            // No wrap needed: copy all samples contiguously at the current position
            memcpy(&m_audio[m_audio_pos], stream, n_samples * sizeof(float));
        }
        // Advance the write position, wrapping back to 0 via modulo if it passed the end
        m_audio_pos = (m_audio_pos + n_samples) % m_audio.size();
        // Track total samples buffered, capping at the buffer's capacity (oldest data is overwritten)
        m_audio_len = std::min(m_audio_len + n_samples, m_audio.size());
    }
}

void audio_async::get(int ms, std::vector<float> & result) {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to get audio from!\n", __func__);
        return;
    }

    if (!m_running) {
        fprintf(stderr, "%s: not running!\n", __func__);
        return;
    }

    result.clear();

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (ms <= 0) {
            ms = m_len_ms;
        }

        size_t n_samples = (m_sample_rate * ms) / 1000;
        if (n_samples > m_audio_len) {
            n_samples = m_audio_len;
        }

        result.resize(n_samples);

        int s0 = m_audio_pos - n_samples;
        if (s0 < 0) {
            s0 += m_audio.size();
        }

        if (s0 + n_samples > m_audio.size()) {
            const size_t n0 = m_audio.size() - s0;

            memcpy(result.data(), &m_audio[s0], n0 * sizeof(float));
            memcpy(&result[n0], &m_audio[0], (n_samples - n0) * sizeof(float));
        } else {
            memcpy(result.data(), &m_audio[s0], n_samples * sizeof(float));
        }
    }
}

bool sdl_poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                {
                    return false;
                }
            default:
                break;
        }
    }

    return true;
}
