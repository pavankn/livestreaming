#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <algorithm>   // ✅ Required for std::find_if
#include <cctype>      // ✅ Required for std::isspace
#include <string>

// Version 2

// FFmpeg C headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <libavutil/timestamp.h>
}

// Global stop signal
volatile std::sig_atomic_t stop_signal = 0;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nCTRL-C received, stopping..." << std::endl;
        stop_signal = 1;
    }
}

// Helper: human-readable FFmpeg error
std::string get_av_error_string(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

// Stream copying thread
void stream_copy_thread(const std::string& input_path, const char* out_url) {
    AVFormatContext* in_fmt_ctx = nullptr;
    AVFormatContext* out_fmt_ctx = nullptr;
    AVPacket* pkt = nullptr;
    const int burst_duration = 2; // seconds before pacing
    AVDictionary* opts = nullptr;
    int64_t start_time;
    const AVOutputFormat* output_fmt = nullptr;
    //const char* out_url = "rtmp://a.rtmp.youtube.com/live2/dkab-gmua-11k2-pw4d-aqwe";

    while (!stop_signal) {
        std::cout << "Starting stream: " << input_path << " -> " << out_url << std::endl;

        int ret = avformat_open_input(&in_fmt_ctx, input_path.c_str(), nullptr, nullptr);
        if (ret < 0) {
            std::cerr << "Could not open input: " << get_av_error_string(ret) << std::endl;
            goto cleanup_retry;
        }

        ret = avformat_find_stream_info(in_fmt_ctx, nullptr);
        if (ret < 0) {
            std::cerr << "Could not get stream info: " << get_av_error_string(ret) << std::endl;
            goto cleanup_retry;
        }

        // output_fmt = av_guess_format("flv", nullptr, nullptr);
        // if (!output_fmt) {
        //     std::cerr << "Could not find FLV output format." << std::endl;
        //     goto cleanup_retry;
        // }

        std::cerr << "Pavankn LiveStream Start: " << out_url << std::endl;

        ret = avformat_alloc_output_context2(&out_fmt_ctx, nullptr, "flv", out_url);
        if (ret < 0 || !out_fmt_ctx) {
            std::cerr << "Could not allocate output context: " << get_av_error_string(ret) << std::endl;
            goto cleanup_retry;
        }

        // Copy streams
        for (unsigned int i = 0; i < in_fmt_ctx->nb_streams; i++) {
            AVStream* in_stream = in_fmt_ctx->streams[i];
            AVStream* out_stream = avformat_new_stream(out_fmt_ctx, nullptr);
            if (!out_stream) continue;
            avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            out_stream->codecpar->codec_tag = 0;
        }

        // Interrupt callback for graceful stop
        out_fmt_ctx->interrupt_callback.callback = [](void*) { return stop_signal ? 1 : 0; };
        out_fmt_ctx->interrupt_callback.opaque = nullptr;


#if 1
// Open output URL for writing
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            std::cout << "Opening output URL: " << out_url << std::endl;
            ret = avio_open2(&out_fmt_ctx->pb, out_url, AVIO_FLAG_WRITE, nullptr, &opts);
            if (ret < 0) {
                std::cerr << "Could not open output URL " << out_url << ": " << get_av_error_string(ret) << std::endl;
                avformat_close_input(&in_fmt_ctx);
                avformat_free_context(out_fmt_ctx);
                goto cleanup_retry;
            }
            av_dict_set(&opts, "reconnect", "1", 0);
            av_dict_set(&opts, "reconnect_streamed", "1", 0);
            av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        }
#else
        // Open output URL for writing
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            std::cout << "Opening output URL: " << out_url << std::endl;
            ret = avio_open(&out_fmt_ctx->pb, out_url, AVIO_FLAG_WRITE);
            if (ret < 0) {
                std::cerr << "Could not open output URL " << out_url << std::endl;
                avformat_close_input(&in_fmt_ctx);
                avformat_free_context(out_fmt_ctx);
                goto cleanup_retry;
            }
        }
#endif


        ret = avformat_write_header(out_fmt_ctx, nullptr);
        if (ret < 0) {
            std::cerr << "Error writing header: " << get_av_error_string(ret) << std::endl;
            goto cleanup_retry;
        }

        pkt = av_packet_alloc();
        if (!pkt) {
            std::cerr << "Could not allocate packet." << std::endl;
            goto cleanup_retry;
        }

        start_time = av_gettime_relative();

        while (!stop_signal && (ret = av_read_frame(in_fmt_ctx, pkt)) >= 0) {
            AVStream* in_stream = in_fmt_ctx->streams[pkt->stream_index];
            int64_t ts_us = pkt->pts != AV_NOPTS_VALUE ?
                av_rescale_q(pkt->pts, in_stream->time_base, AVRational{1, AV_TIME_BASE}) :
                av_rescale_q(pkt->dts, in_stream->time_base, AVRational{1, AV_TIME_BASE});

            int64_t elapsed_us = av_gettime_relative() - start_time;

            if (elapsed_us > burst_duration * AV_TIME_BASE && ts_us > elapsed_us) {
                int64_t delay = ts_us - elapsed_us;
                if (delay > UINT_MAX) delay = UINT_MAX;
                av_usleep(static_cast<unsigned int>(delay));
            }

            av_packet_rescale_ts(pkt, in_stream->time_base,
                                 out_fmt_ctx->streams[pkt->stream_index]->time_base);
            pkt->pos = -1;

            ret = av_interleaved_write_frame(out_fmt_ctx, pkt);
            av_packet_unref(pkt);
            if (ret < 0) {
                std::cerr << "Failed to write frame: " << get_av_error_string(ret) << std::endl;
                goto cleanup_retry;
            }
        }

    cleanup_retry:
        if (pkt) { av_packet_free(&pkt); pkt = nullptr; }
        if (in_fmt_ctx) { avformat_close_input(&in_fmt_ctx); in_fmt_ctx = nullptr; }
        if (out_fmt_ctx) {
            if (out_fmt_ctx->pb) avio_closep(&out_fmt_ctx->pb);
            avformat_free_context(out_fmt_ctx);
            out_fmt_ctx = nullptr;
        }

        if (!stop_signal) {
            std::cout << "Restarting stream in 5 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

void trim_inplace(std::string& s) {
    // left trim
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char ch){ return !std::isspace(ch); }));

    // right trim
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
}

int main() {
    avformat_network_init();
    std::signal(SIGINT, signal_handler);

    std::ifstream csv_file("streams.csv");
    if (!csv_file.is_open()) {
        std::cerr << "Cannot open streams.csv" << std::endl;
        return 1;
    }

    std::vector<std::thread> threads;
    std::string line;
    std::vector<std::string> input_files; // keep storage alive
    std::vector<std::string> rtmp_urls;   // keep storage alive

    #if 0
    while (std::getline(csv_file, line)) {
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;

        std::string input_file = line.substr(0, comma);
        std::string rtmp_url = line.substr(comma + 1);

        threads.emplace_back(stream_copy_thread, input_file, rtmp_url);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    #else
    while (std::getline(csv_file, line)) {
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;

        std::string input_file = line.substr(0, comma);
        std::string rtmp_url = line.substr(comma + 1);

        trim_inplace(input_file);
        trim_inplace(rtmp_url);

        #if 0
        input_files.push_back(line.substr(0, comma));
        rtmp_urls.push_back(line.substr(comma+1));

        trim_inplace(rtmp_urls.back());

        const char* rtmp_url = rtmp_urls.back().c_str();
        std::cout << "Pavankn rtmp_url: " << rtmp_url << std::endl;

        // pass stable pointers into the thread
        threads.emplace_back(
            stream_copy_thread,
            input_files.back().c_str(),
            rtmp_url
        );
        #endif
        threads.emplace_back(stream_copy_thread, input_file, rtmp_url.c_str());

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    #endif

    for (auto& t : threads) t.join();
    return 0;
}
