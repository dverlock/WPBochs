#include "pch.h"
#include "bochs/wpb_net_io.h"

#include <map>
#include <mutex>
#include <vector>
#include <memory>
#include <string>
#include <cstring>

using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

namespace {

std::mutex s_mutex;
int s_nextHandle = 1;

struct UdpState {
    DatagramSocket^ socket;
    wpb_net_data_cb on_data;
    void *user;
    bool connected;
    bool closed;
    std::vector<std::vector<uint8_t>> pending;
};

struct TcpState {
    StreamSocket^ socket;
    DataWriter^ writer;
    wpb_net_connect_cb on_connect;
    wpb_net_data_cb on_data;
    wpb_net_closed_cb on_closed;
    void *user;
    bool connected;
    bool closed;
    bool writing;
    std::vector<std::vector<uint8_t>> write_queue;
};

std::map<int, std::shared_ptr<UdpState>> s_udp;
std::map<int, std::shared_ptr<TcpState>> s_tcp;

std::shared_ptr<UdpState> GetUdp(int handle)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_udp.find(handle);
    return it == s_udp.end() ? nullptr : it->second;
}

std::shared_ptr<TcpState> GetTcp(int handle)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_tcp.find(handle);
    return it == s_tcp.end() ? nullptr : it->second;
}

Platform::String^ WidenToPlatformString(const char *s)
{
    std::wstring w(s, s + strlen(s));
    return ref new Platform::String(w.c_str());
}

void TcpReadLoop(int handle, DataReader^ reader)
{
    Concurrency::create_task(reader->LoadAsync(4096)).then(
        [handle, reader](Concurrency::task<unsigned int> t)
        {
            auto st = GetTcp(handle);
            if (!st) return;
            unsigned int n;
            try { n = t.get(); } catch (...) { n = 0; }
            if (n == 0) {
                st->closed = true;
                if (st->on_closed) st->on_closed(handle, st->user);
                return;
            }
            Platform::Array<uint8_t>^ buf = ref new Platform::Array<uint8_t>(n);
            reader->ReadBytes(buf);
            if (st->on_data) st->on_data(handle, buf->Data, (int)n, st->user);
            TcpReadLoop(handle, reader);
        });
}

}

int wpb_net_udp_open(const char *host, int port, wpb_net_data_cb on_data, void *user)
{
    std::shared_ptr<UdpState> state = std::make_shared<UdpState>();
    state->socket = ref new DatagramSocket();
    state->on_data = on_data;
    state->user = user;
    state->connected = false;
    state->closed = false;

    int handle;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        handle = s_nextHandle++;
        s_udp[handle] = state;
    }

    state->socket->MessageReceived += ref new Windows::Foundation::TypedEventHandler<DatagramSocket^, DatagramSocketMessageReceivedEventArgs^>(
        [handle](DatagramSocket^ sender, DatagramSocketMessageReceivedEventArgs^ args)
        {
            auto st = GetUdp(handle);
            if (!st) return;
            DataReader^ reader = args->GetDataReader();
            unsigned int len = reader->UnconsumedBufferLength;
            if (len == 0) return;
            Platform::Array<uint8_t>^ buf = ref new Platform::Array<uint8_t>(len);
            reader->ReadBytes(buf);
            if (st->on_data) st->on_data(handle, buf->Data, (int)len, st->user);
        });

    HostName^ hostName = ref new HostName(WidenToPlatformString(host));
    std::wstring portWide = std::to_wstring(port);
    Platform::String^ portStr = ref new Platform::String(portWide.c_str());

    Concurrency::create_task(state->socket->ConnectAsync(hostName, portStr)).then(
        [handle](Concurrency::task<void> t)
        {
            auto st = GetUdp(handle);
            if (!st) return;
            try {
                t.get();
                st->connected = true;
                std::vector<std::vector<uint8_t>> pending;
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    pending.swap(st->pending);
                }
                for (auto &chunk : pending) {
                    wpb_net_udp_send(handle, chunk.data(), (int)chunk.size());
                }
            } catch (...) {
                st->closed = true;
            }
        });

    return handle;
}

void wpb_net_udp_send(int handle, const uint8_t *data, int length)
{
    auto st = GetUdp(handle);
    if (!st || st->closed) return;
    if (!st->connected) {
        std::lock_guard<std::mutex> lock(s_mutex);
        st->pending.emplace_back(data, data + length);
        return;
    }
    DataWriter^ writer = ref new DataWriter(st->socket->OutputStream);
    writer->WriteBytes(ref new Platform::Array<uint8_t>((uint8_t *)data, length));
    Concurrency::create_task(writer->StoreAsync()).then(
        [writer](Concurrency::task<unsigned int> t) { try { t.get(); } catch (...) {} });
}

void wpb_net_udp_close(int handle)
{
    std::shared_ptr<UdpState> st;
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_udp.find(handle);
    if (it == s_udp.end()) return;
    st = it->second;
    st->closed = true;
    s_udp.erase(it);
}

int wpb_net_tcp_open(const char *host, int port, wpb_net_connect_cb on_connect,
                      wpb_net_data_cb on_data, wpb_net_closed_cb on_closed, void *user)
{
    std::shared_ptr<TcpState> state = std::make_shared<TcpState>();
    state->socket = ref new StreamSocket();
    state->on_connect = on_connect;
    state->on_data = on_data;
    state->on_closed = on_closed;
    state->user = user;
    state->connected = false;
    state->closed = false;
    state->writing = false;

    int handle;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        handle = s_nextHandle++;
        s_tcp[handle] = state;
    }

    HostName^ hostName = ref new HostName(WidenToPlatformString(host));
    std::wstring portWide = std::to_wstring(port);
    Platform::String^ portStr = ref new Platform::String(portWide.c_str());

    Concurrency::create_task(state->socket->ConnectAsync(hostName, portStr)).then(
        [handle](Concurrency::task<void> t)
        {
            auto st = GetTcp(handle);
            if (!st) return;
            bool ok = true;
            try { t.get(); } catch (...) { ok = false; }
            st->connected = ok;
            if (ok) {
                st->writer = ref new DataWriter(st->socket->OutputStream);
                DataReader^ reader = ref new DataReader(st->socket->InputStream);
                reader->InputStreamOptions = InputStreamOptions::Partial;
                TcpReadLoop(handle, reader);
            } else {
                st->closed = true;
            }
            if (st->on_connect) st->on_connect(handle, ok ? 1 : 0, st->user);
        });

    return handle;
}

void FlushTcpWriteQueue(int handle)
{
    auto st = GetTcp(handle);
    if (!st) return;
    std::vector<uint8_t> chunk;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (st->closed || st->write_queue.empty()) { st->writing = false; return; }
        chunk = std::move(st->write_queue.front());
        st->write_queue.erase(st->write_queue.begin());
    }
    st->writer->WriteBytes(ref new Platform::Array<uint8_t>(chunk.data(), (unsigned int)chunk.size()));
    Concurrency::create_task(st->writer->StoreAsync()).then(
        [handle](Concurrency::task<unsigned int> t)
        {
            auto st2 = GetTcp(handle);
            if (!st2) return;
            try { t.get(); FlushTcpWriteQueue(handle); }
            catch (...) { st2->closed = true; }
        });
}

void wpb_net_tcp_send(int handle, const uint8_t *data, int length)
{
    auto st = GetTcp(handle);
    if (!st || st->closed || !st->connected || !st->writer) return;
    bool startFlush = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        st->write_queue.emplace_back(data, data + length);
        if (!st->writing) {
            st->writing = true;
            startFlush = true;
        }
    }
    if (startFlush) FlushTcpWriteQueue(handle);
}

void wpb_net_tcp_close(int handle)
{
    std::shared_ptr<TcpState> st;
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_tcp.find(handle);
    if (it == s_tcp.end()) return;
    st = it->second;
    st->closed = true;
    s_tcp.erase(it);
}
