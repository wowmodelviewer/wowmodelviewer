/*
 * AudioPlayer.h
 *
 * Voice Lines (V1) audio preview. Decodes WoW audio (OGG/Vorbis, WAV, FLAC, MP3) from an in-memory
 * buffer via miniaudio and plays it on the default output device. One active sound at a time: starting a
 * new play stops the previous one. miniaudio is an implementation detail hidden behind a pImpl -- only
 * AudioPlayer.cpp includes miniaudio.h, so the heavy header never leaks into other translation units.
 */
#ifndef _AUDIOPLAYER_H_
#define _AUDIOPLAYER_H_

#include <cstddef>
#include <memory>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _AUDIOPLAYER_API_ __declspec(dllexport)
#    else
#        define _AUDIOPLAYER_API_ __declspec(dllimport)
#    endif
#else
#    define _AUDIOPLAYER_API_
#endif

class _AUDIOPLAYER_API_ AudioPlayer
{
public:
  AudioPlayer();
  ~AudioPlayer();

  // Copies + decodes the encoded audio buffer and starts playback (asynchronous, on the audio thread).
  // Stops any current sound first. volume01 is clamped to [0,1]. Returns false and sets lastError() on a
  // decode or device failure (e.g. unsupported format, or no output device in a headless environment).
  bool playBytes(const unsigned char * data, size_t len, float volume01 = 1.0f);

  void stop();                     // stop + release the current sound immediately
  void setVolume(float volume01);  // 0..1; affects current + subsequent playback (preview only)
  bool isPlaying() const;
  const char * lastError() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;

  AudioPlayer(const AudioPlayer &);
  AudioPlayer & operator=(const AudioPlayer &);
};

#endif /* _AUDIOPLAYER_H_ */
