#define BX_PLUGGABLE

#include "bochs.h"
#include "wpb_file_io.h"

const off_t vbox_image_t::INVALID_OFFSET = (off_t)-1;

extern bx_hard_drive_c *theHardDrive;
#define LOG_THIS theHardDrive->

int vbox_image_t::open(const char* _pathname)
{
    int flags = O_RDWR;
#ifdef O_BINARY
    flags |= O_BINARY;
#endif

    pathname = _pathname;
    file_descriptor = wpb_open(pathname, flags);
    if (file_descriptor < 0)
        return -1;

    if (!read_header()) {
        BX_PANIC(("unable to read vbox virtual disk header from file '%s'", pathname));
        return -1;
        }

    if (header.image_type != 1) {
        BX_PANIC(("vbox: only dynamically-allocated vdi images are supported ('%s')", pathname));
        return -1;
        }

    block_data = new Bit8u[header.block_size];
    if (block_data == 0)
        BX_PANIC(("vbox: unable to allocate block buffer for '%s'", pathname));

    mtlb = new Bit32s[header.blocks_in_hdd];
    if (mtlb == 0)
        BX_PANIC(("vbox: unable to allocate block map for '%s'", pathname));

    if (wpb_lseek(file_descriptor, header.offset_blocks, SEEK_SET) < 0)
        return -1;
    if (wpb_read(file_descriptor, mtlb, (long long)header.blocks_in_hdd * 4) != (long long)header.blocks_in_hdd * 4)
        return -1;

    mtlb_sector = (Bit32u)-1;
    is_dirty = false;
    mtlb_dirty = false;
    current_offset = 0;

    if (header.cylinders > 0) {
        cylinders = header.cylinders;
        heads = header.heads;
        sectors = header.sectors;
        }
    else {
        cylinders = (unsigned)(((header.disk_size / header.sector_size) / 16) / 63);
        heads = 16;
        sectors = 63;
        }

    BX_INFO(("'vbox' disk image opened: path is '%s'", pathname));

    return 1;
}

void vbox_image_t::close()
{
    if (file_descriptor < 0)
        return;

    flush();

    if (mtlb_dirty) {
        wpb_lseek(file_descriptor, header.offset_blocks, SEEK_SET);
        wpb_write(file_descriptor, mtlb, (long long)header.blocks_in_hdd * 4);
        wpb_lseek(file_descriptor, 0, SEEK_SET);
        wpb_write(file_descriptor, &header, sizeof(VDI_Header));
        }

    delete [] mtlb;
    mtlb = 0;
    delete [] block_data;
    block_data = 0;

    wpb_close(file_descriptor);
    file_descriptor = -1;
}

off_t vbox_image_t::lseek(off_t offset, int whence)
{
    switch (whence) {
      case SEEK_SET:
        current_offset = offset;
        return current_offset;
      case SEEK_CUR:
        current_offset += offset;
        return current_offset;
      case SEEK_END:
        current_offset = (off_t)header.disk_size + offset;
        return current_offset;
      default:
        BX_DEBUG(("vbox: unsupported seek mode %d", whence));
        return INVALID_OFFSET;
        }
}

ssize_t vbox_image_t::read(void* buf, size_t count)
{
    char *cbuf = (char*)buf;
    ssize_t total = 0;
    while (count > 0) {
        off_t readable = perform_seek();
        if (readable == INVALID_OFFSET)
            return -1;

        off_t copysize = ((off_t)count > readable) ? readable : (off_t)count;
        off_t offset = current_offset & (header.block_size - 1);
        memcpy(cbuf, block_data + offset, (size_t)copysize);

        current_offset += copysize;
        total += (long)copysize;
        cbuf += copysize;
        count -= (size_t)copysize;
        }

    return total;
}

ssize_t vbox_image_t::write(const void* buf, size_t count)
{
    char *cbuf = (char*)buf;
    ssize_t total = 0;
    while (count > 0) {
        off_t writable = perform_seek();
        if (writable == INVALID_OFFSET)
            return -1;

        off_t writesize = ((off_t)count > writable) ? writable : (off_t)count;
        off_t offset = current_offset & (header.block_size - 1);
        memcpy(block_data + offset, cbuf, (size_t)writesize);

        current_offset += writesize;
        total += (long)writesize;
        cbuf += writesize;
        count -= (size_t)writesize;
        is_dirty = true;
        }

    return total;
}

bool vbox_image_t::read_header()
{
    if (wpb_lseek(file_descriptor, 0, SEEK_SET) < 0)
        return false;
    if (wpb_read(file_descriptor, &header, sizeof(VDI_Header)) != (long long)sizeof(VDI_Header))
        return false;

    if (header.signature != VDI_SIGNATURE) {
        BX_DEBUG(("not a vbox vdi image"));
        return false;
        }
    if (header.image_type < 1 || header.image_type > 2) {
        BX_DEBUG(("unsupported vbox vdi image type"));
        return false;
        }

    return true;
}

off_t vbox_image_t::perform_seek()
{
    if (current_offset == INVALID_OFFSET)
        return INVALID_OFFSET;

    Bit32u index = (Bit32u)(current_offset / header.block_size);

    if (index == mtlb_sector)
        return header.block_size - (current_offset & (header.block_size - 1));

    flush();
    read_block(index);
    mtlb_sector = index;

    return header.block_size;
}

void vbox_image_t::flush()
{
    if (!is_dirty)
        return;

    write_block(mtlb_sector);
    is_dirty = false;
}

void vbox_image_t::read_block(Bit32u index)
{
    if (mtlb[index] == -1) {
        memset(block_data, 0, header.block_size);
        return;
        }

    Bit64u offset = (Bit64u)mtlb[index] * header.block_size;
    wpb_lseek(file_descriptor, (long long)(header.offset_data + offset), SEEK_SET);
    wpb_read(file_descriptor, block_data, header.block_size);
}

void vbox_image_t::write_block(Bit32u index)
{
    if (mtlb[index] == -1) {
        mtlb[index] = (Bit32s)header.blocks_allocated++;
        mtlb_dirty = true;
        }

    Bit64u offset = (Bit64u)mtlb[index] * header.block_size;
    wpb_lseek(file_descriptor, (long long)(header.offset_data + offset), SEEK_SET);
    wpb_write(file_descriptor, block_data, header.block_size);
}
