#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

namespace BochsEngine
{
    public delegate void LogLineHandler(Platform::String^ line, bool isError);
    public delegate void PanicHandler(Platform::String^ device, Platform::String^ message);

    public ref class BochsMachine sealed
    {
    public:
        BochsMachine();

        void Start(Platform::String^ bochsrcPath);

        void RequestReset();

        void RequestShutdown();

        void SetPaused(bool paused);

        void RegisterExternalFile(Platform::String^ path, Windows::Storage::Streams::IRandomAccessStream^ stream);

        void KeyEvent(int bxScancode, bool down);
        void MouseMove(int dx, int dy);
        void MouseButton(int button, bool down);
        void SetMouseEnabled(bool enabled);

        event PanicHandler^ PanicRequested;

        void ResolvePanic(int choice);

        unsigned int GetFrameWidth();
        unsigned int GetFrameHeight();

        bool TryCopyFrame(const Platform::Array<unsigned char>^ destination);

        unsigned long long GetInstructionCount();

        event LogLineHandler^ LogUpdated;

    private:
        void RunEmulator(const std::string& bochsrcPath);
        static void LogCallbackThunk(int stream, const char *line);
        static int PanicNotifyThunk(int code);
        int BlockForPanicChoice(const char *device, const char *message);
        static void TraceCheckpoint(const char *msg);

        unsigned int m_currentButtonState;
        std::mutex m_panicMutex;
        std::condition_variable m_panicCv;
        bool m_panicResolved;
        int m_panicChoice;
    };
}
