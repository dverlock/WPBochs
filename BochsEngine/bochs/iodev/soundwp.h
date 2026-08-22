#pragma once

#if BX_SUPPORT_SB16

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

class bx_sound_wp_c : public bx_sound_output_c {
public:
  bx_sound_wp_c(bx_sb16_c *sb16);
  virtual ~bx_sound_wp_c();

  virtual int waveready();
  virtual int openwaveoutput(char *device);
  virtual int startwaveplayback(int frequency, int bits, int stereo, int format);
  virtual int sendwavepacket(int length, Bit8u data[]);
  virtual int stopwaveplayback();
  virtual int closewaveoutput();

private:
  bool EnsureEngine();
  bool RecreateVoiceIfNeeded(int frequency, int bits, int stereo);

  IXAudio2 *m_engine;
  IXAudio2MasteringVoice *m_masteringVoice;
  IXAudio2SourceVoice *m_sourceVoice;
  bool m_comInitialized;

  int m_frequency;
  int m_bits;
  int m_stereo;

  static const int kRingSize = 8;
  Bit8u *m_ring[kRingSize];
  int m_ringNext;
};

#endif // BX_SUPPORT_SB16
