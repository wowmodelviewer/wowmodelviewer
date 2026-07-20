/*
 * AudioPlayer.cpp -- thin C++ wrapper around miniaudio for Voice Lines audio preview. The miniaudio
 * implementation itself is compiled once in ThirdParty/miniaudio/miniaudio_impl.c (with stb_vorbis for
 * OGG). Here we only include the header for its API and keep every miniaudio type inside the pImpl.
 */
#include "AudioPlayer.h"

#include "miniaudio.h" // API declarations only; implementation lives in miniaudio_impl.c

#include <string>
#include <vector>

static inline float clamp01(float v)
{
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

struct AudioPlayer::Impl
{
  ma_engine  engine;
  ma_sound   sound;
  ma_decoder decoder;
  std::vector<unsigned char> data; // owned copy; ma_decoder_init_memory references it, so keep it alive
  bool  engineOk    = false;
  bool  soundInited = false;
  bool  decoderInited = false;
  float volume      = 1.0f;
  std::string err;
};

AudioPlayer::AudioPlayer() : m_impl(new Impl)
{
  ma_engine_config cfg = ma_engine_config_init();
  if (ma_engine_init(&cfg, &m_impl->engine) == MA_SUCCESS)
    m_impl->engineOk = true;
  else
    m_impl->err = "audio engine init failed (no output device?)";
}

AudioPlayer::~AudioPlayer()
{
  stop();
  if (m_impl->engineOk)
    ma_engine_uninit(&m_impl->engine);
}

void AudioPlayer::stop()
{
  if (m_impl->soundInited)
  {
    ma_sound_uninit(&m_impl->sound);
    m_impl->soundInited = false;
  }
  if (m_impl->decoderInited)
  {
    ma_decoder_uninit(&m_impl->decoder);
    m_impl->decoderInited = false;
  }
  m_impl->data.clear();
}

bool AudioPlayer::playBytes(const unsigned char * data, size_t len, float volume01)
{
  if (!m_impl->engineOk) { m_impl->err = "audio engine not initialised"; return false; }
  if (!data || len == 0) { m_impl->err = "empty audio buffer"; return false; }

  stop(); // starting a new line stops the previous one

  m_impl->volume = clamp01(volume01);
  m_impl->data.assign(data, data + len);

  if (ma_decoder_init_memory(m_impl->data.data(), m_impl->data.size(), NULL, &m_impl->decoder) != MA_SUCCESS)
  {
    m_impl->err = "decode failed (unsupported or corrupt audio)";
    m_impl->data.clear();
    return false;
  }
  m_impl->decoderInited = true;

  if (ma_sound_init_from_data_source(&m_impl->engine, &m_impl->decoder, 0, NULL, &m_impl->sound) != MA_SUCCESS)
  {
    m_impl->err = "sound init failed";
    ma_decoder_uninit(&m_impl->decoder);
    m_impl->decoderInited = false;
    m_impl->data.clear();
    return false;
  }
  m_impl->soundInited = true;

  ma_sound_set_volume(&m_impl->sound, m_impl->volume);
  if (ma_sound_start(&m_impl->sound) != MA_SUCCESS)
  {
    m_impl->err = "playback start failed";
    stop();
    return false;
  }

  m_impl->err.clear();
  return true;
}

void AudioPlayer::setVolume(float volume01)
{
  m_impl->volume = clamp01(volume01);
  if (m_impl->soundInited)
    ma_sound_set_volume(&m_impl->sound, m_impl->volume);
}

bool AudioPlayer::isPlaying() const
{
  return m_impl->soundInited && ma_sound_is_playing(&m_impl->sound) == MA_TRUE;
}

const char * AudioPlayer::lastError() const
{
  return m_impl->err.c_str();
}
