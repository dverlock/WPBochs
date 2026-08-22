#include "pch.h"
#include "wpb_file_io_internal.h"
#include "bochs/wpb_file_io.h"

#include <map>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;
using namespace Windows::Storage::Streams;

MIDL_INTERFACE("905a0fef-bc53-11df-8c49-001e4fc686da")
IBufferByteAccess : public IUnknown
{
public:
    virtual HRESULT __stdcall Buffer(byte **value) = 0;
};

namespace {

struct ExternalHandle
{
    IRandomAccessStream^ stream;
    Buffer^ scratch;
};

Buffer^ GetScratchBuffer(ExternalHandle& handle, unsigned int minCapacity)
{
    if (handle.scratch == nullptr || handle.scratch->Capacity < minCapacity)
        handle.scratch = ref new Buffer(minCapacity);
    return handle.scratch;
}

std::mutex s_mutex;
std::map<std::string, IRandomAccessStream^> s_registeredFiles;
std::map<int, ExternalHandle> s_openHandles;
int s_nextFd = 1000000;

byte* GetRawBufferPointer(IBuffer^ buffer)
{
    ComPtr<IBufferByteAccess> byteAccess;
    reinterpret_cast<IInspectable*>(buffer)->QueryInterface(IID_PPV_ARGS(&byteAccess));
    byte* raw = nullptr;
    byteAccess->Buffer(&raw);
    return raw;
}

}

void WPB_RegisterExternalFile(const std::string& path, IRandomAccessStream^ stream)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_registeredFiles[path] = stream;
}

int wpb_open(const char* path, int flags)
{
    IRandomAccessStream^ stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_registeredFiles.find(std::string(path));
        if (it != s_registeredFiles.end()) stream = it->second;
    }

    if (stream == nullptr) {
        return ::_open(path, flags);
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    int fd = s_nextFd++;
    ExternalHandle handle;
    handle.stream = stream;
    s_openHandles[fd] = handle;
    return fd;
}

int wpb_close(int fd)
{
    IRandomAccessStream^ stream;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_openHandles.find(fd);
        if (it == s_openHandles.end()) return ::_close(fd);
        stream = it->second.stream;
        s_openHandles.erase(it);
    }
    Concurrency::create_task(stream->FlushAsync()).get();
    return 0;
}

long long wpb_lseek(int fd, long long offset, int whence)
{
    IRandomAccessStream^ stream;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_openHandles.find(fd);
        if (it == s_openHandles.end()) return (long long)::_lseeki64(fd, offset, whence);
        stream = it->second.stream;
    }

    unsigned long long base = 0;
    if (whence == SEEK_CUR) base = stream->Position;
    else if (whence == SEEK_END) base = stream->Size;
    long long newPos = (long long)base + offset;
    stream->Seek((unsigned long long)newPos);
    return newPos;
}

long long wpb_read(int fd, void* buf, long long count)
{
    ExternalHandle* handle;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_openHandles.find(fd);
        if (it == s_openHandles.end()) return (long long)::_read(fd, buf, (unsigned int)count);
        handle = &it->second;
    }

    Buffer^ winBuffer = GetScratchBuffer(*handle, (unsigned int)count);
    auto readOp = handle->stream->ReadAsync(winBuffer, (unsigned int)count, InputStreamOptions::None);
    IBuffer^ result = Concurrency::create_task(readOp).get();
    unsigned int bytesRead = result->Length;
    if (bytesRead > 0) {
        byte* raw = GetRawBufferPointer(result);
        memcpy(buf, raw, bytesRead);
    }
    return (long long)bytesRead;
}

long long wpb_write(int fd, const void* buf, long long count)
{
    ExternalHandle* handle;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_openHandles.find(fd);
        if (it == s_openHandles.end()) return (long long)::_write(fd, buf, (unsigned int)count);
        handle = &it->second;
    }

    Buffer^ winBuffer = GetScratchBuffer(*handle, (unsigned int)count);
    winBuffer->Length = (unsigned int)count;
    byte* raw = GetRawBufferPointer(winBuffer);
    memcpy(raw, buf, (size_t)count);

    unsigned int written = Concurrency::create_task(handle->stream->WriteAsync(winBuffer)).get();
    return (long long)written;
}

long long wpb_length(int fd)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_openHandles.find(fd);
    if (it == s_openHandles.end()) return -1;
    return (long long)it->second.stream->Size;
}

void wpb_flush_all()
{
    std::vector<IRandomAccessStream^> streams;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto& kv : s_openHandles) streams.push_back(kv.second.stream);
    }
    for (auto stream : streams) {
        Concurrency::create_task(stream->FlushAsync()).get();
    }
}
