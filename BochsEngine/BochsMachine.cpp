#include "pch.h"
#include "BochsMachine.h"
#include "wpb_file_io_internal.h"
#include "bochs.h"
#include "gui/wpbochs_gui.h"
#include <string>
#include <cstring>
#include <ppltasks.h>

using namespace BochsEngine;
using namespace Platform;

extern void init_siminterface();
extern void bx_init_options();
extern int  bx_read_configuration(char *rcfile);
extern int  bx_init_hardware();


namespace {
    BochsMachine^ s_activeMachine = nullptr;
}

void OutputDebugStringD(const char *text)
{
#if DEBUG
	OutputDebugStringA(text);
#endif
}

void OutputDebugStringD(const wchar_t *text)
{
#if DEBUG
	OutputDebugStringW(text);
#endif
}

#define WPB_TRACE(msg) BochsMachine::TraceCheckpoint(msg)

void BochsMachine::TraceCheckpoint(const char *msg)
{
    OutputDebugStringD("[WPBochs] ");
    OutputDebugStringD(msg);
    OutputDebugStringD("\n");
}

void BochsMachine::LogCallbackThunk(int stream, const char *line)
{
    std::wstring wline(line, line + strlen(line));
    OutputDebugStringD((L"[Bochs] " + wline + L"\n").c_str());

    auto machine = s_activeMachine;
    if (machine == nullptr) return;
    auto managedLine = ref new String(wline.c_str());
    machine->LogUpdated(managedLine, stream == BX_LOG_STREAM_STDERR);
}

BochsMachine::BochsMachine()
    : m_currentButtonState(0), m_panicResolved(false), m_panicChoice(0)
{
    s_activeMachine = this;
    WPB_TRACE("BochsMachine constructed");
}

void BochsMachine::Start(String^ bochsrcPath)
{
    std::wstring wpath(bochsrcPath->Data());
    std::string path(wpath.begin(), wpath.end());

    WPB_TRACE("Start() called");

    bx_set_log_callback(&BochsMachine::LogCallbackThunk);

    auto thisMachine = this;
    auto task = Concurrency::create_task([thisMachine, path]() {
        WPB_TRACE("emulator task starting");
        thisMachine->RunEmulator(path);
    });
    task.then([](Concurrency::task<void> t) {
        try {
            t.get();
        } catch (const std::exception& ex) {
            OutputDebugStringD("[WPBochs] task FAULTED (std::exception): ");
            OutputDebugStringD(ex.what());
            OutputDebugStringD("\n");
        } catch (...) {
            WPB_TRACE("task FAULTED (unknown exception)");
        }
    });
}

void BochsMachine::RunEmulator(const std::string& path)
{
    WPB_TRACE("RunEmulator: SAFE_GET_IOFUNC");
    SAFE_GET_IOFUNC();
    WPB_TRACE("RunEmulator: SAFE_GET_GENLOG");
    SAFE_GET_GENLOG();
    WPB_TRACE("RunEmulator: init_siminterface");
    init_siminterface();
    SIM->set_notify_callback(&BochsMachine::PanicNotifyThunk);
    WPB_TRACE("RunEmulator: bx_init_options");
    bx_init_options();
    WPB_TRACE("RunEmulator: bx_read_configuration");
    bx_read_configuration(const_cast<char*>(path.c_str()));
    WPB_TRACE("RunEmulator: bx_init_hardware");
    bx_init_hardware();
    WPB_TRACE("RunEmulator: bx_init_hardware returned");

    SIM->set_init_done(1);
    bx_options.Omouse_enabled->set(bx_options.Omouse_enabled->get());

    WPB_TRACE("RunEmulator: entering cpu_loop");
    BX_CPU(0)->cpu_loop(1);
}

int BochsMachine::PanicNotifyThunk(int code)
{
    if (code != NOTIFY_CODE_LOGMSG) {
        SIM->notify_return(0);
        return 0;
    }

    char device[1024] = { 0 };
    int level = 0;
    char message[1024] = { 0 };
    SIM->log_msg_2(device, &level, message, sizeof(message) - 1);

    auto machine = s_activeMachine;
    int choice = (machine != nullptr) ? machine->BlockForPanicChoice(device, message) : 0;
    SIM->notify_return(choice);
    return 0;
}

int BochsMachine::BlockForPanicChoice(const char *device, const char *message)
{
    std::wstring wdevice(device, device + strlen(device));
    std::wstring wmessage(message, message + strlen(message));
    auto deviceString = ref new String(wdevice.c_str());
    auto messageString = ref new String(wmessage.c_str());

    {
        std::lock_guard<std::mutex> lock(m_panicMutex);
        m_panicResolved = false;
    }

    PanicRequested(deviceString, messageString);

    std::unique_lock<std::mutex> lock(m_panicMutex);
    m_panicCv.wait(lock, [this] { return m_panicResolved; });
    return m_panicChoice;
}

void BochsMachine::ResolvePanic(int choice)
{
    {
        std::lock_guard<std::mutex> lock(m_panicMutex);
        m_panicChoice = choice;
        m_panicResolved = true;
    }
    m_panicCv.notify_all();
}

void BochsMachine::RequestReset()
{
    WPBochsGui::RequestReset();
}

void BochsMachine::RequestShutdown()
{
    WPBochsGui::RequestShutdown();
}

void BochsMachine::SetPaused(bool paused)
{
    WPBochsGui::RequestPause(paused);
}

void BochsMachine::RegisterExternalFile(String^ path, Windows::Storage::Streams::IRandomAccessStream^ stream)
{
    std::wstring wpath(path->Data());
    std::string narrowPath(wpath.begin(), wpath.end());
    WPB_RegisterExternalFile(narrowPath, stream);
}

void BochsMachine::KeyEvent(int bxScancode, bool down)
{
    WPBochsGui::PushKeyEvent((unsigned)bxScancode, down);
}

void BochsMachine::MouseMove(int dx, int dy)
{
    WPBochsGui::PushMouseMotion(dx, dy, m_currentButtonState);
}

void BochsMachine::MouseButton(int button, bool down)
{
    unsigned bit = 1u << button;
    if (down)
        m_currentButtonState |= bit;
    else
        m_currentButtonState &= ~bit;
    WPBochsGui::PushMouseMotion(0, 0, m_currentButtonState);
}

void BochsMachine::SetMouseEnabled(bool enabled)
{
    WPBochsGui::SetMouseEnabled(enabled);
}

void BochsMachine::SetNetworkEnabled(bool enabled)
{
    if (bx_devices.pluginNE2kDevice) bx_devices.pluginNE2kDevice->set_link_enabled(enabled);
}

unsigned int BochsMachine::GetFrameWidth()
{
    return WPBochsGui::GetDimensions().width;
}

unsigned int BochsMachine::GetFrameHeight()
{
    return WPBochsGui::GetDimensions().height;
}

bool BochsMachine::TryCopyFrame(const Array<unsigned char>^ destination)
{
    if (!WPBochsGui::TakeFrameDirty())
        return false;
    return WPBochsGui::CopyFramebuffer(destination->Data, destination->Length);
}

unsigned long long BochsMachine::GetInstructionCount()
{
    return (unsigned long long) bx_pc_system.time_ticks();
}
