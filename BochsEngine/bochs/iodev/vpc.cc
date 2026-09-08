#define BX_PLUGGABLE

#include "bochs.h"
#include "wpb_file_io.h"

extern bx_hard_drive_c *theHardDrive;
#define LOG_THIS theHardDrive->

static Bit16u vpc_be16(Bit16u v)
{
    return (Bit16u)((v >> 8) | (v << 8));
}

static Bit32u vpc_be32(Bit32u v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}

static Bit64u vpc_be64(Bit64u v)
{
    return ((Bit64u)vpc_be32((Bit32u)(v & 0xFFFFFFFF)) << 32) | (Bit64u)vpc_be32((Bit32u)(v >> 32));
}

int vpc_image_t::open(const char* _pathname)
{
    int flags = O_RDWR;
#ifdef O_BINARY
    flags |= O_BINARY;
#endif

    pathname = _pathname;
    file_descriptor = wpb_open(pathname, flags);
    if (file_descriptor < 0)
        return -1;

    if (wpb_lseek(file_descriptor, 0, SEEK_SET) < 0)
        return -1;
    if (wpb_read(file_descriptor, footer_buf, VPC_HEADER_SIZE) != VPC_HEADER_SIZE)
        return -1;

    VHD_Footer *footer = (VHD_Footer*)footer_buf;
    if (memcmp(footer->creator, "conectix", 8) != 0) {
        BX_DEBUG(("not a vhd image '%s'", pathname));
        return -1;
        }

    Bit32u disk_type = vpc_be32(footer->type);
    if (disk_type != 3) {
        BX_PANIC(("vpc: only dynamically-allocated vhd images are supported ('%s')", pathname));
        return -1;
        }

    cylinders = vpc_be16(footer->cyls);
    heads = footer->heads;
    sectors = footer->secs_per_cyl;
    sector_count = (off_t)cylinders * heads * sectors;

    Bit64u data_offset = vpc_be64(footer->data_offset);
    Bit8u dyn_buf[VPC_HEADER_SIZE];
    if (wpb_lseek(file_descriptor, (long long)data_offset, SEEK_SET) < 0)
        return -1;
    if (wpb_read(file_descriptor, dyn_buf, VPC_HEADER_SIZE) != VPC_HEADER_SIZE)
        return -1;

    VHD_DynHeader *dyn = (VHD_DynHeader*)dyn_buf;
    if (memcmp(dyn->magic, "cxsparse", 8) != 0) {
        BX_PANIC(("vpc: invalid dynamic disk header in '%s'", pathname));
        return -1;
        }

    block_size = vpc_be32(dyn->block_size);
    bitmap_size = ((block_size / (8 * 512)) + 511) & ~511;
    max_table_entries = vpc_be32(dyn->max_table_entries);
    bat_offset = vpc_be64(dyn->table_offset);

    pagetable = new Bit32u[max_table_entries];
    if (pagetable == 0)
        BX_PANIC(("vpc: unable to allocate BAT for '%s'", pathname));

    if (wpb_lseek(file_descriptor, (long long)bat_offset, SEEK_SET) < 0)
        return -1;
    if (wpb_read(file_descriptor, pagetable, (long long)max_table_entries * 4) != (long long)max_table_entries * 4)
        return -1;

    free_data_block_offset = (bat_offset + ((Bit64u)max_table_entries * 4) + 511) & ~(Bit64u)511;
    for (Bit32u i = 0; i < max_table_entries; i++) {
        pagetable[i] = vpc_be32(pagetable[i]);
        if (pagetable[i] != 0xFFFFFFFF) {
            Bit64u next = (512ULL * (Bit64u)pagetable[i]) + bitmap_size + block_size;
            if (next > free_data_block_offset)
                free_data_block_offset = next;
            }
        }

    last_bitmap_offset = (Bit64u)-1;
    cur_sector = 0;

    BX_INFO(("'vpc' disk image opened: path is '%s'", pathname));

    return 1;
}

void vpc_image_t::close()
{
    if (file_descriptor < 0)
        return;

    delete [] pagetable;
    pagetable = 0;

    wpb_close(file_descriptor);
    file_descriptor = -1;
}

off_t vpc_image_t::lseek(off_t offset, int whence)
{
    if (whence == SEEK_SET)
        cur_sector = offset / 512;
    else if (whence == SEEK_CUR)
        cur_sector += offset / 512;
    else if (whence == SEEK_END)
        cur_sector = sector_count + offset / 512;
    else {
        BX_DEBUG(("vpc: unsupported seek mode %d", whence));
        return -1;
        }

    if (cur_sector < 0 || cur_sector > sector_count)
        return -1;

    return cur_sector * 512;
}

ssize_t vpc_image_t::read(void* buf, size_t count)
{
    char *cbuf = (char*)buf;
    Bit32u scount = (Bit32u)(count / 512);
    off_t sectors_per_block = block_size >> 9;

    while (scount > 0) {
        off_t offset = get_sector_offset(cur_sector, false);
        off_t avail = sectors_per_block - (cur_sector % sectors_per_block);
        if (avail > (off_t)scount)
            avail = scount;

        if (offset == -1) {
            memset(cbuf, 0, (size_t)(avail * 512));
            }
        else {
            if (wpb_lseek(file_descriptor, offset, SEEK_SET) < 0)
                return -1;
            if (wpb_read(file_descriptor, cbuf, (long long)avail * 512) != (long long)avail * 512)
                return -1;
            }

        scount -= (Bit32u)avail;
        cur_sector += avail;
        cbuf += avail * 512;
        }

    return (ssize_t)count;
}

ssize_t vpc_image_t::write(const void* buf, size_t count)
{
    char *cbuf = (char*)buf;
    Bit32u scount = (Bit32u)(count / 512);
    off_t sectors_per_block = block_size >> 9;

    while (scount > 0) {
        off_t offset = get_sector_offset(cur_sector, true);
        off_t avail = sectors_per_block - (cur_sector % sectors_per_block);
        if (avail > (off_t)scount)
            avail = scount;

        if (offset == -1) {
            offset = alloc_block(cur_sector);
            if (offset < 0)
                return -1;
            }

        if (wpb_lseek(file_descriptor, offset, SEEK_SET) < 0)
            return -1;
        if (wpb_write(file_descriptor, cbuf, (long long)avail * 512) != (long long)avail * 512)
            return -1;

        scount -= (Bit32u)avail;
        cur_sector += avail;
        cbuf += avail * 512;
        }

    return (ssize_t)count;
}

off_t vpc_image_t::get_sector_offset(off_t sector_num, bool for_write)
{
    Bit64u offset = (Bit64u)sector_num * 512;
    Bit32u pagetable_index = (Bit32u)(offset / block_size);
    Bit32u pageentry_index = (Bit32u)((offset % block_size) / 512);

    if (pagetable_index >= max_table_entries || pagetable[pagetable_index] == 0xFFFFFFFF)
        return -1;

    Bit64u bitmap_offset = 512ULL * (Bit64u)pagetable[pagetable_index];
    Bit64u block_offset = bitmap_offset + bitmap_size + (512ULL * pageentry_index);

    if (for_write && last_bitmap_offset != bitmap_offset) {
        Bit8u *bitmap = new Bit8u[bitmap_size];
        last_bitmap_offset = bitmap_offset;
        memset(bitmap, 0xFF, bitmap_size);
        wpb_lseek(file_descriptor, (long long)bitmap_offset, SEEK_SET);
        wpb_write(file_descriptor, bitmap, bitmap_size);
        delete [] bitmap;
        }

    return (off_t)block_offset;
}

void vpc_image_t::rewrite_footer()
{
    wpb_lseek(file_descriptor, (long long)free_data_block_offset, SEEK_SET);
    wpb_write(file_descriptor, footer_buf, VPC_HEADER_SIZE);
}

off_t vpc_image_t::alloc_block(off_t sector_num)
{
    if (sector_num < 0 || sector_num > sector_count)
        return -1;

    Bit32u index = (Bit32u)(((Bit64u)sector_num * 512) / block_size);
    if (pagetable[index] != 0xFFFFFFFF)
        return -1;

    pagetable[index] = (Bit32u)(free_data_block_offset / 512);

    Bit8u *bitmap = new Bit8u[bitmap_size];
    memset(bitmap, 0xFF, bitmap_size);
    wpb_lseek(file_descriptor, (long long)free_data_block_offset, SEEK_SET);
    wpb_write(file_descriptor, bitmap, bitmap_size);
    delete [] bitmap;

    free_data_block_offset += (Bit64u)block_size + bitmap_size;
    rewrite_footer();

    Bit32u bat_value = vpc_be32(pagetable[index]);
    wpb_lseek(file_descriptor, (long long)(bat_offset + (4 * index)), SEEK_SET);
    wpb_write(file_descriptor, &bat_value, 4);

    return get_sector_offset(sector_num, false);
}
