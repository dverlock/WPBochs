#include "bochs.h"
#include "icon_bochs.h"
#include "wpbochs_gui.h"
#include "wpb_file_io.h"
#include "../font/vga.bitmap.h"

#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstring>
#include <atomic>

#define LOG_THIS bx_gui.

namespace {

std::mutex s_fbMutex;
unsigned s_width = 640;
unsigned s_height = 480;
unsigned s_tileWidth = 16;
unsigned s_tileHeight = 16;
unsigned s_bpp = 8;
std::vector<unsigned char> s_framebuffer;
bool s_frameDirty = false;

unsigned char s_paletteB[256];
unsigned char s_paletteG[256];
unsigned char s_paletteR[256];
bool s_paletteInitialized = false;

void EnsureFramebuffer()
{
  size_t needed = (size_t)s_width * s_height * 4;
  if (s_framebuffer.size() != needed)
    s_framebuffer.assign(needed, 0);
}

void PutPixel(unsigned x, unsigned y, unsigned char r, unsigned char g, unsigned char b)
{
  if (x >= s_width || y >= s_height) return;
  size_t off = ((size_t)y * s_width + x) * 4;
  s_framebuffer[off + 0] = b;
  s_framebuffer[off + 1] = g;
  s_framebuffer[off + 2] = r;
  s_framebuffer[off + 3] = 0xFF;
}


struct QueuedKey { unsigned bxScancode; bool down; };
struct QueuedMouse { int dx; int dy; unsigned buttonState; bool isMotion; };

std::mutex s_inputMutex;
std::vector<QueuedKey> s_keyQueue;
std::vector<QueuedMouse> s_mouseQueue;

std::atomic<bool> s_resetRequested(false);
std::atomic<bool> s_shutdownRequested(false);

std::mutex s_pauseMutex;
std::condition_variable s_pauseCv;
bool s_paused = false;

}

namespace WPBochsGui {

FrameInfo GetDimensions()
{
  std::lock_guard<std::mutex> lock(s_fbMutex);
  FrameInfo info;
  info.width = s_width;
  info.height = s_height;
  return info;
}

bool CopyFramebuffer(unsigned char* dest, unsigned destCapacityBytes)
{
  std::lock_guard<std::mutex> lock(s_fbMutex);
  size_t needed = (size_t)s_width * s_height * 4;
  if (dest == nullptr || destCapacityBytes < needed || s_framebuffer.size() != needed)
    return false;
  memcpy(dest, s_framebuffer.data(), needed);
  return true;
}

bool TakeFrameDirty()
{
  std::lock_guard<std::mutex> lock(s_fbMutex);
  bool wasDirty = s_frameDirty;
  s_frameDirty = false;
  return wasDirty;
}

void PushKeyEvent(unsigned bxScancode, bool down)
{
  std::lock_guard<std::mutex> lock(s_inputMutex);
  QueuedKey k;
  k.bxScancode = bxScancode;
  k.down = down;
  s_keyQueue.push_back(k);
}

void PushMouseMotion(int dx, int dy, unsigned buttonState)
{
  std::lock_guard<std::mutex> lock(s_inputMutex);
  QueuedMouse m;
  m.dx = dx;
  m.dy = dy;
  m.buttonState = buttonState;
  m.isMotion = true;
  s_mouseQueue.push_back(m);
}

void SetMouseEnabled(bool enabled)
{
  if (bx_options.Omouse_enabled != NULL) {
    bx_options.Omouse_enabled->set(enabled ? 1 : 0);
  }
}

void RequestReset()
{
  s_resetRequested = true;
}

void RequestShutdown()
{
  s_shutdownRequested = true;
}

void ShutdownNow()
{
  bx_atexit();
  wpb_flush_all();
  std::unique_lock<std::mutex> shutdownLock(s_pauseMutex);
  s_pauseCv.wait(shutdownLock, [] { return false; });
}

void RequestPause(bool paused)
{
  {
    std::lock_guard<std::mutex> lock(s_pauseMutex);
    s_paused = paused;
  }
  if (!paused) s_pauseCv.notify_all();
}

}

  void
bx_gui_c::specific_init(bx_gui_c *th, int argc, char **argv, unsigned tilewidth, unsigned tileheight,
                     unsigned headerbar_y)
{
  th->put("WPGUI");
  UNUSED(th);
  UNUSED(argc);
  UNUSED(argv);
  UNUSED(headerbar_y);
  UNUSED(bochs_icon_bits);

  std::lock_guard<std::mutex> lock(s_fbMutex);
  s_tileWidth = tilewidth;
  s_tileHeight = tileheight;
}


  void
bx_gui_c::handle_events(void)
{
  if (s_resetRequested.exchange(false)) {
    bx_pc_system.ResetSignal(PCS_SET);
    for (int i = 0; i < BX_SMP_PROCESSORS; i++)
      BX_CPU(i)->reset(BX_RESET_HARDWARE);
    longjmp(BX_CPU(0)->jmp_buf_env, 1);
  }
  if (s_shutdownRequested.exchange(false)) {
    WPBochsGui::ShutdownNow();
  }

  {
    std::unique_lock<std::mutex> lock(s_pauseMutex);
    s_pauseCv.wait(lock, [] { return !s_paused; });
  }

  std::vector<QueuedKey> keys;
  std::vector<QueuedMouse> mice;
  {
    std::lock_guard<std::mutex> lock(s_inputMutex);
    keys.swap(s_keyQueue);
    mice.swap(s_mouseQueue);
  }
  for (size_t i = 0; i < keys.size(); i++) {
    Bit32u key = keys[i].bxScancode | (keys[i].down ? BX_KEY_PRESSED : BX_KEY_RELEASED);
    DEV_kbd_gen_scancode(key);
  }
  for (size_t i = 0; i < mice.size(); i++) {
    DEV_mouse_motion(mice[i].dx, mice[i].dy, mice[i].buttonState);
  }
}


  void
bx_gui_c::flush(void)
{
}


  void
bx_gui_c::clear_screen(void)
{
  std::lock_guard<std::mutex> lock(s_fbMutex);
  EnsureFramebuffer();
  std::fill(s_framebuffer.begin(), s_framebuffer.end(), (unsigned char)0);
  s_frameDirty = true;
}


  void
bx_gui_c::text_update(Bit8u *old_text, Bit8u *new_text,
                      unsigned long cursor_x, unsigned long cursor_y,
                      bx_vga_tminfo_t tm_info, unsigned nrows)
{
  UNUSED(old_text);
  UNUSED(tm_info);

  static const unsigned char kCgaColors[16][3] = {
    {0,0,0}, {0,0,170}, {0,170,0}, {0,170,170},
    {170,0,0}, {170,0,170}, {170,85,0}, {170,170,170},
    {85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
    {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
  };

  std::lock_guard<std::mutex> lock(s_fbMutex);
  const unsigned cols = 80;
  const unsigned charW = 8, charH = 16;
  unsigned neededW = cols * charW;
  unsigned neededH = nrows * charH;
  if (s_width != neededW || s_height != neededH) {
    s_width = neededW;
    s_height = neededH;
  }
  EnsureFramebuffer();

  for (unsigned row = 0; row < nrows; row++) {
    for (unsigned col = 0; col < cols; col++) {
      unsigned cellOff = (row * cols + col) * 2;
      unsigned char ch = new_text[cellOff];
      unsigned char attr = new_text[cellOff + 1];
      const unsigned char *fg = kCgaColors[attr & 0x0F];
      const unsigned char *bg = kCgaColors[(attr >> 4) & 0x0F];
      const bx_fontcharbitmap_t &glyph = bx_vgafont[ch];
      unsigned baseX = col * charW;
      unsigned baseY = row * charH;
      for (unsigned gy = 0; gy < charH; gy++) {
        unsigned char bits = glyph.data[gy];
        for (unsigned gx = 0; gx < charW; gx++) {
          bool set = (bits & (0x01 << gx)) != 0;
          const unsigned char *c = set ? fg : bg;
          PutPixel(baseX + gx, baseY + gy, c[0], c[1], c[2]);
        }
      }
    }
  }
  s_frameDirty = true;
}

  int
bx_gui_c::get_clipboard_text(Bit8u **bytes, Bit32s *nbytes)
{
  UNUSED(bytes);
  UNUSED(nbytes);
  return 0;
}

  int
bx_gui_c::set_clipboard_text(char *text_snapshot, Bit32u len)
{
  UNUSED(text_snapshot);
  UNUSED(len);
  return 0;
}


  Boolean
bx_gui_c::palette_change(unsigned index, unsigned red, unsigned green, unsigned blue)
{
  if (index < 256) {
    s_paletteR[index] = (unsigned char)red;
    s_paletteG[index] = (unsigned char)green;
    s_paletteB[index] = (unsigned char)blue;
    s_paletteInitialized = true;
  }
  return 1;
}


  void
bx_gui_c::graphics_tile_update(Bit8u *tile, unsigned x0, unsigned y0)
{
  std::lock_guard<std::mutex> lock(s_fbMutex);
  EnsureFramebuffer();
  unsigned maxY = (y0 + s_tileHeight <= s_height) ? s_tileHeight : (s_height > y0 ? s_height - y0 : 0);
  unsigned maxX = (x0 + s_tileWidth <= s_width) ? s_tileWidth : (s_width > x0 ? s_width - x0 : 0);

  if (s_bpp == 32) {
    Bit32u *src = (Bit32u *)tile;
    for (unsigned ty = 0; ty < maxY; ty++) {
      for (unsigned tx = 0; tx < maxX; tx++) {
        Bit32u px = src[ty * s_tileWidth + tx];
        PutPixel(x0 + tx, y0 + ty, (unsigned char)(px >> 16), (unsigned char)(px >> 8), (unsigned char)px);
      }
    }
  } else if (s_bpp == 24) {
    for (unsigned ty = 0; ty < maxY; ty++) {
      Bit8u *row = &tile[ty * s_tileWidth * 3];
      for (unsigned tx = 0; tx < maxX; tx++) {
        Bit8u *px = &row[tx * 3];
        PutPixel(x0 + tx, y0 + ty, px[2], px[1], px[0]);
      }
    }
  } else if (s_bpp == 16) {
    Bit16u *src = (Bit16u *)tile;
    for (unsigned ty = 0; ty < maxY; ty++) {
      for (unsigned tx = 0; tx < maxX; tx++) {
        Bit16u px = src[ty * s_tileWidth + tx];
        unsigned char r = (unsigned char)(((px >> 11) & 0x1F) << 3);
        unsigned char g = (unsigned char)(((px >> 5) & 0x3F) << 2);
        unsigned char b = (unsigned char)((px & 0x1F) << 3);
        PutPixel(x0 + tx, y0 + ty, r, g, b);
      }
    }
  } else if (s_bpp == 15) {
    Bit16u *src = (Bit16u *)tile;
    for (unsigned ty = 0; ty < maxY; ty++) {
      for (unsigned tx = 0; tx < maxX; tx++) {
        Bit16u px = src[ty * s_tileWidth + tx];
        unsigned char r = (unsigned char)(((px >> 10) & 0x1F) << 3);
        unsigned char g = (unsigned char)(((px >> 5) & 0x1F) << 3);
        unsigned char b = (unsigned char)((px & 0x1F) << 3);
        PutPixel(x0 + tx, y0 + ty, r, g, b);
      }
    }
  } else {
    for (unsigned ty = 0; ty < maxY; ty++) {
      for (unsigned tx = 0; tx < maxX; tx++) {
        unsigned char idx = tile[ty * s_tileWidth + tx];
        unsigned char r = s_paletteInitialized ? s_paletteR[idx] : idx;
        unsigned char g = s_paletteInitialized ? s_paletteG[idx] : idx;
        unsigned char b = s_paletteInitialized ? s_paletteB[idx] : idx;
        PutPixel(x0 + tx, y0 + ty, r, g, b);
      }
    }
  }
  s_frameDirty = true;
}


  void
bx_gui_c::graphics_tile_update_rgb(Bit8u *rgb_tile, unsigned x0, unsigned y0)
{
  std::lock_guard<std::mutex> lock(s_fbMutex);
  EnsureFramebuffer();
  unsigned maxY = (y0 + s_tileHeight <= s_height) ? s_tileHeight : (s_height > y0 ? s_height - y0 : 0);
  unsigned maxX = (x0 + s_tileWidth <= s_width) ? s_tileWidth : (s_width > x0 ? s_width - x0 : 0);
  for (unsigned ty = 0; ty < maxY; ty++) {
    for (unsigned tx = 0; tx < maxX; tx++) {
      unsigned char *px = &rgb_tile[(ty * s_tileWidth + tx) * 3];
      PutPixel(x0 + tx, y0 + ty, px[0], px[1], px[2]);
    }
  }
  s_frameDirty = true;
}


  void
bx_gui_c::dimension_update(unsigned x, unsigned y, unsigned fheight, unsigned fwidth, unsigned bpp)
{
  UNUSED(fheight);
  UNUSED(fwidth);
  std::lock_guard<std::mutex> lock(s_fbMutex);
  s_width = x;
  s_height = y;
  s_bpp = bpp;
  EnsureFramebuffer();
  s_frameDirty = true;
}

  void
bx_gui_c::set_text_charmap(Bit8u *fbuffer)
{
  UNUSED(fbuffer);
}

  void
bx_gui_c::set_text_charbyte(Bit16u address, Bit8u data)
{
  UNUSED(address);
  UNUSED(data);
}



  unsigned
bx_gui_c::create_bitmap(const unsigned char *bmap, unsigned xdim, unsigned ydim)
{
  UNUSED(bmap);
  UNUSED(xdim);
  UNUSED(ydim);
  return(0);
}

  unsigned
bx_gui_c::headerbar_bitmap(unsigned bmap_id, unsigned alignment, void (*f)(void))
{
  UNUSED(bmap_id);
  UNUSED(alignment);
  UNUSED(f);
  return(0);
}

  void
bx_gui_c::show_headerbar(void)
{
}

  void
bx_gui_c::replace_bitmap(unsigned hbar_id, unsigned bmap_id)
{
  UNUSED(hbar_id);
  UNUSED(bmap_id);
}


  void
bx_gui_c::exit(void)
{
  BX_INFO(("bx_gui_c::exit()"));
}

  void
bx_gui_c::mouse_enabled_changed_specific (Boolean val)
{
  UNUSED(val);
}
