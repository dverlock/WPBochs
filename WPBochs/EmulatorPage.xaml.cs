using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;
using Windows.Foundation;
using Windows.Storage;
using Windows.Storage.Streams;
using Windows.UI;
using Windows.UI.Core;
using Windows.UI.Text;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Media.Imaging;
using Windows.UI.Xaml.Navigation;
using Windows.System.Display;
using BochsEngine;
using System.IO;

namespace WPBochs
{
    public class EmulatorLaunchParams { public string BochsrcPath; public List<StorageFile> ExternalMediaFiles; }

    public sealed partial class EmulatorPage : Page
    {
        private BochsMachine _machine;
        private WriteableBitmap _bitmap;
        private DispatcherTimer _ipsTimer;
        private byte[] _frameBuffer, _grayscaleScratch;
        private uint _bitmapWidth, _bitmapHeight;
        private bool _mouseEnabled = true;
        private bool _networkEnabled = true;
        private bool _paused, _machineStopped, _twoFingerTapConsumed;
        private ulong _lastInstructionCount;
        private const double TapMoveThreshold = 12.0, KeyHeight = 34;
        private readonly Dictionary<uint, TouchInfo> _touches = new Dictionary<uint, TouchInfo>();
        private readonly Dictionary<uint, DispatcherTimer> _holdTimers = new Dictionary<uint, DispatcherTimer>();
        private readonly List<List<StackPanel>> _keyboardPages = new List<List<StackPanel>>();
        private int _currentKeyboardPage, _logTextLength;
        private const int MaxLogLines = 100;
        private DisplayRequest _displayRequest;

        private class TouchInfo { public Point StartPosition; public Point LastPosition; public bool MovedBeyondThreshold; public bool Dragging; }

        public EmulatorPage()
        {
            InitializeComponent();
            BuildKeyboard();
        }

        protected override async void OnNavigatedTo(NavigationEventArgs e)
        {
            EmulatorLaunchParams launchParams = e.Parameter as EmulatorLaunchParams;
            string bochsrcPath = launchParams != null ? launchParams.BochsrcPath : null;
            DebugWriteLine("Path to bochsrc is " + bochsrcPath);
            string[] bochsrcLines = new string[0];
            if (!string.IsNullOrEmpty(bochsrcPath))
            {
                StorageFile bochsrcFile = await StorageFile.GetFileFromPathAsync(bochsrcPath);
                string bochsrcText = await FileIO.ReadTextAsync(bochsrcFile);
                bochsrcLines = bochsrcText.Split('\n');
            }
            _mouseEnabled = false;
            bool networkAvailable = false;
            foreach (string rawLine in bochsrcLines)
            {
                string line = rawLine.TrimEnd('\r').Trim();
                if (line.StartsWith("mouse:") && line.Contains("enabled=1")) _mouseEnabled = true;
                else if (line.StartsWith("ne2k:")) networkAvailable = true;
            }
            if (!_mouseEnabled) mouseToggleButton.Visibility = Visibility.Collapsed;
            networkToggleButton.Visibility = networkAvailable ? Visibility.Visible : Visibility.Collapsed;
            AcquireWakelock();
            DebugWriteLine("Creating a BochsMachine");
            _machine = new BochsMachine();
            _machine.LogUpdated += Machine_LogUpdated;
            _machine.PanicRequested += Machine_PanicRequested;
            if (launchParams != null && launchParams.ExternalMediaFiles != null)
            {
                foreach (StorageFile file in launchParams.ExternalMediaFiles)
                {
                    IRandomAccessStream stream = await file.OpenAsync(FileAccessMode.ReadWrite);
                    _machine.RegisterExternalFile(file.Path, stream);
                }
            }
            if (!string.IsNullOrEmpty(bochsrcPath))
            {
                DebugWriteLine("Starting Machine");
                _machine.Start(bochsrcPath);
                DebugWriteLine("Machine started");
            }
        }

        private void EmulatorPage_Loaded(object sender, RoutedEventArgs e)
        {
            CompositionTarget.Rendering += CompositionTarget_Rendering;
            _ipsTimer = new DispatcherTimer();
            _ipsTimer.Interval = TimeSpan.FromSeconds(1);
            _ipsTimer.Tick += IpsTimer_Tick;
            _ipsTimer.Start();
            _logFlushTimer = new DispatcherTimer();
            _logFlushTimer.Interval = TimeSpan.FromMilliseconds(250);
            _logFlushTimer.Tick += LogFlushTimer_Tick;
            _logFlushTimer.Start();
        }

        private void EmulatorPage_Unloaded(object sender, RoutedEventArgs e)
        {
            CompositionTarget.Rendering -= CompositionTarget_Rendering;
            if (_ipsTimer != null) { _ipsTimer.Stop(); _ipsTimer = null; }
            if (_logFlushTimer != null) { _logFlushTimer.Stop(); _logFlushTimer = null; }
            StopAllHoldTimers();
            AllowScreenTurnoff();
        }

        private void StopMachine()
        {
            if (_machineStopped) return;
            _machineStopped = true;
            ReleaseAllModifiers();
            AllowScreenTurnoff();
            BochsMachine machine = _machine;
            _machine = null;
            if (machine != null)
            {
                machine.LogUpdated -= Machine_LogUpdated;
                machine.PanicRequested -= Machine_PanicRequested;
                machine.RequestShutdown();
            }
        }

        private void AcquireWakelock()
        {
            if (_displayRequest == null) _displayRequest = new DisplayRequest();
            _displayRequest.RequestActive();
        }

        private void AllowScreenTurnoff()
        {
            if (_displayRequest != null)
            {
                _displayRequest.RequestRelease();
                _displayRequest = null;
            }
        }

        private void ReleaseAllModifiers()
        {
            if (_modifierToggleState.Count == 0) return;
            List<int> heldCodes = new List<int>();
            foreach (KeyValuePair<int, bool> kvp in _modifierToggleState) if (kvp.Value) heldCodes.Add(kvp.Key);
            foreach (int code in heldCodes) { _modifierToggleState[code] = false; if (_machine != null) _machine.KeyEvent(code, false); }
            if (heldCodes.Count > 0 && _currentKeyboardPage < _keyboardPages.Count) RefreshModifierHighlights(_keyboardPages[_currentKeyboardPage]);
        }

        private void CompositionTarget_Rendering(object sender, object e)
        {
            if (_machine == null || _paused) return;
            uint w = _machine.GetFrameWidth();
            uint h = _machine.GetFrameHeight();
            if (w == 0 || h == 0) return;
            if (_bitmap == null || w != _bitmapWidth || h != _bitmapHeight)
            {
                ulong byteCount = (ulong)w * h * 4;
                if (byteCount == 0 || byteCount > int.MaxValue) return;
                _bitmap = new WriteableBitmap((int)w, (int)h);
                _bitmapWidth = w;
                _bitmapHeight = h;
                _frameBuffer = new byte[(int)byteCount];
                displayImage.Source = _bitmap;
            }
            if (_machine.TryCopyFrame(_frameBuffer)) { using (Stream stream = _bitmap.PixelBuffer.AsStream()) { stream.Write(_frameBuffer, 0, _frameBuffer.Length); } _bitmap.Invalidate(); }
        }

        private void pauseButton_Click(object sender, RoutedEventArgs e)
        {
            if (_machine == null) return;
            _paused = !_paused;
            _machine.SetPaused(_paused);
            if (_paused) { ApplyGrayscaleToDisplay(); pauseIcon.Source = new BitmapImage(new Uri("ms-appx:///Assets/ToolbarIcons/resume.png")); }
            else { RestoreColorDisplay(); pauseIcon.Source = new BitmapImage(new Uri("ms-appx:///Assets/ToolbarIcons/pause.png")); }
        }

        private void ApplyGrayscaleToDisplay()
        {
            if (_bitmap == null || _frameBuffer == null) return;
            if (_grayscaleScratch == null || _grayscaleScratch.Length != _frameBuffer.Length) _grayscaleScratch = new byte[_frameBuffer.Length];
            for (int i = 0; i + 3 < _frameBuffer.Length; i += 4)
            {
                byte b = _frameBuffer[i];
                byte g = _frameBuffer[i + 1];
                byte r = _frameBuffer[i + 2];
                byte luma = (byte)((r * 30 + g * 59 + b * 11) / 100);
                _grayscaleScratch[i] = luma;
                _grayscaleScratch[i + 1] = luma;
                _grayscaleScratch[i + 2] = luma;
                _grayscaleScratch[i + 3] = _frameBuffer[i + 3];
            }
            using (Stream stream = _bitmap.PixelBuffer.AsStream()) { stream.Write(_grayscaleScratch, 0, _grayscaleScratch.Length); }
            _bitmap.Invalidate();
        }

        private void RestoreColorDisplay()
        {
            if (_bitmap == null || _frameBuffer == null) return;
            using (Stream stream = _bitmap.PixelBuffer.AsStream()) { stream.Write(_frameBuffer, 0, _frameBuffer.Length); }
            _bitmap.Invalidate();
        }

        private void IpsTimer_Tick(object sender, object e)
        {
            if (_machine == null) return;
            ulong current = _machine.GetInstructionCount();
            ulong delta = current >= _lastInstructionCount ? current - _lastInstructionCount : 0;
            _lastInstructionCount = current;
            ipsText.Text = delta.ToString("N0") + " IPS";
        }

        private void SendMouseMove(Point from, Point to)
        {
            double dx = to.X - from.X;
            double dy = to.Y - from.Y;
            if (dx == 0 && dy == 0) return;
            _machine.MouseMove((int)dx, -(int)dy);
        }

        private static double Distance(Point a, Point b)
        {
            double dx = a.X - b.X;
            double dy = a.Y - b.Y;
            return Math.Sqrt(dx * dx + dy * dy);
        }

        private void displaySurface_PointerPressed(object sender, PointerRoutedEventArgs e)
        {
            if (!_mouseEnabled || _machine == null) return;
            Point point = e.GetCurrentPoint(displayImage).Position;
            uint id = e.Pointer.PointerId;
            _touches[id] = new TouchInfo { StartPosition = point, LastPosition = point, MovedBeyondThreshold = false, Dragging = false };
            displaySurface.CapturePointer(e.Pointer);
            if (_touches.Count == 2)
            {
                _twoFingerTapConsumed = false;
                foreach (uint touchId in _touches.Keys) StopHoldTimer(touchId);
                return;
            }
            if (_touches.Count > 2) return;
            DispatcherTimer holdTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1000) };
            holdTimer.Tick += (s, args) => HoldTimer_Tick(id);
            _holdTimers[id] = holdTimer;
            holdTimer.Start();
        }

        private void HoldTimer_Tick(uint id)
        {
            StopHoldTimer(id);
            TouchInfo touch;
            if (!_touches.TryGetValue(id, out touch) || touch.MovedBeyondThreshold || _machine == null) return;
            touch.Dragging = true;
            Windows.Phone.Devices.Notification.VibrationDevice.GetDefault().Vibrate(TimeSpan.FromMilliseconds(100));
            _machine.MouseButton(0, true);
        }

        private void StopHoldTimer(uint id)
        {
            DispatcherTimer timer;
            if (!_holdTimers.TryGetValue(id, out timer)) return;
            timer.Stop();
            _holdTimers.Remove(id);
        }
        private void StopAllHoldTimers() { foreach (DispatcherTimer timer in _holdTimers.Values) timer.Stop(); _holdTimers.Clear(); }

        private void displaySurface_PointerMoved(object sender, PointerRoutedEventArgs e)
        {
            if (!_mouseEnabled || _machine == null) return;
            uint id = e.Pointer.PointerId;
            TouchInfo touch;
            if (!_touches.TryGetValue(id, out touch)) return;
            Point point = e.GetCurrentPoint(displayImage).Position;
            if (!touch.MovedBeyondThreshold && Distance(touch.StartPosition, point) > TapMoveThreshold) { touch.MovedBeyondThreshold = true; StopHoldTimer(id); }
            SendMouseMove(touch.LastPosition, point);
            touch.LastPosition = point;
        }

        private void displaySurface_PointerReleased(object sender, PointerRoutedEventArgs e)
        {
            uint id = e.Pointer.PointerId;
            StopHoldTimer(id);
            TouchInfo touch;
            if (_touches.TryGetValue(id, out touch))
            {
                bool wasTwoFingerTap = _touches.Count == 2 && !_twoFingerTapConsumed && _touches.Values.All(t => !t.MovedBeyondThreshold);
                bool wasSingleTap = _touches.Count == 1 && !touch.Dragging && !touch.MovedBeyondThreshold;
                if (touch.Dragging && _machine != null) _machine.MouseButton(0, false);
                _touches.Remove(id);
                if (wasTwoFingerTap && _machine != null)
                {
                    _machine.MouseButton(1, true);
                    _machine.MouseButton(1, false);
                    _twoFingerTapConsumed = true;
                }
                else if (wasSingleTap && _machine != null) { _machine.MouseButton(0, true); _machine.MouseButton(0, false); }
            }
            displaySurface.ReleasePointerCapture(e.Pointer);
            if (_touches.Count == 0) _twoFingerTapConsumed = false;
        }

        private void mouseToggleButton_Click(object sender, RoutedEventArgs e)
        {
            _mouseEnabled = !_mouseEnabled;
            mouseToggleIcon.Source = new BitmapImage(new Uri( _mouseEnabled ? "ms-appx:///Assets/ToolbarIcons/mouse_enabled.png" : "ms-appx:///Assets/ToolbarIcons/mouse_disabled.png"));
            if (_machine != null) _machine.SetMouseEnabled(_mouseEnabled);
        }

        private void networkToggleButton_Click(object sender, RoutedEventArgs e)
        {
            _networkEnabled = !_networkEnabled;
            networkToggleIcon.Source = new BitmapImage(new Uri( _networkEnabled ? "ms-appx:///Assets/ToolbarIcons/yesnetwork.png" : "ms-appx:///Assets/ToolbarIcons/nonetwork.png"));
            if (_machine != null) _machine.SetNetworkEnabled(_networkEnabled);
        }

        private void keyboardToggleButton_Click(object sender, RoutedEventArgs e) => keyboardOverlay.Visibility = keyboardOverlay.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;

        private readonly List<LogEntry> _logBuffer = new List<LogEntry>();
        private readonly List<LogEntry> _pendingLogLines = new List<LogEntry>();
        private readonly object _pendingLogLock = new object();
        private DispatcherTimer _logFlushTimer;
        private struct LogEntry { public string Line; public bool IsError; }

        private void logButton_Click(object sender, RoutedEventArgs e) => logPopup.IsOpen = !logPopup.IsOpen;
        private void logPopup_Opened(object sender, object e)
        {
            RebuildLogTextBox();
            logTextBox.UpdateLayout();
            ScrollLogToEnd();
        }

        private void ScrollLogToEnd() { ITextRange endRange = logTextBox.Document.GetRange(_logTextLength, _logTextLength); endRange.ScrollIntoView(PointOptions.None); }
        private void resetButton_Click(object sender, RoutedEventArgs e) { ReleaseAllModifiers(); if (_machine != null) _machine.RequestReset(); }

        private async void shutdownButton_Click(object sender, RoutedEventArgs e) { StopMachine(); await Task.Delay(1000); Application.Current.Exit(); }

        private void Machine_LogUpdated(string line, bool isError)
        {
            DebugWriteLine("Machine: " + line);
            lock (_pendingLogLock) { _pendingLogLines.Add(new LogEntry { Line = line, IsError = isError }); }
        }

        private void LogFlushTimer_Tick(object sender, object e)
        {
            List<LogEntry> pending;
            lock (_pendingLogLock) { if (_pendingLogLines.Count == 0) return; pending = new List<LogEntry>(_pendingLogLines); _pendingLogLines.Clear();}
            logText.Text = "Last " + MaxLogLines + " lines of Bochs output";
            foreach (LogEntry entry in pending) { _logBuffer.Add(entry); if (_logBuffer.Count > MaxLogLines) _logBuffer.RemoveAt(0); }

            if (logPopup.IsOpen)
            {
                logTextBox.IsReadOnly = false;
                if (pending.Count >= MaxLogLines)
                {
                    logTextBox.Document.SetText(TextSetOptions.None, "");
                    _logTextLength = 0;
                    foreach (LogEntry entry in _logBuffer) AppendLogLine(entry.Line, entry.IsError);
                }
                else foreach (LogEntry entry in pending) AppendLogLine(entry.Line, entry.IsError);
                logTextBox.IsReadOnly = true;
                ScrollLogToEnd();
            }
        }

        private void AppendLogLine(string line, bool isError)
        {
            ITextDocument doc = logTextBox.Document;
            ITextRange range = doc.GetRange(_logTextLength, _logTextLength);
            range.CharacterFormat.ForegroundColor = isError ? Colors.OrangeRed : Colors.White;
            range.SetText(TextSetOptions.None, line + "\r");
            _logTextLength += line.Length + 1;
        }

        private void RebuildLogTextBox()
        {
            logTextBox.IsReadOnly = false;
            logTextBox.Document.SetText(TextSetOptions.None, "");
            _logTextLength = 0;
            foreach (LogEntry entry in _logBuffer) AppendLogLine(entry.Line, entry.IsError);
            logTextBox.IsReadOnly = true;
        }

        private async void Machine_PanicRequested(string device, string message)
        {
            BochsMachine machine = _machine;
            if (Dispatcher == null || machine == null) { machine?.ResolvePanic(0); return; }
            await Dispatcher.RunAsync(CoreDispatcherPriority.Normal, async () =>
            {
                if (_machine != machine) return;
                try
                {
                    RadioButton contRadio = new RadioButton { Content = "Continue execution", IsChecked = true };
                    RadioButton alwaysContRadio = new RadioButton { Content = "Continue execution and don't ask again" };
                    RadioButton dieRadio = new RadioButton { Content = "Stop execution and exit" };
                    StackPanel panel = new StackPanel();
                    panel.Children.Add(new TextBlock { Text = $"Device: {device}{Environment.NewLine}Message: {message}{Environment.NewLine}A PANIC has occurred. Do you want to:", TextWrapping = TextWrapping.Wrap });
                    panel.Children.Add(contRadio);
                    panel.Children.Add(alwaysContRadio);
                    panel.Children.Add(new TextBlock { Text = "This affects only PANIC events from device " + device, TextWrapping = TextWrapping.Wrap, FontSize = 12 });
                    panel.Children.Add(dieRadio);
                    ScrollViewer scrollViewer = new ScrollViewer
                    {
                        Content = panel,
                        VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                        HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
                        MaxHeight = Window.Current.Bounds.Height * 0.6
                    };
                    ContentDialog dialog = new ContentDialog { Title = ">>PANIC<<", Content = scrollViewer, PrimaryButtonText = "Continue" };
                    ContentDialogResult result = await dialog.ShowAsync();
                    if (_machine != machine) { machine.ResolvePanic(0); return; }
                    if (result == ContentDialogResult.Primary)
                    {
                        if (contRadio.IsChecked == true) machine.ResolvePanic(0);
                        else if (alwaysContRadio.IsChecked == true) machine.ResolvePanic(1);
                        else if (dieRadio.IsChecked == true)
                        {
                            machine.ResolvePanic(2);
                            StopMachine();
                            await Task.Delay(2000);
                            Application.Current.Exit();
                        }
                    }
                    else machine.ResolvePanic(0);
                }
                catch (Exception ex)
                {
                    DebugWriteLine(ex.ToString());
                    machine.ResolvePanic(0);
                }
            });
        }

        private void BuildKeyboard()
        {
            List<StackPanel> lettersPage = new List<StackPanel>
            {
                CreateKeyRow(Key("Tab", BxKeys.TAB, 50), Key("Q", BxKeys.Q), Key("W", BxKeys.W), Key("E", BxKeys.E), Key("R", BxKeys.R), Key("T", BxKeys.T), Key("Y", BxKeys.Y), Key("U", BxKeys.U), Key("I", BxKeys.I), Key("O", BxKeys.O), Key("P", BxKeys.P), Key("[", BxKeys.LEFT_BRACKET), Key("]", BxKeys.RIGHT_BRACKET), Key("\\", BxKeys.BACKSLASH), Key("Backspace", BxKeys.BACKSPACE, 70)),
                CreateKeyRow(Key("Caps", BxKeys.CAPS_LOCK, 60), Key("A", BxKeys.A), Key("S", BxKeys.S), Key("D", BxKeys.D), Key("F", BxKeys.F), Key("G", BxKeys.G), Key("H", BxKeys.H), Key("J", BxKeys.J), Key("K", BxKeys.K), Key("L", BxKeys.L), Key(";", BxKeys.SEMICOLON), Key("'", BxKeys.SINGLE_QUOTE), Key("Enter", BxKeys.ENTER, 70)),
                CreateKeyRow(Key("Shift", BxKeys.SHIFT_L, 70), Key("Z", BxKeys.Z), Key("X", BxKeys.X), Key("C", BxKeys.C), Key("V", BxKeys.V), Key("B", BxKeys.B), Key("N", BxKeys.N), Key("M", BxKeys.M), Key(",", BxKeys.COMMA), Key(".", BxKeys.PERIOD), Key("/", BxKeys.SLASH), Key("Shift", BxKeys.SHIFT_R, 70)),
                CreateKeyRow(Key("Ctrl", BxKeys.CTRL_L, 45), Key("Win", BxKeys.WIN_L, 45), Key("Alt", BxKeys.ALT_L, 45), Key("Space", BxKeys.SPACE, 160), Key("Alt", BxKeys.ALT_R, 45), Key("Ctrl", BxKeys.CTRL_R, 45)),
                CreateNavRow(),
            };
            List<StackPanel> numbersPage = new List<StackPanel>
            {
                CreateKeyRow(Key("Esc", BxKeys.ESC), Key("F1", BxKeys.F1), Key("F2", BxKeys.F2), Key("F3", BxKeys.F3), Key("F4", BxKeys.F4), Key("F5", BxKeys.F5), Key("F6", BxKeys.F6), Key("F7", BxKeys.F7), Key("F8", BxKeys.F8), Key("F9", BxKeys.F9), Key("F10", BxKeys.F10), Key("F11", BxKeys.F11), Key("F12", BxKeys.F12)),
                CreateKeyRow(Key("`", BxKeys.GRAVE), Key("1", BxKeys.K1), Key("2", BxKeys.K2), Key("3", BxKeys.K3), Key("4", BxKeys.K4), Key("5", BxKeys.K5), Key("6", BxKeys.K6), Key("7", BxKeys.K7), Key("8", BxKeys.K8), Key("9", BxKeys.K9), Key("0", BxKeys.K0), Key("-", BxKeys.MINUS), Key("=", BxKeys.EQUALS)),
                CreateKeyRow(Key("Ctrl", BxKeys.CTRL_L, 45), Key("Alt", BxKeys.ALT_L, 45), Key("Shift", BxKeys.SHIFT_L, 70), Key("Shift", BxKeys.SHIFT_R, 70), Key("Alt", BxKeys.ALT_R, 45), Key("Ctrl", BxKeys.CTRL_R, 45)),
                CreateNavRow(),
            };
            List<StackPanel> navPage = new List<StackPanel>
            {
                CreateKeyRow(Key("PrtSc", BxKeys.PRINT, 50), Key("Scroll", BxKeys.SCRL_LOCK, 50), Key("Pause", BxKeys.PAUSE, 50)),
                CreateKeyRow(Key("Ins", BxKeys.INSERT, 50), Key("Home", BxKeys.HOME, 50), Key("PgUp", BxKeys.PAGE_UP, 50)),
                CreateKeyRow(Key("Del", BxKeys.DELETE, 50), Key("End", BxKeys.END, 50), Key("PgDn", BxKeys.PAGE_DOWN, 50)),
                CreateKeyRow(Key("Ctrl", BxKeys.CTRL_L, 45), Key("Alt", BxKeys.ALT_L, 45), Key("Shift", BxKeys.SHIFT_L, 70), Key("Shift", BxKeys.SHIFT_R, 70), Key("Alt", BxKeys.ALT_R, 45), Key("Ctrl", BxKeys.CTRL_R, 45)),
                CreateKeyRow(Key("↑", BxKeys.UP, 50)),
                CreateKeyRow(Key("←", BxKeys.LEFT, 50), Key("↓", BxKeys.DOWN, 50), Key("→", BxKeys.RIGHT, 50)),
                CreateNavRow(),
            };
            _keyboardPages.Add(lettersPage);
            _keyboardPages.Add(numbersPage);
            _keyboardPages.Add(navPage);
            ShowKeyboardPage(0);
            DebugWriteLine("Built keyboard");
        }

        private struct KeyDef { public string Label; public int Code; public int Width; }
        private static KeyDef Key(string label, int code, int width = 34) { return new KeyDef { Label = label, Code = code, Width = width }; }

        private StackPanel CreateKeyRow(params KeyDef[] keys)
        {
            StackPanel row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
            foreach (KeyDef k in keys)
            {
                Button button = new Button
                {
                    Content = k.Label,
                    Width = k.Width,
                    MinWidth = k.Width,
                    MaxWidth = k.Width,
                    Height = KeyHeight,
                    MinHeight = KeyHeight,
                    MaxHeight = KeyHeight,
                    Tag = k.Code,
                    Style = (Style)Resources["KeyButtonStyle"]
                };
                button.AddHandler(PointerPressedEvent, new PointerEventHandler(KeyButton_PointerPressed), true);
                button.AddHandler(PointerReleasedEvent, new PointerEventHandler(KeyButton_PointerReleased), true);
                button.AddHandler(PointerCaptureLostEvent, new PointerEventHandler(KeyButton_PointerReleased), true);
                if (ModifierKeyCodes.Contains(k.Code))
                {
                    bool isDown;
                    _modifierToggleState.TryGetValue(k.Code, out isDown);
                    if (isDown) button.Background = ModifierActiveBrush;
                }
                row.Children.Add(button);
            }
            return row;
        }

        private readonly List<TextBlock> _keyboardPageIndicators = new List<TextBlock>();
        private StackPanel CreateNavRow()
        {
            Button prevButton = new Button { Content = "◀", Width = 70, MinWidth = 70, MaxWidth = 70, Height = KeyHeight, MinHeight = KeyHeight, MaxHeight = KeyHeight, Style = (Style)Resources["KeyButtonStyle"] };
            Button nextButton = new Button { Content = "▶", Width = 70, MinWidth = 70, MaxWidth = 70, Height = KeyHeight, MinHeight = KeyHeight, MaxHeight = KeyHeight, Style = (Style)Resources["KeyButtonStyle"] };
            TextBlock indicator = new TextBlock { Text = PageIndicatorText(), Foreground = new SolidColorBrush(Colors.White), FontSize = 14, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 10, 0), TextAlignment = TextAlignment.Center, Width = 60 };
            prevButton.Click += (s, e) => ShowKeyboardPage(_currentKeyboardPage - 1);
            nextButton.Click += (s, e) => ShowKeyboardPage(_currentKeyboardPage + 1);
            StackPanel row = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 4, 0, 0) };
            row.Children.Add(prevButton);
            row.Children.Add(indicator);
            row.Children.Add(nextButton);
            _keyboardPageIndicators.Add(indicator);
            return row;
        }

        private string PageIndicatorText() => (_currentKeyboardPage + 1) + "/" + Math.Max(_keyboardPages.Count, 1);
        private void ShowKeyboardPage(int index)
        {
            int pageCount = _keyboardPages.Count;
            if (pageCount == 0) return;
            _currentKeyboardPage = ((index % pageCount) + pageCount) % pageCount;
            keyboardStack.Children.Clear();
            List<StackPanel> page = _keyboardPages[_currentKeyboardPage];
            foreach (StackPanel row in page) keyboardStack.Children.Add(row);
            RefreshModifierHighlights(page);
            string text = PageIndicatorText();
            foreach (TextBlock indicator in _keyboardPageIndicators) indicator.Text = text;
        }

        private void RefreshModifierHighlights(List<StackPanel> page)
        {
            foreach (StackPanel row in page)
            {
                foreach (object child in row.Children)
                {
                    Button button = child as Button;
                    if (button == null || !(button.Tag is int)) continue;
                    int code = (int)button.Tag;
                    if (!ModifierKeyCodes.Contains(code)) continue;
                    bool isDown;
                    _modifierToggleState.TryGetValue(code, out isDown);
                    if (isDown) button.Background = ModifierActiveBrush;
                    else button.ClearValue(BackgroundProperty);
                }
            }
        }
        private static readonly HashSet<int> ModifierKeyCodes = new HashSet<int> { BxKeys.SHIFT_L, BxKeys.SHIFT_R, BxKeys.CTRL_L, BxKeys.CTRL_R, BxKeys.ALT_L, BxKeys.ALT_R };
        private static readonly SolidColorBrush ModifierActiveBrush = new SolidColorBrush(Color.FromArgb(0xFF, 0x33, 0x99, 0xFF));
        private readonly Dictionary<int, bool> _modifierToggleState = new Dictionary<int, bool>();

        private void KeyButton_PointerPressed(object sender, PointerRoutedEventArgs e)
        {
            Button button = (Button)sender;
            int code = (int)button.Tag;
            if (ModifierKeyCodes.Contains(code))
            {
                bool wasDown;
                _modifierToggleState.TryGetValue(code, out wasDown);
                bool isDown = !wasDown;
                _modifierToggleState[code] = isDown;
                if (isDown) button.Background = ModifierActiveBrush;
                else button.ClearValue(Button.BackgroundProperty);
                if (_machine != null) _machine.KeyEvent(code, isDown);
                return;
            }
            button.CapturePointer(e.Pointer);
            if (_machine != null) _machine.KeyEvent(code, true);
        }

        private void KeyButton_PointerReleased(object sender, PointerRoutedEventArgs e)
        {
            Button button = (Button)sender;
            int code = (int)button.Tag;
            if (ModifierKeyCodes.Contains(code)) return;
            if (_machine != null) _machine.KeyEvent(code, false);
        }

        private void DebugWriteLine(string text)
        {
#if DEBUG
            System.Diagnostics.Debug.WriteLine(text);
#endif
        }

        private void ToolbarButton_PointerPressed(object sender, PointerRoutedEventArgs e) => ((Border)sender).Background = (SolidColorBrush)Application.Current.Resources["PhoneAccentBrush"];
        private void ToolbarButton_PointerReleased(object sender, PointerRoutedEventArgs e) => ((Border)sender).Background = new SolidColorBrush(Colors.Transparent);
    }
}
