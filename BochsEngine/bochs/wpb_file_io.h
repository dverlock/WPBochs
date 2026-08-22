#pragma once

// routes floppy/hard-disk/CD-ROM image i/o through a WinRT-backed shim instead of raw CRT calls
// falls back to real CRT open() for paths never registered via BochsMachine::RegisterExternalFile
// included from plain-C++ files compiled without /ZW, so it must stay free of WinRT/C++-CX types

#ifdef __cplusplus
extern "C" {
#endif

int wpb_open(const char* path, int flags);
int wpb_close(int fd);
long long wpb_lseek(int fd, long long offset, int whence);
long long wpb_read(int fd, void* buf, long long count);
long long wpb_write(int fd, const void* buf, long long count);
// size in bytes, only valid for synthetic (externally-registered) handles
long long wpb_length(int fd);

// flushes every open externally-registered handle, call before exit()
void wpb_flush_all(void);

#ifdef __cplusplus
}
#endif
