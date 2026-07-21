#include "audio.h"

#include <fluidsynth.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

bool AudioPlayer::open() {
  SDL_AudioSpec want{};
  want.freq = kAudioRate;
  want.format = kAudioFormat;
  want.channels = kAudioChannels;
  want.samples = 2048;
  device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have_, 0);
  if (!device_) {
    std::cerr << "nixalarm: audio open failed: " << SDL_GetError() << "\n";
    return false;
  }
  std::cerr << "nixalarm: audio driver=" << (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown")
            << " freq=" << have_.freq
            << " channels=" << static_cast<int>(have_.channels)
            << " samples=" << have_.samples << "\n";
  SDL_PauseAudioDevice(device_, 0);
  return true;
}

void AudioPlayer::start(Source source, const std::string& fallback, const std::map<std::string, Source>& all) {
  stop();
  stopping_ = false;
  SDL_ClearQueuedAudio(device_);
  worker_ = std::thread([this, source, fallback, all]() {
    bool ok = false;
    if (source.type == SourceType::Generated) ok = play_generated();
    else if (source.type == SourceType::File) ok = play_ffmpeg(source.path);
    else if (source.type == SourceType::Internet) ok = play_ffmpeg(source.url);
    else if (source.type == SourceType::Midi) ok = play_midi(source);
    else if (source.type == SourceType::SdrWeatherband) ok = play_sdr(source);
    if (!ok && !stopping_) {
      auto it = all.find(fallback);
      if (it != all.end()) {
        std::cerr << "nixalarm: source failed; falling back to " << fallback << "\n";
        Source fb = it->second;
        if (fb.type == SourceType::Generated) play_generated();
        else if (fb.type == SourceType::File) play_ffmpeg(fb.path);
        else if (fb.type == SourceType::Internet) play_ffmpeg(fb.url);
        else if (fb.type == SourceType::Midi) play_midi(fb);
        else if (fb.type == SourceType::SdrWeatherband) play_sdr(fb);
      } else {
        play_generated();
      }
    }
  });
}

void AudioPlayer::stop() {
  stopping_ = true;
  if (worker_.joinable()) worker_.join();
  if (device_) SDL_ClearQueuedAudio(device_);
}

void AudioPlayer::queue_bytes(const Uint8* data, Uint32 bytes) {
  while (!stopping_ && SDL_GetQueuedAudioSize(device_) > static_cast<Uint32>(kAudioRate * 2)) {
    SDL_Delay(20);
  }
  if (!stopping_) SDL_QueueAudio(device_, data, bytes);
}

bool AudioPlayer::play_generated() {
  double phase = 0.0;
  const double two_pi = 6.283185307179586;
  std::vector<int16_t> buf(1024 * kAudioChannels);
  while (!stopping_) {
    auto now = Clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    double freq = ((ms / 450) % 2 == 0) ? 880.0 : 660.0;
    for (size_t i = 0; i < buf.size(); i += kAudioChannels) {
      double gate = ((ms / 900) % 2 == 0) ? 1.0 : 0.35;
      int16_t sample = static_cast<int16_t>(std::sin(phase) * 30000.0 * volume_ * gate);
      for (int ch = 0; ch < kAudioChannels; ++ch) buf[i + ch] = sample;
      phase += two_pi * freq / kAudioRate;
      if (phase > two_pi) phase -= two_pi;
    }
    queue_bytes(reinterpret_cast<Uint8*>(buf.data()), static_cast<Uint32>(buf.size() * sizeof(int16_t)));
  }
  return true;
}

bool AudioPlayer::play_ffmpeg(const std::string& uri) {
  if (uri.empty()) return false;
  AVFormatContext* fmt = nullptr;
  AVDictionary* opts = nullptr;
  av_dict_set(&opts, "reconnect", "1", 0);
  av_dict_set(&opts, "reconnect_streamed", "1", 0);
  std::string timeout_us = std::to_string(source_timeout_seconds_ * 1000000);
  av_dict_set(&opts, "rw_timeout", timeout_us.c_str(), 0);
  if (avformat_open_input(&fmt, uri.c_str(), nullptr, &opts) < 0) {
    av_dict_free(&opts);
    std::cerr << "nixalarm: could not open audio source: " << uri << "\n";
    return false;
  }
  av_dict_free(&opts);
  auto fmt_deleter = [](AVFormatContext* p) {
    if (p) avformat_close_input(&p);
  };
  std::unique_ptr<AVFormatContext, decltype(fmt_deleter)> fmt_guard(fmt, fmt_deleter);
  if (avformat_find_stream_info(fmt, nullptr) < 0) return false;
  int stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (stream_index < 0) return false;
  AVStream* stream = fmt->streams[stream_index];
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) return false;
  AVCodecContext* raw_ctx = avcodec_alloc_context3(codec);
  if (!raw_ctx) return false;
  auto codec_deleter = [](AVCodecContext* p) {
    if (p) avcodec_free_context(&p);
  };
  std::unique_ptr<AVCodecContext, decltype(codec_deleter)> ctx(raw_ctx, codec_deleter);
  if (avcodec_parameters_to_context(ctx.get(), stream->codecpar) < 0) return false;
  if (avcodec_open2(ctx.get(), codec, nullptr) < 0) return false;

  SwrContext* raw_swr = nullptr;
  AVChannelLayout out_layout;
  av_channel_layout_default(&out_layout, kAudioChannels);
  AVChannelLayout in_layout = ctx->ch_layout;
  if (in_layout.nb_channels == 0) av_channel_layout_default(&in_layout, ctx->ch_layout.nb_channels ? ctx->ch_layout.nb_channels : 2);
  if (swr_alloc_set_opts2(&raw_swr, &out_layout, AV_SAMPLE_FMT_S16, kAudioRate,
                          &in_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr) < 0) return false;
  auto swr_deleter = [](SwrContext* p) {
    if (p) swr_free(&p);
  };
  std::unique_ptr<SwrContext, decltype(swr_deleter)> swr(raw_swr, swr_deleter);
  if (swr_init(swr.get()) < 0) return false;

  AVPacket* raw_pkt = av_packet_alloc();
  AVFrame* raw_frame = av_frame_alloc();
  if (!raw_pkt || !raw_frame) return false;
  auto packet_deleter = [](AVPacket* p) {
    if (p) av_packet_free(&p);
  };
  auto frame_deleter = [](AVFrame* p) {
    if (p) av_frame_free(&p);
  };
  std::unique_ptr<AVPacket, decltype(packet_deleter)> pkt(raw_pkt, packet_deleter);
  std::unique_ptr<AVFrame, decltype(frame_deleter)> frame(raw_frame, frame_deleter);
  bool played = false;
  while (!stopping_) {
    int read = av_read_frame(fmt, pkt.get());
    if (read < 0) {
      if (fmt->duration > 0 && source_is_file(uri)) {
        av_seek_frame(fmt, stream_index, 0, AVSEEK_FLAG_BACKWARD);
        continue;
      }
      break;
    }
    if (pkt->stream_index != stream_index) {
      av_packet_unref(pkt.get());
      continue;
    }
    if (avcodec_send_packet(ctx.get(), pkt.get()) == 0) {
      while (!stopping_ && avcodec_receive_frame(ctx.get(), frame.get()) == 0) {
        int out_count = av_rescale_rnd(swr_get_delay(swr.get(), ctx->sample_rate) + frame->nb_samples,
                                       kAudioRate, ctx->sample_rate, AV_ROUND_UP);
        std::vector<int16_t> out(out_count * kAudioChannels);
        uint8_t* out_data = reinterpret_cast<uint8_t*>(out.data());
        int converted = swr_convert(swr.get(), &out_data, out_count,
                                    const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
        if (converted > 0) {
          for (int i = 0; i < converted * kAudioChannels; ++i) {
            out[i] = static_cast<int16_t>(std::clamp(out[i] * volume_, -32768.0, 32767.0));
          }
          queue_bytes(reinterpret_cast<Uint8*>(out.data()), static_cast<Uint32>(converted * kAudioChannels * sizeof(int16_t)));
          played = true;
        }
        av_frame_unref(frame.get());
      }
    }
    av_packet_unref(pkt.get());
  }
  return played;
}

std::string AudioPlayer::auto_soundfont() {
  for (const char* p : {
           "/usr/share/soundfonts/default.sf2",
           "/usr/share/sounds/sf2/FluidR3_GM.sf2",
           "/usr/share/soundfonts/FluidR3_GM.sf2",
           "/usr/share/minuet/soundfonts/GeneralUser-v1.47.sf2",
       }) {
    if (fs::exists(p)) return p;
  }
  return {};
}

bool AudioPlayer::play_midi(const Source& source) {
  if (source.path.empty()) return false;
  std::string soundfont = source.soundfont.empty() ? auto_soundfont() : source.soundfont;
  if (soundfont.empty()) {
    std::cerr << "nixalarm: MIDI source needs a SoundFont; set soundfont in config\n";
    return false;
  }

  fluid_settings_t* settings = new_fluid_settings();
  if (!settings) return false;
  fluid_settings_setnum(settings, "synth.sample-rate", kAudioRate);
  fluid_synth_t* synth = new_fluid_synth(settings);
  if (!synth) {
    delete_fluid_settings(settings);
    return false;
  }
  if (fluid_synth_sfload(synth, soundfont.c_str(), 1) == FLUID_FAILED) {
    std::cerr << "nixalarm: could not load MIDI SoundFont: " << soundfont << "\n";
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
    return false;
  }

  bool played = false;
  while (!stopping_) {
    fluid_player_t* player = new_fluid_player(synth);
    if (!player) break;
    if (fluid_player_add(player, source.path.c_str()) == FLUID_FAILED || fluid_player_play(player) == FLUID_FAILED) {
      std::cerr << "nixalarm: could not play MIDI file: " << source.path << "\n";
      delete_fluid_player(player);
      break;
    }

    constexpr int frames = 1024;
    std::vector<int16_t> left(frames);
    std::vector<int16_t> right(frames);
    std::vector<int16_t> interleaved(frames * kAudioChannels);
    while (!stopping_ && fluid_player_get_status(player) != FLUID_PLAYER_DONE) {
      if (fluid_synth_write_s16(synth, frames, left.data(), 0, 1, right.data(), 0, 1) == FLUID_FAILED) {
        break;
      }
      for (int i = 0; i < frames; ++i) {
        interleaved[i * 2] = static_cast<int16_t>(std::clamp(left[i] * volume_, -32768.0, 32767.0));
        interleaved[i * 2 + 1] = static_cast<int16_t>(std::clamp(right[i] * volume_, -32768.0, 32767.0));
      }
      queue_bytes(reinterpret_cast<Uint8*>(interleaved.data()), static_cast<Uint32>(interleaved.size() * sizeof(int16_t)));
      played = true;
    }
    fluid_player_stop(player);
    delete_fluid_player(player);
  }

  delete_fluid_synth(synth);
  delete_fluid_settings(settings);
  return played;
}

bool AudioPlayer::source_is_file(const std::string& uri) {
  return uri.find("://") == std::string::npos;
}

bool AudioPlayer::play_sdr(const Source& source) {
  std::ostringstream cmd;
  cmd << "rtl_fm -f " << std::fixed << std::setprecision(3) << source.frequency_mhz
      << "M -M fm -s 12000 -r " << kAudioRate << " -E deemp -d " << source.device_index;
  if (source.gain != "auto") cmd << " -g " << source.gain;
  cmd << " 2>/dev/null";
  FILE* pipe = popen(cmd.str().c_str(), "r");
  if (!pipe) {
    std::cerr << "nixalarm: failed to start rtl_fm\n";
    return false;
  }
  std::vector<int16_t> mono(2048);
  std::vector<int16_t> stereo(mono.size() * kAudioChannels);
  bool played = false;
  while (!stopping_) {
    size_t n = fread(mono.data(), sizeof(int16_t), mono.size(), pipe);
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i) {
      int16_t sample = static_cast<int16_t>(std::clamp(mono[i] * volume_, -32768.0, 32767.0));
      stereo[i * 2] = sample;
      stereo[i * 2 + 1] = sample;
    }
    queue_bytes(reinterpret_cast<Uint8*>(stereo.data()), static_cast<Uint32>(n * kAudioChannels * sizeof(int16_t)));
    played = true;
  }
  pclose(pipe);
  return played;
}
