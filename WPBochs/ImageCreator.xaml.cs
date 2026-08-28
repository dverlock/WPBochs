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
        private bool _pendingUseVmdk;
        private const uint VmdkSectorSize = 512, VmdkSlbCount = 512;
        private const ulong VmdkGrainSectors = 128;
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
            _pendingUseVmdk = hardDiskTypeRadio.IsChecked == true && vmdkCheck.IsChecked == true;
            FileSavePicker picker = new FileSavePicker();
            picker.SuggestedStartLocation = PickerLocationId.DocumentsLibrary;
            picker.SuggestedFileName = fileName;
            if (_pendingUseVmdk) picker.FileTypeChoices.Add("VMDK disk image", new System.Collections.Generic.List<string> { ".vmdk" });
            else picker.FileTypeChoices.Add("Disk image", new System.Collections.Generic.List<string> { ".img" });
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
            try
            {
                if (_pendingUseVmdk) await WriteVmdkImageAsync(args.File, _pendingSizeBytes);
                else await WriteBlankImageAsync(args.File, _pendingSizeBytes);
                progressText.Text = "Image created.";
            }
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

        private async System.Threading.Tasks.Task WriteVmdkImageAsync(Windows.Storage.StorageFile file, ulong totalBytes)
        {
            ulong totalSectors = totalBytes / VmdkSectorSize;
            ulong grainsTotal = (totalSectors + VmdkGrainSectors - 1) / VmdkGrainSectors;
            uint gtCount = (uint)((grainsTotal + VmdkSlbCount - 1) / VmdkSlbCount);
            uint gtSize = (VmdkSlbCount * 4 + VmdkSectorSize - 1) / VmdkSectorSize;
            uint gdSize = (gtCount * 4 + VmdkSectorSize - 1) / VmdkSectorSize;
            ulong descriptionOffsetSectors = 1;
            ulong descriptionSizeSectors = 20;
            ulong flbOffsetSectors = descriptionOffsetSectors + descriptionSizeSectors;
            ulong flbCopyOffsetSectors = flbOffsetSectors + gdSize + (ulong)gtSize * gtCount;
            ulong tlbOffsetSectorsRaw = flbCopyOffsetSectors + gdSize + (ulong)gtSize * gtCount;
            ulong tlbOffsetSectors = ((tlbOffsetSectorsRaw + VmdkGrainSectors - 1) / VmdkGrainSectors) * VmdkGrainSectors;
            byte[] header = new byte[VmdkSectorSize];
            header[0] = (byte)'K'; header[1] = (byte)'D'; header[2] = (byte)'M'; header[3] = (byte)'V';
            WriteU32(header, 4, 1);
            WriteU32(header, 8, 3);
            WriteU64(header, 12, totalSectors);
            WriteU64(header, 20, VmdkGrainSectors);
            WriteU64(header, 28, descriptionOffsetSectors);
            WriteU64(header, 36, descriptionSizeSectors);
            WriteU32(header, 44, VmdkSlbCount);
            WriteU64(header, 48, flbOffsetSectors);
            WriteU64(header, 56, flbCopyOffsetSectors);
            WriteU64(header, 64, tlbOffsetSectors);
            header[72] = 0;
            header[73] = 0x0a; header[74] = 0x20; header[75] = 0x0d; header[76] = 0x0a;
            using (IRandomAccessStream stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite))
            {
                stream.Size = 0;
                DataWriter writer = new DataWriter(stream);
                ulong totalWriteBytes = tlbOffsetSectors * VmdkSectorSize;
                progressBar.Maximum = totalWriteBytes;
                progressBar.Value = 0;
                ulong written = 0;
                writer.WriteBytes(header);
                await writer.StoreAsync();
                written += VmdkSectorSize;
                progressBar.Value = written;
                byte[] descSector = new byte[VmdkSectorSize];
                string desc = "# Disk DescriptorFile\nversion=1\nCID=ffffffff\nparentCID=ffffffff\ncreateType=\"monolithicSparse\"\n\n# Extent description\nRW " + totalSectors + " SPARSE \"" + file.Name + "\"\n\n# The Disk Data Base\n#DDB\n\nddb.virtualHWVersion = \"4\"\nddb.geometry.sectors = \"63\"\nddb.geometry.heads = \"16\"\nddb.adapterType = \"ide\"\n";
                byte[] descBytes = System.Text.Encoding.UTF8.GetBytes(desc);
                Array.Copy(descBytes, descSector, Math.Min(descBytes.Length, descSector.Length));
                writer.WriteBytes(descSector);
                for (ulong i = 1; i < descriptionSizeSectors; i++) writer.WriteBytes(new byte[VmdkSectorSize]);
                await writer.StoreAsync();
                written += descriptionSizeSectors * VmdkSectorSize;
                progressBar.Value = written;
                byte[] flbBuffer = new byte[gdSize * VmdkSectorSize];
                for (uint i = 0; i < gtCount; i++) WriteU32(flbBuffer, (int)(i * 4), (uint)(flbOffsetSectors + gdSize + (ulong)i * gtSize));
                writer.WriteBytes(flbBuffer);
                await writer.StoreAsync();
                written += (ulong)flbBuffer.Length;
                progressBar.Value = written;
                written = await WriteZeroSectorsAsync(writer, (ulong)gtSize * gtCount, written, totalWriteBytes);
                byte[] flbCopyBuffer = new byte[gdSize * VmdkSectorSize];
                for (uint i = 0; i < gtCount; i++) WriteU32(flbCopyBuffer, (int)(i * 4), (uint)(flbCopyOffsetSectors + gdSize + (ulong)i * gtSize));
                writer.WriteBytes(flbCopyBuffer);
                await writer.StoreAsync();
                written += (ulong)flbCopyBuffer.Length;
                progressBar.Value = written;
                written = await WriteZeroSectorsAsync(writer, (ulong)gtSize * gtCount, written, totalWriteBytes);
                if (tlbOffsetSectors > tlbOffsetSectorsRaw) written = await WriteZeroSectorsAsync(writer, tlbOffsetSectors - tlbOffsetSectorsRaw, written, totalWriteBytes);
                progressText.Text = (written / 1024) + " KB / " + (totalWriteBytes / 1024) + " KB";
                await writer.FlushAsync();
                writer.DetachStream();
            }
        }

        private async System.Threading.Tasks.Task<ulong> WriteZeroSectorsAsync(DataWriter writer, ulong sectorCount, ulong written, ulong totalWriteBytes)
        {
            const ulong chunkSectors = 2048;
            byte[] chunk = new byte[chunkSectors * VmdkSectorSize];
            ulong remaining = sectorCount;
            while (remaining > 0)
            {
                ulong thisChunkSectors = Math.Min(chunkSectors, remaining);
                writer.WriteBytes(thisChunkSectors == chunkSectors ? chunk : new byte[thisChunkSectors * VmdkSectorSize]);
                await writer.StoreAsync();
                remaining -= thisChunkSectors;
                written += thisChunkSectors * VmdkSectorSize;
                progressBar.Value = written;
                progressText.Text = (written / 1024) + " KB / " + (totalWriteBytes / 1024) + " KB";
            }
            return written;
        }

        private static void WriteU32(byte[] buf, int offset, uint value)
        {
            buf[offset] = (byte)(value & 0xFF);
            buf[offset + 1] = (byte)((value >> 8) & 0xFF);
            buf[offset + 2] = (byte)((value >> 16) & 0xFF);
            buf[offset + 3] = (byte)((value >> 24) & 0xFF);
        }

        private static void WriteU64(byte[] buf, int offset, ulong value) { for (int i = 0; i < 8; i++) buf[offset + i] = (byte)((value >> (8 * i)) & 0xFF); }
    }
}
