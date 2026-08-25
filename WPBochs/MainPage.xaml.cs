using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Windows.ApplicationModel.Activation;
using Windows.Storage;
using Windows.Storage.AccessCache;
using Windows.Storage.Pickers;
using Windows.UI.Popups;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Input;
using Windows.System;
using Windows.Storage.FileProperties;
using Windows.Storage.Streams;

namespace WPBochs
{
    public sealed partial class MainPage : Page
    {
        private const string BiosFileName = "BIOS-bochs-latest", AcpiBiosFileName = "BIOS-bochs-acpi", VgaBiosFileName = "VGABIOS-lgpl-latest", DemoFloppyFileName = "wpbfdos.img";
        private bool _memoryWarningShown, _loadingSettings, _experimentalDialogShowing;
        private StorageFile _flpaFile, _flpbFile, _hd0File, _hd1File, _cdromFile;
        int lastMemorySliderValue;

        public MainPage()
        {
            InitializeComponent();
#if DEBUG
            debugText.Visibility = Visibility.Visible;
#endif
            RoutedEventHandler save = (s, e) => SaveSettings();
            flpaCheck.Checked += save;
            flpaCheck.Unchecked += save;
            flpbCheck.Checked += save;
            flpbCheck.Unchecked += save;
            hd0Check.Checked += save;
            hd0Check.Unchecked += save;
            hd1Check.Checked += save;
            hd1Check.Unchecked += save;
            mouseCheck.Checked += save;
            mouseCheck.Unchecked += save;
            i440fxCheck.Checked += save;
            i440fxCheck.Unchecked += save;
            sb16Check.Checked += save;
            sb16Check.Unchecked += save;
            acpiCheck.Checked += (s, e) => { i440fxCheck.IsChecked = true; i440fxCheck.IsEnabled = false; save(s, e); };
            acpiCheck.Unchecked += (s, e) => { i440fxCheck.IsEnabled = true; save(s, e); };
            fpuCheck.Checked += save;
            fpuCheck.Unchecked += save;
            newHDSupportCheck.Checked += save;
            newHDSupportCheck.Unchecked += save;
            slowdownTimerCheck.Checked += save;
            slowdownTimerCheck.Unchecked += save;
            bootFloppyRadio.Checked += save;
            bootHardRadio.Checked += save;
            bootCdromRadio.Checked += save;
            kbXtRadio.Checked += save;
            kbAtRadio.Checked += save;
            kbMfRadio.Checked += save;
            ipsBox.TextChanged += (s, e) => SaveSettings();
            LoadSettingsAsync();
        }

        private async void CdromCheck_Checked(object sender, RoutedEventArgs e)
        {
            CheckBox other = sender == cdromCheck ? hd1Check : cdromCheck;
            if ((bool)other.IsChecked)
            {
                MessageDialog dialog = new MessageDialog("You can only use either Hard drive 1 or CDROM. Pick which one to use:");
                dialog.Commands.Add(new UICommand("Hard drive 1", new UICommandInvokedHandler((cmd) =>
                {
                    cdromCheck.IsChecked = false;
                    hd1Check.IsChecked = true;
                })));
                dialog.Commands.Add(new UICommand("CDROM", new UICommandInvokedHandler((cmd) =>
                {
                    hd1Check.IsChecked = false;
                    cdromCheck.IsChecked = true;
                })));
                await dialog.ShowAsync();
                return;
            }
            newHDSupportCheck.IsChecked = cdromCheck.IsChecked;
            newHDSupportCheck.IsEnabled = cdromCheck.IsChecked != true;
            SaveSettings();
        }

        private void CdromCheck_Unchecked(object sender, RoutedEventArgs e)
        {
            newHDSupportCheck.IsEnabled = true;
            SaveSettings();
        }

        private async void ExperimentalFeatureCheck_Checked(object sender, RoutedEventArgs e)
        {
            if (_loadingSettings || _experimentalDialogShowing) return;
            _experimentalDialogShowing = true;
            try
            {
                MessageDialog dialog = new MessageDialog("Please note that this feature is experimental and can lead to unexpected Bochs behaviour or crashing/panicking.", "Experimental Feature");
                dialog.Commands.Add(new UICommand("OK"));
                await dialog.ShowAsync();
            }
            finally
            {
                _experimentalDialogShowing = false;
            }
        }

        private void SaveSettings()
        {
            if (_loadingSettings) return;
            Windows.Foundation.Collections.IPropertySet s = ApplicationData.Current.LocalSettings.Values;
            s["flpaChecked"] = flpaCheck.IsChecked == true;
            s["flpbChecked"] = flpbCheck.IsChecked == true;
            s["hd0Checked"] = hd0Check.IsChecked == true;
            s["hd1Checked"] = hd1Check.IsChecked == true;
            s["cdromChecked"] = cdromCheck.IsChecked == true;
            s["mouseChecked"] = mouseCheck.IsChecked == true;
            s["i440fxChecked"] = i440fxCheck.IsChecked == true;
            s["sb16Checked"] = sb16Check.IsChecked == true;
            s["acpiChecked"] = acpiCheck.IsChecked == true;
            s["fpuChecked"] = fpuCheck.IsChecked == true;
            s["newHDChecked"] = newHDSupportCheck.IsChecked == true;
            s["slowdownTimerChecked"] = slowdownTimerCheck.IsChecked == true;
            s["memory"] = memorySlider.Value;
            s["ips"] = ipsBox.Text;
            s["bootFrom"] = bootHardRadio.IsChecked == true ? "hard" : (bootCdromRadio.IsChecked == true ? "cdrom" : "floppy");
            s["kbType"] = kbAtRadio.IsChecked == true ? "at" : (kbXtRadio.IsChecked == true ? "xt" : "mf");
        }

        private async void LoadSettingsAsync()
        {
            _loadingSettings = true;
            try
            {
                Windows.Foundation.Collections.IPropertySet s = ApplicationData.Current.LocalSettings.Values;
                if (!s.ContainsKey("firstLaunch"))
                {
                    s["firstLaunch"] = false;
                    MessageDialog dialog = new MessageDialog("It seems like it is your first time launching WPBochs. Do you want to insert a demo floppy disk with FreeDOS and some games on it?", "First launch");
                    dialog.Commands.Add(new UICommand("Yes", new UICommandInvokedHandler((cmd) => InsertSample())));
                    dialog.Commands.Add(new UICommand("No"));
                    await dialog.ShowAsync();
                }
                if (s.ContainsKey("flpaChecked")) flpaCheck.IsChecked = (bool)s["flpaChecked"];
                if (s.ContainsKey("flpbChecked")) flpbCheck.IsChecked = (bool)s["flpbChecked"];
                if (s.ContainsKey("hd0Checked")) hd0Check.IsChecked = (bool)s["hd0Checked"];
                if (s.ContainsKey("hd1Checked")) hd1Check.IsChecked = (bool)s["hd1Checked"];
                if (s.ContainsKey("cdromChecked")) cdromCheck.IsChecked = (bool)s["cdromChecked"];
                if (s.ContainsKey("mouseChecked")) mouseCheck.IsChecked = (bool)s["mouseChecked"];
                if (s.ContainsKey("i440fxChecked")) i440fxCheck.IsChecked = (bool)s["i440fxChecked"];
                if (s.ContainsKey("sb16Checked")) sb16Check.IsChecked = (bool)s["sb16Checked"];
                if (s.ContainsKey("acpiChecked")) acpiCheck.IsChecked = (bool)s["acpiChecked"];
                if (s.ContainsKey("fpuChecked")) fpuCheck.IsChecked = (bool)s["fpuChecked"];
                if (s.ContainsKey("newHDChecked")) newHDSupportCheck.IsChecked = (bool)s["newHDChecked"];
                if (s.ContainsKey("slowdownTimerChecked")) slowdownTimerCheck.IsChecked = (bool)s["slowdownTimerChecked"];
                if (s.ContainsKey("memory")) memorySlider.Value = (double)s["memory"];
                if (s.ContainsKey("ips")) ipsBox.Text = (string)s["ips"];
                if (s.ContainsKey("bootFrom"))
                {
                    string b = (string)s["bootFrom"];
                    bootFloppyRadio.IsChecked = b == "floppy";
                    bootHardRadio.IsChecked = b == "hard";
                    bootCdromRadio.IsChecked = b == "cdrom";
                }
                if (s.ContainsKey("kbType"))
                {
                    string k = (string)s["kbType"];
                    kbXtRadio.IsChecked = k == "xt";
                    kbAtRadio.IsChecked = k == "at";
                    kbMfRadio.IsChecked = k == "mf";
                }
                await RestoreFileSelectionAsync("flpa", f => { _flpaFile = f; flpaText.Text = f.Name; });
                await RestoreFileSelectionAsync("flpb", f => { _flpbFile = f; flpbText.Text = f.Name; });
                await RestoreFileSelectionAsync("hd0", f => { _hd0File = f; hd0Text.Text = f.Name; });
                await RestoreFileSelectionAsync("hd1", f => { _hd1File = f; hd1Text.Text = f.Name; });
                await RestoreFileSelectionAsync("cdrom", f => { _cdromFile = f; cdromText.Text = f.Name; });
                if (cdromCheck.IsChecked == true)
                {
                    newHDSupportCheck.IsChecked = true;
                    newHDSupportCheck.IsEnabled = false;
                }
            }
            finally { _loadingSettings = false; }
        }

        private static async Task RestoreFileSelectionAsync(string token, Action<StorageFile> apply)
        {
            if (!StorageApplicationPermissions.FutureAccessList.ContainsItem(token)) return;
            try
            {
                StorageFile file = await StorageApplicationPermissions.FutureAccessList.GetFileAsync(token);
                apply(file);
            }
            catch (Exception) { }
        }

        private void OpenFilePicker(string[] filters, string dataname, string data)
        {
            FileOpenPicker picker = new FileOpenPicker();
            picker.SuggestedStartLocation = PickerLocationId.Downloads;
            foreach (string filter in filters) picker.FileTypeFilter.Add(filter);
            picker.ContinuationData[dataname] = data;
            picker.PickSingleFileAndContinue();
        }

        private void flpaSelectButton_Click(object sender, RoutedEventArgs e) => OpenFilePicker(new string[] { ".img", ".ima" }, "filetype", "flp0");

        public async void ContinueFileOpenPicker(FileOpenPickerContinuationEventArgs args)
        {
            if (args.Files.Count == 0) return;
            StorageFile file = args.Files.FirstOrDefault();
            string filetype = (string)args.ContinuationData["filetype"];
            switch (filetype)
            {
                case "flp0":
                    _flpaFile = file;
                    flpaText.Text = file.Name;
                    StorageApplicationPermissions.FutureAccessList.AddOrReplace("flpa", file);
                    break;
                case "flp1":
                    _flpbFile = file;
                    flpbText.Text = file.Name;
                    StorageApplicationPermissions.FutureAccessList.AddOrReplace("flpb", file);
                    break;
                case "hd0":
                    if (file.FileType.Equals(".vmdk", StringComparison.OrdinalIgnoreCase) && !await IsSupportedVmdkAsync(file))
                    {
                        MessageDialog messageDialog = new MessageDialog("WPBochs currently only supports VMware 3 COW disk and VMware 4+ sparse disk format .vmdk files.", "Unsupported VMDK format");
                        messageDialog.Commands.Add(new UICommand("OK"));
                        await messageDialog.ShowAsync();
                        return;
                    }
                    _hd0File = file;
                    hd0Text.Text = file.Name;
                    StorageApplicationPermissions.FutureAccessList.AddOrReplace("hd0", file);
                    break;
                case "hd1":
                    if (file.FileType.Equals(".vmdk", StringComparison.OrdinalIgnoreCase) && !await IsSupportedVmdkAsync(file))
                    {
                        MessageDialog messageDialog = new MessageDialog("WPBochs currently only supports VMware 3 COW disk and VMware 4+ sparse disk format .vmdk files.", "Unsupported VMDK format");
                        messageDialog.Commands.Add(new UICommand("OK"));
                        await messageDialog.ShowAsync();
                        return;
                    }
                    _hd1File = file;
                    hd1Text.Text = file.Name;
                    StorageApplicationPermissions.FutureAccessList.AddOrReplace("hd1", file);
                    break;
                case "cdrom":
                    _cdromFile = file;
                    cdromText.Text = file.Name;
                    StorageApplicationPermissions.FutureAccessList.AddOrReplace("cdrom", file);
                    break;
            }
            SaveSettings();
        }

        private async void logoImage_Tapped(object sender, TappedRoutedEventArgs e)
        {
            Windows.ApplicationModel.PackageVersion version = Windows.ApplicationModel.Package.Current.Id.Version;
            string build =
#if DEBUG
                "debug";
#else
            "release";
#endif
            MessageDialog dialog = new MessageDialog($"WPBochs by dverlock. WPBochs {build} build {version.Revision}, Bochs v{version.Major}.{version.Minor}.{version.Build}", "WPBochs");
            dialog.Commands.Add(new UICommand("OK"));
            await dialog.ShowAsync();
        }

        private async void memorySlider_ValueChanged(object sender, Windows.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            if (memoryAmountText == null || memorySlider == null) return;
            memoryAmountText.Text = $"{memorySlider.Value:F0} MB";
            SaveSettings();
            ulong memLimitMB = MemoryManager.AppMemoryUsageLimit / 1024 / 1024;
            if (memorySlider.Value > memLimitMB)
            {
                if (_memoryWarningShown || _loadingSettings) return;
                _memoryWarningShown = true;
                MessageDialog dialog = new MessageDialog($"The selected RAM amount ({memorySlider.Value:F0} MB) exceeds the WPBochs' memory limit ({memLimitMB} MB). This leads to Bochs not starting the machine.", "Memory warning");
                dialog.Commands.Add(new UICommand("OK"));
                await dialog.ShowAsync();
            }
            else _memoryWarningShown = false;
        }

        private void flpbSelectButton_Click(object sender, RoutedEventArgs e) => OpenFilePicker(new string[] { ".img", ".ima" }, "filetype", "flp1");

        private void hd0SelectButton_Click(object sender, RoutedEventArgs e) => OpenFilePicker(new string[] { ".img", ".vmdk" }, "filetype", "hd0");

        private void hd1SelectButton_Click(object sender, RoutedEventArgs e) => OpenFilePicker(new string[] { ".img", ".vmdk" }, "filetype", "hd1");

        private static async Task<bool> IsSupportedVmdkAsync(StorageFile file)
        {
            using (IRandomAccessStream stream = await file.OpenAsync(FileAccessMode.Read))
            using (DataReader reader = new DataReader(stream))
            {
                await reader.LoadAsync(4);
                if (reader.UnconsumedBufferLength < 4) return false;
                byte[] magic = new byte[4];
                reader.ReadBytes(magic);
                bool isVmware3 = magic[0] == (byte)'C' && magic[1] == (byte)'O' && magic[2] == (byte)'W' && magic[3] == (byte)'D';
                bool isVmware4 = magic[0] == (byte)'K' && magic[1] == (byte)'D' && magic[2] == (byte)'M' && magic[3] == (byte)'V';
                return isVmware3 || isVmware4;
            }
        }

        private void cdromSelectButton_Click(object sender, RoutedEventArgs e) => OpenFilePicker(new string[] { ".iso" }, "filetype", "cdrom");

        private static async Task<StorageFolder> EnsureBiosFilesAsync()
        {
            StorageFolder localBios = await ApplicationData.Current.LocalFolder.CreateFolderAsync("bios", CreationCollisionOption.OpenIfExists);
            await CopyPackageFileAlwaysAsync(localBios, BiosFileName);
            await CopyPackageFileAlwaysAsync(localBios, AcpiBiosFileName);
            await CopyPackageFileAlwaysAsync(localBios, VgaBiosFileName);
            return localBios;
        }

        private static async Task CopyPackageFileAlwaysAsync(StorageFolder destFolder, string fileName)
        {
            StorageFolder assets = await Windows.ApplicationModel.Package.Current.InstalledLocation.GetFolderAsync("Assets");
            StorageFolder biosAssets = await assets.GetFolderAsync("BIOS");
            StorageFile src = await biosAssets.GetFileAsync(fileName);
            await src.CopyAsync(destFolder, fileName, NameCollisionOption.ReplaceExisting);
        }

        private static async Task CopyPackageFileIfMissingAsync(StorageFolder destFolder, string fileName)
        {
            try { await destFolder.GetFileAsync(fileName); return; }
            catch (FileNotFoundException) { }
            StorageFolder assets = await Windows.ApplicationModel.Package.Current.InstalledLocation.GetFolderAsync("Assets");
            StorageFolder biosAssets = await assets.GetFolderAsync("BIOS");
            StorageFile src = await biosAssets.GetFileAsync(fileName);
            await src.CopyAsync(destFolder, fileName, NameCollisionOption.ReplaceExisting);
        }

        private static long ComputeCylinders(ulong sizeBytes, int heads, int spt)
        {
            long sectors = (long)(sizeBytes / 512);
            long cyl = sectors / (heads * spt);
            return Math.Max(cyl, 1);
        }

        private static string GetFloppyTypeKey(ulong sizeBytes)
        {
            if (sizeBytes <= 737280) return "720k";
            if (sizeBytes <= 1228800) return "1_2";
            if (sizeBytes <= 1474560) return "1_44";
            if (sizeBytes <= 2949120) return "2_88";
            throw new Exception("Selected floppy image is not the size of a floppy");
        }

        private void memoryAmountText_Tapped(object sender, TappedRoutedEventArgs e)
        {
            memoryAmountText.Visibility = Visibility.Collapsed;
            memoryAmountGrid.Visibility = Visibility.Visible;
            lastMemorySliderValue = (int)memorySlider.Value;
            memoryAmountBox.Text = lastMemorySliderValue.ToString();
            Windows.Foundation.IAsyncAction _ = Dispatcher.RunAsync(Windows.UI.Core.CoreDispatcherPriority.Normal, () =>
            {
                 memoryAmountBox.Focus(FocusState.Programmatic);
                 memoryAmountBox.SelectAll();
            });
        }

        private void memoryAmountBox_LostFocus(object sender, RoutedEventArgs e)
        {
            Windows.Foundation.IAsyncAction _ = Dispatcher.RunAsync(Windows.UI.Core.CoreDispatcherPriority.Normal, () =>
            {
                object focused = FocusManager.GetFocusedElement();
                if (focused != memoryAmountBox && focused != typeof(Slider)) CommitMemoryValue();
            });
        }

        private void CommitMemoryValue()
        {
            if (memoryAmountGrid.Visibility == Visibility.Collapsed) return;
            int val;
            memorySlider.Value = int.TryParse(memoryAmountBox.Text, out val) && val > 0 ? Math.Max(memorySlider.Minimum, Math.Min(memorySlider.Maximum, val)) : lastMemorySliderValue;
            memoryAmountText.Visibility = Visibility.Visible;
            memoryAmountGrid.Visibility = Visibility.Collapsed;
        }

        private async Task<string> BuildBochsrcAsync(StorageFolder biosFolder)
        {
            StringBuilder sb = new StringBuilder();
            sb.AppendLine("# configuration file generated by WPBochs");
            if (flpaCheck.IsChecked == true && _flpaFile != null)
            {
                BasicProperties flpaProps = await _flpaFile.GetBasicPropertiesAsync();
                sb.AppendLine($"floppya: {GetFloppyTypeKey(flpaProps.Size)}=\"{_flpaFile.Path}\", status=inserted");
            }
            else sb.AppendLine("# no floppya");
            if (flpbCheck.IsChecked == true && _flpbFile != null)
            {
                BasicProperties flpbProps = await _flpbFile.GetBasicPropertiesAsync();
                sb.AppendLine($"floppyb: {GetFloppyTypeKey(flpbProps.Size)}=\"{_flpbFile.Path}\", status=inserted");
            }
            else sb.AppendLine("# no floppyb");
            if (hd0Check.IsChecked == true && _hd0File != null)
            {
                BasicProperties props = await _hd0File.GetBasicPropertiesAsync();
                long cyl = ComputeCylinders(props.Size, 16, 63);
                sb.AppendLine($"diskc: file=\"{_hd0File.Path}\", cyl={cyl}, heads=16, spt=63");
            }
            else sb.AppendLine("# no diskc");
            if (hd1Check.IsChecked == true && _hd1File != null)
            {
                BasicProperties props = await _hd1File.GetBasicPropertiesAsync();
                long cyl = ComputeCylinders(props.Size, 16, 63);
                sb.AppendLine($"diskd: file=\"{_hd1File.Path}\", cyl={cyl}, heads=16, spt=63");
            }
            else sb.AppendLine("# no diskd");
            if (cdromCheck.IsChecked == true && _cdromFile != null) sb.AppendLine($"cdromd: dev=\"{_cdromFile.Path}\", status=inserted");
            else sb.AppendLine("# no cdromd");
            bool acpiEnabled = acpiCheck.IsChecked == true && i440fxCheck.IsChecked == true;
            if (acpiEnabled) sb.AppendLine($"romimage: file={biosFolder.Path}\\{AcpiBiosFileName}, address=0xe0000");
            else sb.AppendLine($"romimage: file={biosFolder.Path}\\{BiosFileName}, address=0xf0000");
            sb.AppendLine($"vgaromimage: {biosFolder.Path}\\{VgaBiosFileName}");
            sb.AppendLine($"megs: {(int)memorySlider.Value}");
            if (sb16Check.IsChecked == true) sb.AppendLine("sb16: midimode=0, midi=, wavemode=1, wave=, loglevel=0, log=, dmatimer=600000");
            if (acpiEnabled) sb.AppendLine("acpi: enabled=1");
            string boot = bootHardRadio.IsChecked == true ? "c" : (bootCdromRadio.IsChecked == true ? "cdrom" : "a");
            sb.AppendLine($"boot: {boot}");
            sb.AppendLine("vga_update_interval: 30000");
            sb.AppendLine("keyboard_serial_delay: 20000");
            sb.AppendLine("keyboard_paste_delay: 100000");
            sb.AppendLine("floppy_command_delay: 50000");
            int ips;
            if (!int.TryParse(ipsBox.Text, out ips) || ips <= 0) ips = 10000000;
            sb.AppendLine($"ips: {ips}");
            sb.AppendLine($"mouse: enabled={(mouseCheck.IsChecked == true ? 1 : 0)}");
            sb.AppendLine($"fpu: enabled={(fpuCheck.IsChecked == true ? 1 : 0)}");
            sb.AppendLine("private_colormap: enabled=0");
            sb.AppendLine($"i440fxsupport: enabled={(i440fxCheck.IsChecked == true ? 1 : 0)}");
            sb.AppendLine("time0: 0");
            sb.AppendLine($"newharddrivesupport: enabled={(newHDSupportCheck.IsChecked == true ? 1 : 0)}");
            sb.AppendLine($"slowdown_timer: enabled={(slowdownTimerCheck.IsChecked == true ? 1 : 0)}");
            sb.AppendLine("log: -");
            sb.AppendLine("panic: action=ask");
            sb.AppendLine("error: action=report");
            sb.AppendLine("info: action=report");
            sb.AppendLine("debug: action=ignore");
            sb.AppendLine("keyboard_mapping: enabled=0, map=");
            string kbType = kbXtRadio.IsChecked == true ? "xt" : (kbAtRadio.IsChecked == true ? "at" : "mf");
            sb.AppendLine($"keyboard_type: {kbType}");
            return sb.ToString();
        }

        private void startButton_PointerPressed(object sender, PointerRoutedEventArgs e) => startButton.Opacity = 0.5;
        private void startButton_PointerReleased(object sender, PointerRoutedEventArgs e) => startButton.Opacity = 1.0;
        private void insertSampleButton_Click(object sender, RoutedEventArgs e) => InsertSample();

        private async void InsertSample()
        {
            startingOverlay.Visibility = Visibility.Visible;
            DebugWriteLine("Saving and inserting the demo floppy");
            startLogText.Text = "Saving and inserting the demo floppy";
            StorageFolder biosFolder = await EnsureBiosFilesAsync();
            await CopyPackageFileIfMissingAsync(biosFolder, DemoFloppyFileName);
            StorageFile demoFloppy = await biosFolder.GetFileAsync(DemoFloppyFileName);
            _flpaFile = demoFloppy;
            flpaText.Text = demoFloppy.Name;
            StorageApplicationPermissions.FutureAccessList.AddOrReplace("flpa", demoFloppy);
            flpaCheck.IsChecked = true;
            SaveSettings();
            startLogText.Text = "Starting Bochs";
            startingOverlay.Visibility = Visibility.Collapsed;
        }

        private async void startButton_Click(object sender, TappedRoutedEventArgs e)
        {
            startingOverlay.Visibility = Visibility.Visible;
            DebugWriteLine("Saving settings");
            startLogText.Text = "Saving settings";
            SaveSettings();
            try
            {
                DebugWriteLine("Ensuring that BIOS files are there");
                startLogText.Text = "Ensuring that BIOS files are there";
                StorageFolder biosFolder = await EnsureBiosFilesAsync();
                DebugWriteLine("Building bochsrc");
                startLogText.Text = "Building bochsrc";
                string bochsrc = await BuildBochsrcAsync(biosFolder);
                DebugWriteLine(".bochsrc:" + Environment.NewLine + bochsrc);
                DebugWriteLine("Saving bochsrc");
                startLogText.Text = "Saving bochsrc";
                StorageFile rcFile = await ApplicationData.Current.LocalFolder.CreateFileAsync("wpbochs.bochsrc", CreationCollisionOption.ReplaceExisting);
                await FileIO.WriteTextAsync(rcFile, bochsrc);
                DebugWriteLine("Collecting external media files");
                startLogText.Text = "Collecting external media files";
                List<StorageFile> externalFiles = new List<StorageFile>();
                if (flpaCheck.IsChecked == true && _flpaFile != null) externalFiles.Add(_flpaFile);
                if (flpbCheck.IsChecked == true && _flpbFile != null) externalFiles.Add(_flpbFile);
                if (hd0Check.IsChecked == true && _hd0File != null) externalFiles.Add(_hd0File);
                if (hd1Check.IsChecked == true && _hd1File != null) externalFiles.Add(_hd1File);
                if (cdromCheck.IsChecked == true && _cdromFile != null) externalFiles.Add(_cdromFile);
                DebugWriteLine("Navigating to Emulator");
                startLogText.Text = "Navigating to Emulator";
                Frame.Navigate(typeof(EmulatorPage), new EmulatorLaunchParams
                {
                    BochsrcPath = rcFile.Path,
                    MouseEnabled = mouseCheck.IsChecked == true,
                    ExternalMediaFiles = externalFiles
                });
            }
            catch (Exception ex)
            {
                startingOverlay.Visibility = Visibility.Collapsed;
                MessageDialog dialog = new MessageDialog(ex.Message, "Failed to start");
                dialog.Commands.Add(new UICommand("OK"));
                await dialog.ShowAsync();
            }
        }

        private void imageCreatorButton_Click(object sender, RoutedEventArgs e) => Frame.Navigate(typeof(ImageCreator));

        private void DebugWriteLine(string text)
        {
#if DEBUG
            System.Diagnostics.Debug.WriteLine(text);
#endif
        }
    }
}
