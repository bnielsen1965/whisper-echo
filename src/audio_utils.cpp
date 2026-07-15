// Audio utilities (pruned from whisper.cpp examples/common.cpp)

#define _USE_MATH_DEFINES // for M_PI

#include "audio_utils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

void high_pass_filter(std::vector<float> & data, float cutoff, float sample_rate) {
    const float rc = 1.0f / (2.0f * M_PI * cutoff);
    const float dt = 1.0f / sample_rate;
    const float alpha = dt / (rc + dt);

    float y = data[0];

    for (size_t i = 1; i < data.size(); i++) {
        y = alpha * (y + data[i] - data[i - 1]);
        data[i] = y;
    }
}

bool vad_simple(std::vector<float> & pcmf32, int sample_rate, int last_ms, float vad_thold, float freq_thold, bool verbose) {
    const int n_samples      = pcmf32.size();
    const int n_samples_last = (sample_rate * last_ms) / 1000;

    if (n_samples_last >= n_samples) {
        // not enough samples - assume no speech
        return false;
    }

    if (freq_thold > 0.0f) {
        high_pass_filter(pcmf32, freq_thold, sample_rate);
    }

    float energy_all  = 0.0f;
    float energy_last = 0.0f;

    for (int i = 0; i < n_samples; i++) {
        energy_all += fabsf(pcmf32[i]);

        if (i >= n_samples - n_samples_last) {
            energy_last += fabsf(pcmf32[i]);
        }
    }

    energy_all  /= n_samples;
    energy_last /= n_samples_last;

    if (verbose) {
        fprintf(stderr, "%s: energy_all: %f, energy_last: %f, vad_thold: %f, freq_thold: %f\n", __func__, energy_all, energy_last, vad_thold, freq_thold);
    }

    if (energy_last > vad_thold*energy_all) {
        return false;
    }

    return true;
}

//  500 -> 00:05.000
// 6000 -> 01:00.000
std::string to_timestamp(int64_t t, bool comma) {
    int64_t msec = t * 10;
    int64_t hr = msec / (1000 * 60 * 60);
    msec = msec - hr * (1000 * 60 * 60);
    int64_t min = msec / (1000 * 60);
    msec = msec - min * (1000 * 60);
    int64_t sec = msec / 1000;
    msec = msec - sec * 1000;

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d%s%03d", (int) hr, (int) min, (int) sec, comma ? "," : ".", (int) msec);

    return std::string(buf);
}

// ──────────────────────────────────────────────────────────────────────────
// Streaming Silero VAD state machine
// ──────────────────────────────────────────────────────────────────────────

bool stream_vad_init(stream_vad_state & state,
                     const std::string & model_path,
                     int n_threads,
                     bool use_gpu,
                     int gpu_device) {
    // Check if model file exists
    std::ifstream f(model_path);
    if (!f.good()) {
        fprintf(stderr, "%s: VAD model '%s' not found, falling back to vad_simple\n",
                __func__, model_path.c_str());
        state.initialized = false;
        return false;
    }

    struct whisper_vad_context_params ctx_params = whisper_vad_default_context_params();
    ctx_params.n_threads = n_threads;
    // GPU VAD is forced to CPU inside whisper.cpp (whisper_vad_init_context
    // hardcodes use_gpu = false).  If model weights are loaded onto GPU but the
    // scheduler is created without a GPU backend, allocation aborts with:
    //   "pre-allocated tensor … in a buffer (Vulkan0) that cannot run the
    //    operation (NONE)"
    // So always keep the VAD model on CPU regardless of the caller's flag.
    ctx_params.use_gpu    = false;
    ctx_params.gpu_device = 0;

    state.vctx = whisper_vad_init_from_file_with_params(model_path.c_str(), ctx_params);
    if (state.vctx == nullptr) {
        fprintf(stderr, "%s: failed to load VAD model '%s', falling back to vad_simple\n",
                __func__, model_path.c_str());
        state.initialized = false;
        return false;
    }

    state.initialized = true;
    state.in_speech = false;
    state.silence_ms = 0;
    state.speech_ms = 0;

    fprintf(stderr, "%s: Silero VAD initialized (model: %s)\n",
            __func__, model_path.c_str());
    return true;
}

stream_vad_state::Result stream_vad_state::feed_chunk(const float * samples, int n_samples) {
    if (vctx == nullptr || !initialized) {
        return Result::SILENCE;
    }

    const int chunk_ms = (n_samples * 1000) / sample_rate;

    // Apply gain to compensate for low microphone levels. If gain > 1.0,
    // scale into a temporary buffer so the original samples are untouched.
    std::vector<float> scaled;
    const float * input_samples = samples;
    if (vad_gain != 1.0f) {
        scaled.resize(n_samples);
        for (int i = 0; i < n_samples; i++) {
            scaled[i] = samples[i] * vad_gain;
        }
        input_samples = scaled.data();
    }

    // Run VAD inference without resetting LSTM state (streaming continuity).
    // This function returns true on success — it does NOT indicate speech.
    // The actual probability is stored in vctx->probs.
    if (!whisper_vad_detect_speech_no_reset(vctx, input_samples, n_samples)) {
        // Inference failed — treat as silence
        return Result::SILENCE;
    }

    // Read the speech probability from the last chunk
    int     n_probs = whisper_vad_n_probs(vctx);
    float * probs   = whisper_vad_probs(vctx);
    bool    is_speech = (n_probs > 0 && probs[n_probs - 1] >= vad_threshold);

    if (is_speech) {
        silence_ms = 0;
        speech_ms += chunk_ms;

        if (!in_speech) {
            // Transition: SILENCE -> SPEECH
            in_speech = true;
            speech_ms = chunk_ms;
            captured_ms = chunk_ms;
        } else {
            captured_ms += chunk_ms;
        }
        return Result::SPEECH;
    }

    // This chunk is classified as silence/noise
    if (!in_speech) {
        // Still in silence, nothing interesting
        speech_ms = 0;
        return Result::SILENCE;
    }

    // Was in speech, now silence — accumulate to confirm end
    silence_ms += chunk_ms;
    captured_ms += chunk_ms;  // include intra-utterance silence in total span
    if (silence_ms >= min_silence_ms && speech_ms >= min_speech_ms) {
        // Confirmed end of speech
        in_speech = false;
        return Result::END;
    }
    // Not enough silence yet — keep in SPEECH state to keep accumulating
    return Result::SPEECH;
}

void stream_vad_reset(stream_vad_state & state) {
    state.in_speech = false;
    state.silence_ms = 0;
    state.speech_ms = 0;
    state.captured_ms = 0;
    if (state.vctx != nullptr) {
        whisper_vad_reset_state(state.vctx);
    }
}

void stream_vad_free(stream_vad_state & state) {
    if (state.vctx != nullptr) {
        whisper_vad_free(state.vctx);
        state.vctx = nullptr;
    }
    state.initialized = false;
}
