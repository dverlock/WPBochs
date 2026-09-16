#pragma once

namespace WPBochsGui {

struct FrameInfo {
  unsigned width;
  unsigned height;
};

FrameInfo GetDimensions();

bool CopyFramebuffer(unsigned char* dest, unsigned destCapacityBytes);

bool TakeFrameDirty();

void PushKeyEvent(unsigned bxScancode, bool down);

void PushMouseMotion(int dx, int dy, unsigned buttonState);

void SetMouseEnabled(bool enabled);

void RequestReset();
void RequestShutdown();
void ShutdownNow();

void RequestPause(bool paused);

void DrawCursorPixel(unsigned x, unsigned y, unsigned char r, unsigned char g, unsigned char b);

typedef void (*AcpiShutdownCallback)(void);
void SetAcpiShutdownCallback(AcpiShutdownCallback callback);
void NotifyAcpiShutdown();

}
