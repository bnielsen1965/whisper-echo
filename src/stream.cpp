// Real-time speech recognition of input from a microphone (VAD mode only)
//
// Voice Activity Detection: only transcribes when speech is detected.
//
#include "audio_capture.h"
#include "audio_utils.h"
#include "commands.h"
#include "uinput.h"
#include "whisper.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// Suppress whisper.cpp INFO logs, keeping only WARN and ERROR.
// whisper_log_set() replaces the global log callback, so it intercepts
// both whisper-level and ggml-level log output (it calls ggml_log_set() internally).
static void cb_log_suppress_info(enum ggml_log_level level, const char *text, void *user_data) {
    (void)user_data;
    if (level >= GGML_LOG_LEVEL_WARN) {
        fputs(text, stderr);
        fflush(stderr);
    }
}

// command-line parameters
struct whisper_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t length_ms  = 60000; // circular buffer depth — cost is memory only (64KB/sec at 16kHz)
    int32_t capture_id = -1;
    int32_t beam_size  = -1;
    int32_t audio_ctx  = 0;

    float vad_thold    = 0.6f;
    float freq_thold   = 100.0f;
    float vad_gain     = 1.0f;    // multiplier applied to audio before VAD

    bool translate     = false;
    bool no_fallback   = false;
    bool print_special = false;
    bool tinydiarize   = false;
    bool save_audio    = false; // save audio to wav file
    bool use_gpu        = true;
    bool flash_attn     = true;
    int32_t gpu_device  = 0;
    bool uinput_enabled = false; // type transcribed text via uinput virtual keyboard

    std::string language = "en";
    std::string model    = "models/ggml-base.en.bin";
    std::string fname_out;

    std::string vad_model = "models/for-tests-silero-v6.2.0-ggml.bin";
    bool use_silero_vad   = true;
    bool print_details    = false; // print transcription headers and timestamps
    bool print_status     = true;  // show status indicator (idle, listening, ...)

    std::string commands_file;  // Path to command.json for voice commands
};

void whisper_print_usage(int argc, char ** argv, const whisper_params & params);

static bool whisper_params_parse(int argc, char ** argv, whisper_params & params) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            whisper_print_usage(argc, argv, params);
            exit(0);
        }
        else if (arg == "-t"    || arg == "--threads")      { params.n_threads   = std::stoi(argv[++i]); }
        else if (arg == "-l"    || arg == "--language")     { params.language    = argv[++i]; }
        else if (arg == "-m"    || arg == "--model")        { params.model       = argv[++i]; }
        else if (arg == "-f"    || arg == "--file")         { params.fname_out   = argv[++i]; }
        else if (arg == "-c"    || arg == "--capture")      { params.capture_id  = std::stoi(argv[++i]); }
        else if (arg == "-bs"   || arg == "--beam-size")    { params.beam_size   = std::stoi(argv[++i]); }
        else if (arg == "-ac"   || arg == "--audio-ctx")    { params.audio_ctx   = std::stoi(argv[++i]); }
        else if (arg == "--length")                         { params.length_ms   = std::stoi(argv[++i]); }
        else if (arg == "-vth"  || arg == "--vad-thold")    { params.vad_thold   = std::stof(argv[++i]); }
        else if (arg == "-fth"  || arg == "--freq-thold")   { params.freq_thold  = std::stof(argv[++i]); }
        else if (arg == "-vg"   || arg == "--vad-gain")     { params.vad_gain    = std::stof(argv[++i]); }
        else if (arg == "-tr"   || arg == "--translate")    { params.translate   = true; }
        else if (arg == "-nf"   || arg == "--no-fallback")  { params.no_fallback = true; }
        else if (arg == "-ps"   || arg == "--print-special"){ params.print_special = true; }
        else if (arg == "-tdrz" || arg == "--tinydiarize")  { params.tinydiarize = true; }
        else if (arg == "-sa"   || arg == "--save-audio")   { params.save_audio  = true; }
        else if (arg == "-ng"   || arg == "--no-gpu")       { params.use_gpu     = false; }
        else if (arg == "-gd"   || arg == "--gpu-device")   { params.gpu_device  = std::stoi(argv[++i]); }
        else if (arg == "-nfa"  || arg == "--no-flash-attn"){ params.flash_attn  = false; }
        else if (arg == "-vm"   || arg == "--vad-model")    { params.vad_model   = argv[++i]; }
        else if (arg == "-nsv"  || arg == "--no-silero-vad"){ params.use_silero_vad = false; }
        else if (arg == "-cm"   || arg == "--commands")      { params.commands_file = argv[++i]; }
        else if (arg == "-d"    || arg == "--detail")        { params.print_details = true; }
        else if (arg == "-ns"   || arg == "--no-status")     { params.print_status = false; }
        else if (arg == "-ui"   || arg == "--uinput")        { params.uinput_enabled = true; }
        else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            whisper_print_usage(argc, argv, params);
            exit(0);
        }
    }

    return true;
}

void whisper_print_usage(int /*argc*/, char ** argv, const whisper_params & params) {
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -h,        --help            show this help message and exit\n");
    fprintf(stderr, "  -t N,      --threads N      [%-7d] number of threads to use during computation\n",    params.n_threads);
    fprintf(stderr, "            --length N         [%-7d] audio length in milliseconds\n",                   params.length_ms);
    fprintf(stderr, "  -c ID,     --capture ID     [%-7d] capture device ID\n",                              params.capture_id);
    fprintf(stderr, "  -bs N,     --beam-size N    [%-7d] beam size for beam search\n",                      params.beam_size);
    fprintf(stderr, "  -ac N,     --audio-ctx N    [%-7d] audio context size (0 - all)\n",                   params.audio_ctx);
    fprintf(stderr, "  -vth N,    --vad-thold N    [%-7.2f] voice activity detection threshold (only with --no-silero-vad)\n", params.vad_thold);
    fprintf(stderr, "  -fth N,    --freq-thold N   [%-7.2f] high-pass frequency cutoff (only with --no-silero-vad)\n",       params.freq_thold);
    fprintf(stderr, "  -vg N,     --vad-gain N     [%-7.2f] audio gain multiplier for VAD input\n",          params.vad_gain);
    fprintf(stderr, "  -tr,       --translate      [%-7s] translate from source language to english\n",      params.translate ? "true" : "false");
    fprintf(stderr, "  -nf,       --no-fallback     do not use temperature fallback while decoding\n");
    fprintf(stderr, "  -ps,       --print-special  [%-7s] print special tokens\n",                           params.print_special ? "true" : "false");
    fprintf(stderr, "  -l LANG,   --language LANG  [%-7s] spoken language\n",                                params.language.c_str());
    fprintf(stderr, "  -m FNAME,  --model FNAME    [%-7s] model path\n",                                     params.model.c_str());
    fprintf(stderr, "  -f FNAME,  --file FNAME     [%-7s] text output file name\n",                          params.fname_out.c_str());
    fprintf(stderr, "  -tdrz,     --tinydiarize    [%-7s] enable tinydiarize (requires a tdrz model)\n",     params.tinydiarize ? "true" : "false");
    fprintf(stderr, "  -sa,       --save-audio     [%-7s] save the recorded audio to a file\n",              params.save_audio ? "true" : "false");
    fprintf(stderr, "  -ng,       --no-gpu         disable GPU inference\n");
    fprintf(stderr, "  -gd ID,    --gpu-device ID  [%-7d] GPU device ID to use\n",                           params.gpu_device);
    fprintf(stderr, "  -nfa,      --no-flash-attn  disable flash attention during inference\n");
    fprintf(stderr, "  -vm FNAME, --vad-model FNAME[%-7s] Silero VAD model path\n",                          params.vad_model.c_str());
    fprintf(stderr, "  -nsv,      --no-silero-vad   disable Silero VAD, use energy-based vad_simple\n");
    fprintf(stderr, "  -d,        --detail         [%-7s] print transcription details (headers, timestamps)\n", params.print_details ? "true" : "false");
    fprintf(stderr, "  -ns,       --no-status      disable status indicator\n");
    fprintf(stderr, "  -cm FNAME, --commands FNAME [none] voice command configuration file\n");
    fprintf(stderr, "  -ui,       --uinput         type transcribed text via uinput virtual keyboard\n");
    fprintf(stderr, "\n");
}

int main(int argc, char ** argv) {
    // Suppress whisper.cpp INFO logs before any whisper/ggml operations
    whisper_log_set(cb_log_suppress_info, NULL);

    ggml_backend_load_all();

    whisper_params params;

    if (whisper_params_parse(argc, argv, params) == false) {
        return 1;
    }

    // Warn if VAD-simple flags are set but Silero VAD is active
    if (params.use_silero_vad && (params.vad_thold != 0.6f || params.freq_thold != 100.0f || params.vad_gain != 1.0f)) {
        fprintf(stderr, "warning: --vad-thold, --freq-thold, and --vad-gain are ignored when Silero VAD is active\n");
    }

    // Load voice commands (defaults are always available)
    if (!params.commands_file.empty()) {
        if (!CommandRegistry::instance().load_from_file(params.commands_file)) {
            fprintf(stderr, "warning: failed to load commands from '%s', using defaults\n",
                    params.commands_file.c_str());
        }
    }

    const int n_samples_len  = (1e-3*params.length_ms)*WHISPER_SAMPLE_RATE;
    const int n_samples_30s  = (1e-3*30000.0         )*WHISPER_SAMPLE_RATE;

    // init audio
    audio_async audio(params.length_ms);
    if (!audio.init(params.capture_id, WHISPER_SAMPLE_RATE)) {
        fprintf(stderr, "%s: audio.init() failed!\n", __func__);
        return 1;
    }

    audio.resume();

    // whisper init
    if (params.language != "auto" && whisper_lang_id(params.language.c_str()) == -1){
        fprintf(stderr, "error: unknown language '%s'\n", params.language.c_str());
        whisper_print_usage(argc, argv, params);
        exit(0);
    }

    struct whisper_context_params cparams = whisper_context_default_params();

    cparams.use_gpu    = params.use_gpu;
    cparams.flash_attn = params.flash_attn;

    struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);
    if (ctx == nullptr) {
        fprintf(stderr, "error: failed to initialize whisper context\n");
        return 2;
    }

    std::vector<float> pcmf32(n_samples_30s, 0.0f);

    // print some info about the processing
    {
        fprintf(stderr, "\n");
        if (!whisper_is_multilingual(ctx)) {
            if (params.language != "en" || params.translate) {
                params.language = "en";
                params.translate = false;
                fprintf(stderr, "%s: WARNING: model is not multilingual, ignoring language and translation options\n", __func__);
            }
        }
        fprintf(stderr, "%s: audio length = %.1f sec, %d threads, lang = %s, task = %s, vad_thold = %.2f, freq_thold = %.2f ...\n",
                __func__,
                float(n_samples_len)/WHISPER_SAMPLE_RATE,
                params.n_threads,
                params.language.c_str(),
                params.translate ? "translate" : "transcribe",
                params.vad_thold,
                params.freq_thold);

        fprintf(stderr, "%s: using VAD, will transcribe on speech activity\n", __func__);
        fprintf(stderr, "\n");
    }

    // Initialize streaming VAD
    stream_vad_state vad_state;
    bool use_silero = false;
    if (params.use_silero_vad) {
        use_silero = stream_vad_init(vad_state, params.vad_model, params.n_threads, params.use_gpu, params.gpu_device);
        if (!use_silero) {
            fprintf(stderr, "%s: Silero VAD not available, using vad_simple fallback\n", __func__);
        }
    }
    vad_state.vad_gain = params.vad_gain;

    // Initialize uinput device if requested
    int uinput_fd = -1;
    if (params.uinput_enabled) {
        uinput_fd = uinput::setup();
        if (uinput_fd < 0) {
            fprintf(stderr, "%s: failed to initialize uinput, disabling uinput output\n", __func__);
            params.uinput_enabled = false;
        }
    }

    int n_iter = 0;
    bool is_running = true;

    std::ofstream fout;
    if (params.fname_out.length() > 0) {
        fout.open(params.fname_out);
        if (!fout.is_open()) {
            fprintf(stderr, "%s: failed to open output file '%s'!\n", __func__, params.fname_out.c_str());
            return 1;
        }
    }

    wav_writer wavWriter;
    if (params.save_audio) {
        time_t now = time(0);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", localtime(&now));
        std::string filename = std::string(buffer) + ".wav";

        wavWriter.open(filename, WHISPER_SAMPLE_RATE, 16, 1);
    }

    // Status indicator states
    enum class Status { IDLE, LISTENING, CAPTURING, PROCESSING };
    Status last_status = Status::IDLE;
    auto print_status = [&last_status, &params](Status status) {
        // Only redraw when the status actually changes to avoid flooding stdout
        if (status == last_status) return;
        last_status = status;

        if (!params.print_status) return;

        const char * label;
        switch (status) {
            case Status::IDLE:
                label = "idle";
                break;
            case Status::LISTENING:
                label = "listening";
                break;
            case Status::CAPTURING:
                label = "capturing";
                break;
            case Status::PROCESSING:
                label = "processing";
                break;
        }
        // \r to overwrite the same line; \33[K to clear to end of line
        printf("\r\33[K [%s%s]", label, g_print_paused.load() ? " (p)" : "");
        fflush(stdout);
    };

    const auto t_start = std::chrono::high_resolution_clock::now();

    // Helper to run transcription and print results (shared by both VAD modes)
    auto transcribe_and_print = [&]() {
        print_status(Status::PROCESSING);

        whisper_full_params wparams = whisper_full_default_params(
            params.beam_size > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);

        wparams.print_progress   = false;
        wparams.print_special    = params.print_special;
        wparams.print_realtime   = false;
        wparams.print_timestamps = true;
        wparams.translate        = params.translate;
        wparams.single_segment   = false;
        wparams.max_tokens       = 0;
        wparams.language         = params.language.c_str();
        wparams.n_threads        = params.n_threads;
        wparams.beam_search.beam_size = params.beam_size;
        wparams.audio_ctx        = params.audio_ctx;
        wparams.tdrz_enable      = params.tinydiarize;
        wparams.temperature_inc  = params.no_fallback ? 0.0f : wparams.temperature_inc;

        if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
            fprintf(stderr, "%s: failed to process audio\n", argv[0]);
            return false;
        }

        {
            bool printed_any = false;

            auto ensure_off_status_line = [&printed_any, &params]() {
                if (!printed_any && params.print_status) {
                    printf("\n");
                    printed_any = true;
                }
            };

            if (params.print_details) {
                const int64_t t1 = (std::chrono::high_resolution_clock::now() - t_start).count() / 1000000;
                const int64_t t0 = std::max(int64_t(0), t1 - (int64_t)(pcmf32.size() * 1000.0 / WHISPER_SAMPLE_RATE));

                ensure_off_status_line();
                printf("### Transcription %d | t0 = %d ms | t1 = %d ms\n", n_iter, (int)t0, (int)t1);
                printf("\n");
            }

            const int n_segments = whisper_full_n_segments(ctx);
            for (int i = 0; i < n_segments; ++i) {
                const char * text = whisper_full_get_segment_text(ctx, i);

                // --- Command matching ---
                MatchResult result = CommandRegistry::instance().match(text);
                if (result.command != nullptr) {
                    switch (result.command->action) {
                        case CommandAction::PAUSE_PRINT:
                            g_print_paused.store(true);
                            break;
                        case CommandAction::RESUME_PRINT:
                            g_print_paused.store(false);
                            break;
                        case CommandAction::NEW_LINE:
                            ensure_off_status_line();
                            printf("\n");
                            fflush(stdout);
                            if (params.fname_out.length() > 0) {
                                fout << "\n";
                            }
                            if (params.uinput_enabled && uinput_fd >= 0) {
                                uinput::type_newline(uinput_fd);
                            }
                            break;
                        case CommandAction::BACKSPACE:
                            if (!result.params.empty()) {
                                int count = result.params[0];
                                if (params.uinput_enabled && uinput_fd >= 0) {
                                    uinput::type_backspaces(uinput_fd, count);
                                }
                            }
                            break;
                        case CommandAction::SPACE:
                            if (!result.params.empty()) {
                                int count = result.params[0];
                                if (params.uinput_enabled && uinput_fd >= 0) {
                                    uinput::type_spaces(uinput_fd, count);
                                }
                            }
                            break;
                    }
                    // Command executed — don't print the segment text
                    continue;
                }

                // Build output string (always written to file; printed only if not paused)
                const int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                const int64_t t1 = whisper_full_get_segment_t1(ctx, i);

                // Strip leading space from segment text (whisper adds one)
                const char * segment_text = text;
                if (*text == ' ') {
                    segment_text = text + 1;
                }

                std::string output;
                if (params.print_details) {
                    output = "[" + to_timestamp(t0, false) + " --> " +
                        to_timestamp(t1, false) + "]  " + segment_text;
                } else {
                    output = segment_text;
                }

                if (whisper_full_get_segment_speaker_turn_next(ctx, i)) {
                    output += " [SPEAKER_TURN]";
                }

                output += "\n";

                if (!g_print_paused.load()) {
                    ensure_off_status_line();
                    printf("%s", output.c_str());
                    fflush(stdout);
                }

                if (params.fname_out.length() > 0) {
                    fout << output;
                }

                // Send to uinput device if enabled — strip the trailing newline
                // and append a space so segments stay on one line until the
                // "new line" command types Enter.
                if (params.uinput_enabled && uinput_fd >= 0) {
                    std::string uinput_text = output;
                    if (!uinput_text.empty() && uinput_text.back() == '\n') {
                        uinput_text.pop_back();
                    }
                    uinput_text += ' ';
                    uinput::type_string(uinput_fd, uinput_text);
                }
            }

            if (params.fname_out.length() > 0) {
                fout << std::endl;
            }

            if (params.print_details) {
                ensure_off_status_line();
                printf("### Transcription %d END\n", n_iter);
            }

            // Force status redraw on next main loop iteration
            last_status = Status::IDLE;
        }

        // Save audio before advancing buffer
        if (params.save_audio) {
            wavWriter.write(pcmf32.data(), pcmf32.size());
        }

        pcmf32.clear();
        ++n_iter;
        fflush(stdout);

        return true;
    };

    print_status(Status::IDLE);

    // VAD chunk size for streaming Silero mode (~30ms at 16kHz)
    const int vad_chunk_ms = 30;

    // main audio loop
    while (is_running) {
        is_running = sdl_poll_events();
        if (!is_running) {
            break;
        }

        if (use_silero) {
            // ── Silero VAD streaming mode ──────────────────────────
            // Feed small chunks from the tail of the circular buffer
            // to the neural VAD state machine.

            std::vector<float> chunk;
            audio.get(vad_chunk_ms, chunk);

            if (chunk.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(vad_chunk_ms));
                continue;
            }

            auto result = vad_state.feed_chunk(chunk.data(), (int)chunk.size());

            switch (result) {
                case stream_vad_state::Result::SPEECH:
                    print_status(Status::CAPTURING);
                    break;

                case stream_vad_state::Result::END: {
                    print_status(Status::PROCESSING);

                    // Extract the speech segment from the circular buffer.
                    // Add a pre-speech buffer to capture audio spoken before the
                    // VAD detected speech — VAD has ~50-100ms of latency.
                    {
                        const int pre_buffer_ms = 200;
                        int speech_duration_ms = vad_state.speech_ms + vad_state.min_silence_ms + pre_buffer_ms;
                        if (speech_duration_ms > params.length_ms) {
                            speech_duration_ms = params.length_ms;
                        }
                        if (speech_duration_ms < 100) {
                            speech_duration_ms = 100;
                        }

                        audio.get(speech_duration_ms, pcmf32);

                        if (!pcmf32.empty() && transcribe_and_print()) {
                            // Advance buffer past the transcribed audio
                            audio.advance(speech_duration_ms);
                        }
                    }

                    // Reset VAD state for the next utterance
                    stream_vad_reset(vad_state);
                    break;
                }

                case stream_vad_state::Result::SILENCE:
                    print_status(Status::LISTENING);
                    break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(vad_chunk_ms));

        } else {
            // ── Fallback: energy-based vad_simple mode ─────────────
            print_status(Status::LISTENING);

            audio.get(params.length_ms, pcmf32);

            if (pcmf32.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (::vad_simple(pcmf32, WHISPER_SAMPLE_RATE, 1000,
                             params.vad_thold, params.freq_thold, false)) {
                // Speech followed by silence — transcribe
                if (!transcribe_and_print()) {
                    return 6;
                }

                // Advance the buffer past the transcribed audio
                audio.advance(params.length_ms);
            } else {
                // Still speaking or too noisy — check again
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
    }

    // Cleanup
    if (uinput_fd >= 0) {
        uinput::teardown(uinput_fd);
    }
    stream_vad_free(vad_state);
    audio.pause();

    whisper_print_timings(ctx);
    whisper_free(ctx);

    return 0;
}
