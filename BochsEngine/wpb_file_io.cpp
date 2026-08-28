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
    std::vector<byte> data;
    long long pos = 0;
    bool dirty = false;
};

byte* GetRawBufferPointer(IBuffer^ buffer)
{
    ComPtr<IBufferByteAccess> byteAccess;
    reinterpret_cast<IInspectable*>(buffer)->QueryInterface(IID_PPV_ARGS(&byteAccess));
    byte* raw = nullptr;
    byteAccess->Buffer(&raw);
    return raw;
}

void FlushHandleToStream(ExternalHandle& handle)
{
    if (!handle.dirty) return;

    Buffer^ winBuffer = ref new Buffer((unsigned int)handle.data.size());
    winBuffer->Length = (unsigned int)handle.data.size();
    if (!handle.data.empty()) {
        byte* raw = GetRawBufferPointer(winBuffer);
        memcpy(raw, handle.data.data(), handle.data.size());
    }

    handle.stream->Seek(0);
    Concurrency::create_task(handle.stream->WriteAsync(winBuffer)).get();
    Concurrency::create_task(handle.stream->FlushAsync()).get();
    handle.dirty = false;
}

std::mutex s_mutex;
std::map<std::string, IRandomAccessStream^> s_registeredFiles;
std::map<int, ExternalHandle> s_openHandles;
int s_nextFd = 1000000;

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

    ExternalHandle handle;
    handle.stream = stream;

    unsigned int size = (unsigned int)stream->Size;
    handle.data.resize(size);
    if (size > 0) {
        stream->Seek(0);
        Buffer^ winBuffer = ref new Buffer(size);
        IBuffer^ result = Concurrency::create_task(stream->ReadAsync(winBuffer, size, InputStreamOptions::None)).get();
        unsigned int bytesRead = result->Length;
        if (bytesRead > 0) {
            byte* raw = GetRawBufferPointer(result);
            memcpy(handle.data.data(), raw, bytesRead);
        }
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    int fd = s_nextFd++;
    s_openHandles[fd] = std::move(handle);
    return fd;
}

int wpb_close(int fd)
{
    ExternalHandle handle;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_openHandles.find(fd);
        if (it == s_openHandles.end()) return ::_close(fd);
        handle = std::move(it->second);
        s_openHandles.erase(it);
    }
    FlushHandleToStream(handle);
    return 0;
}

long long wpb_lseek(int fd, long long offset, int whence)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_openHandles.find(fd);
    if (it == s_openHandles.end()) return (long long)::_lseeki64(fd, offset, whence);

    ExternalHandle& handle = it->second;
    long long base = 0;
    if (whence == SEEK_CUR) base = handle.pos;
    else if (whence == SEEK_END) base = (long long)handle.data.size();
    handle.pos = base + offset;
    return handle.pos;
}

long long wpb_read(int fd, void* buf, long long count)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_openHandles.find(fd);
    if (it == s_openHandles.end()) return (long long)::_read(fd, buf, (unsigned int)count);

    ExternalHandle& handle = it->second;
    if (handle.pos < 0 || handle.pos >= (long long)handle.data.size()) return 0;

    long long available = (long long)handle.data.size() - handle.pos;
    long long toRead = count < available ? count : available;
    memcpy(buf, handle.data.data() + handle.pos, (size_t)toRead);
    handle.pos += toRead;
    return toRead;
}

long long wpb_write(int fd, const void* buf, long long count)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_openHandles.find(fd);
    if (it == s_openHandles.end()) return (long long)::_write(fd, buf, (unsigned int)count);

    ExternalHandle& handle = it->second;
    long long endPos = handle.pos + count;
    if (endPos > (long long)handle.data.size()) handle.data.resize((size_t)endPos);

    memcpy(handle.data.data() + handle.pos, buf, (size_t)count);
    handle.pos += count;
    handle.dirty = true;
    return count;
}

long long wpb_length(int fd)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_openHandles.find(fd);
    if (it == s_openHandles.end()) return -1;
    return (long long)it->second.data.size();
}

void wpb_flush_all()
{
    std::vector<ExternalHandle*> handles;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto& kv : s_openHandles) handles.push_back(&kv.second);
    }
    for (auto* handle : handles) {
        std::lock_guard<std::mutex> lock(s_mutex);
        FlushHandleToStream(*handle);
    }
}
