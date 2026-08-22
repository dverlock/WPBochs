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

void RequestPause(bool paused);

}
