using System;
using System.IO;
using System.Linq;
using Windows.Storage.Pickers;
using Windows.Storage.Streams;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace WPBochs
{
    public sealed partial class ImageCreator : Page
    {
        private ulong _pendingSizeBytes;

        public ImageCreator() { InitializeComponent(); }

        private void ImageKindRadio_Checked(object sender, RoutedEventArgs e)
        {
            if (floppySizePanel == null || hardDiskSizePanel == null) return;
            bool isFloppy = floppyTypeRadio.IsChecked == true;
            floppySizePanel.Visibility = isFloppy ? Visibility.Visible : Visibility.Collapsed;
            hardDiskSizePanel.Visibility = isFloppy ? Visibility.Collapsed : Visibility.Visible;
        }

        private void backButton_Click(object sender, RoutedEventArgs e) { if (Frame.CanGoBack) Frame.GoBack(); }

        private void createButton_Click(object sender, RoutedEventArgs e)
        {
            errorText.Text = "";
            string fileName = fileNameBox.Text.Trim();
            if (fileName.Length == 0 || fileNameBox.Text.Any(Path.GetInvalidFileNameChars().Contains)) { errorText.Text = "Enter a valid file name."; return; }
            ulong sizeBytes;
            if (floppyTypeRadio.IsChecked == true)
            {
                if (floppy720Radio.IsChecked == true) sizeBytes = 737280;
                else if (floppy12Radio.IsChecked == true) sizeBytes = 1228800;
                else if (floppy288Radio.IsChecked == true) sizeBytes = 2949120;
                else sizeBytes = 1474560;
            }
            else
            {
                uint sizeMb;
                if (!uint.TryParse(hardDiskSizeBox.Text, out sizeMb) || sizeMb == 0) { errorText.Text = "Enter a valid hard disk size in MB."; return; }
                sizeBytes = (ulong)sizeMb * 1024 * 1024;
            }
            _pendingSizeBytes = sizeBytes;
            FileSavePicker picker = new FileSavePicker();
            picker.SuggestedStartLocation = PickerLocationId.DocumentsLibrary;
            picker.SuggestedFileName = fileName;
            picker.FileTypeChoices.Add("Disk image", new System.Collections.Generic.List<string> { ".img" });
            picker.PickSaveFileAndContinue();
        }

        public async void ContinueFileSavePicker(Windows.ApplicationModel.Activation.FileSavePickerContinuationEventArgs args)
        {
            if (args.File == null) return;
            createButton.IsEnabled = false;
            progressOverlay.Visibility = Visibility.Visible;
            progressBar.Maximum = _pendingSizeBytes;
            progressBar.Value = 0;
            progressText.Text = "Creating image...";
            try { await WriteBlankImageAsync(args.File, _pendingSizeBytes); progressText.Text = "Image created."; }
            catch (Exception ex) { progressText.Text = "Failed: " + ex.Message; }
            finally
            {
                createButton.IsEnabled = true;
                await System.Threading.Tasks.Task.Delay(1200);
                progressOverlay.Visibility = Visibility.Collapsed;
            }
        }

        private async System.Threading.Tasks.Task WriteBlankImageAsync(Windows.Storage.StorageFile file, ulong totalBytes)
        {
            using (IRandomAccessStream stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite))
            {
                stream.Size = 0;
                DataWriter writer = new DataWriter(stream);
                const uint chunkSize = 1024 * 1024;
                byte[] chunk = new byte[chunkSize];
                ulong written = 0;
                while (written < totalBytes)
                {
                    uint thisChunk = (uint)Math.Min(chunkSize, totalBytes - written);
                    writer.WriteBytes(thisChunk == chunkSize ? chunk : new byte[thisChunk]);
                    await writer.StoreAsync();
                    written += thisChunk;
                    progressBar.Value = written;
                    progressText.Text = (written / 1024) + " KB / " + (totalBytes / 1024) + " KB";
                }
                await writer.FlushAsync();
                writer.DetachStream();
            }
        }
    }
}
