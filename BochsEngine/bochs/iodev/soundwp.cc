#include "bochs.h"
#include "soundwp.h"

#if BX_SUPPORT_SB16

#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>
#include <cstring>

#define LOG_THIS this->

bx_sound_wp_c::bx_sound_wp_c(bx_sb16_c *sb16)
  : bx_sound_output_c(sb16),
    m_engine(NULL), m_masteringVoice(NULL), m_sourceVoice(NULL),
    m_comInitialized(false), m_frequency(0), m_bits(0), m_stereo(0),
    m_ringNext(0)
{
  for (int i = 0; i < kRingSize; i++) m_ring[i] = NULL;
}

bx_sound_wp_c::~bx_sound_wp_c()
{
  closewaveoutput();
}

bool bx_sound_wp_c::EnsureEngine()
{
  if (m_engine != NULL) return true;

  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr)) m_comInitialized = true;

  hr = XAudio2Create(&m_engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    BX_ERROR(("sb16 (WPBochs): XAudio2Create failed (hr=0x%08x)", (unsigned)hr));
    m_engine = NULL;
    return false;
  }

  hr = m_engine->CreateMasteringVoice(&m_masteringVoice);
  if (FAILED(hr)) {
    BX_ERROR(("sb16 (WPBochs): CreateMasteringVoice failed (hr=0x%08x)", (unsigned)hr));
    m_engine->Release();
    m_engine = NULL;
    return false;
  }

  return true;
}

bool bx_sound_wp_c::RecreateVoiceIfNeeded(int frequency, int bits, int stereo)
{
  if (m_sourceVoice != NULL &&
      frequency == m_frequency && bits == m_bits && stereo == m_stereo)
    return true;

  if (m_sourceVoice != NULL) {
    m_sourceVoice->Stop();
    m_sourceVoice->FlushSourceBuffers();
    m_sourceVoice->DestroyVoice();
    m_sourceVoice = NULL;
  }

  WAVEFORMATEX fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.wFormatTag = WAVE_FORMAT_PCM;
  fmt.nChannels = (WORD)(stereo ? 2 : 1);
  fmt.nSamplesPerSec = (DWORD)frequency;
  fmt.wBitsPerSample = (WORD)bits;
  fmt.nBlockAlign = (WORD)(fmt.nChannels * (fmt.wBitsPerSample / 8));
  fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

  HRESULT hr = m_engine->CreateSourceVoice(&m_sourceVoice, &fmt);
  if (FAILED(hr)) {
    BX_ERROR(("sb16 (WPBochs): CreateSourceVoice failed (hr=0x%08x)", (unsigned)hr));
    m_sourceVoice = NULL;
    return false;
  }

  m_frequency = frequency;
  m_bits = bits;
  m_stereo = stereo;
  return true;
}

int bx_sound_wp_c::waveready()
{
  return BX_SOUND_OUTPUT_OK;
}

int bx_sound_wp_c::openwaveoutput(char *device)
{
  UNUSED(device);
  return EnsureEngine() ? BX_SOUND_OUTPUT_OK : BX_SOUND_OUTPUT_ERR;
}

int bx_sound_wp_c::startwaveplayback(int frequency, int bits, int stereo, int format)
{
  if (((format >> 1) & 7) != 0) {
    BX_ERROR(("sb16 (WPBochs): compressed wave format %d not supported, dropping audio", format));
    return BX_SOUND_OUTPUT_ERR;
  }
  if (!EnsureEngine()) return BX_SOUND_OUTPUT_ERR;
  if (!RecreateVoiceIfNeeded(frequency, bits, stereo)) return BX_SOUND_OUTPUT_ERR;

  m_sourceVoice->Start(0);
  return BX_SOUND_OUTPUT_OK;
}

int bx_sound_wp_c::sendwavepacket(int length, Bit8u data[])
{
  if (m_sourceVoice == NULL || length <= 0) return BX_SOUND_OUTPUT_ERR;

  Bit8u *&slot = m_ring[m_ringNext];
  m_ringNext = (m_ringNext + 1) % kRingSize;
  delete [] slot;
  slot = new Bit8u[length];
  memcpy(slot, data, length);

  XAUDIO2_BUFFER buf;
  memset(&buf, 0, sizeof(buf));
  buf.AudioBytes = (UINT32)length;
  buf.pAudioData = slot;

  HRESULT hr = m_sourceVoice->SubmitSourceBuffer(&buf);
  return SUCCEEDED(hr) ? BX_SOUND_OUTPUT_OK : BX_SOUND_OUTPUT_ERR;
}

int bx_sound_wp_c::stopwaveplayback()
{
  if (m_sourceVoice != NULL)
    m_sourceVoice->Stop();
  return BX_SOUND_OUTPUT_OK;
}

int bx_sound_wp_c::closewaveoutput()
{
  if (m_sourceVoice != NULL) {
    m_sourceVoice->Stop();
    m_sourceVoice->FlushSourceBuffers();
    m_sourceVoice->DestroyVoice();
    m_sourceVoice = NULL;
  }
  if (m_masteringVoice != NULL) {
    m_masteringVoice->DestroyVoice();
    m_masteringVoice = NULL;
  }
  if (m_engine != NULL) {
    m_engine->Release();
    m_engine = NULL;
  }
  for (int i = 0; i < kRingSize; i++) {
    delete [] m_ring[i];
    m_ring[i] = NULL;
  }
  if (m_comInitialized) {
    CoUninitialize();
    m_comInitialized = false;
  }
  return BX_SOUND_OUTPUT_OK;
}

#endif // BX_SUPPORT_SB16
