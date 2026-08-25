/*
 * This file provides support for VMWare's virtual disk image
 * format version 4 and above.
 *
 * Author: Sharvil Nanavati
 * Contact: snrrrub@gmail.com
 *
 * Copyright (C) 2006 Sharvil Nanavati.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE

#include "bochs.h"
#include "wpb_file_io.h"

const off_t vmware4_image_t::INVALID_OFFSET = (off_t)-1;
const unsigned vmware4_image_t::SECTOR_SIZE = 512;

extern bx_hard_drive_c *theHardDrive;
#define LOG_THIS theHardDrive->

int vmware4_image_t::open(const char* _pathname)
{
    int flags = O_RDWR;
#ifdef O_BINARY
    flags |= O_BINARY;
#endif

    pathname = _pathname;

    file_descriptor = wpb_open(pathname, flags);
    if (file_descriptor < 0)
        return -1;

    if (!read_header())
        BX_PANIC(("unable to read vmware4 virtual disk header from file '%s'", pathname));

    tlb = new char [(size_t)(header.tlb_size_sectors * SECTOR_SIZE)];
    if (tlb == 0)
        BX_PANIC(("unable to allocate memory for vmware4 image's tlb in file '%s'", pathname));

    tlb_offset = INVALID_OFFSET;
    current_offset = 0;
    is_dirty = false;

    Bit64u grains_total = (header.total_sectors + header.tlb_size_sectors - 1) / header.tlb_size_sectors;
    gt_count = (unsigned)((grains_total + header.slb_count - 1) / header.slb_count);
    flb_cache = new Bit32u [gt_count];
    flb_copy_cache = new Bit32u [gt_count];
    slb_cache = new Bit32u [header.slb_count];
    if (flb_cache == 0 || flb_copy_cache == 0 || slb_cache == 0)
        BX_PANIC(("unable to allocate memory for vmware4 image's grain tables in file '%s'", pathname));

    wpb_lseek(file_descriptor, (long long)header.flb_offset_sectors * SECTOR_SIZE, SEEK_SET);
    wpb_read(file_descriptor, flb_cache, (long long)gt_count * sizeof(Bit32u));
    wpb_lseek(file_descriptor, (long long)header.flb_copy_offset_sectors * SECTOR_SIZE, SEEK_SET);
    wpb_read(file_descriptor, flb_copy_cache, (long long)gt_count * sizeof(Bit32u));

    current_flb_index = 0;
    slb_cache_loaded = false;
    slb_cache_dirty = false;

    cylinders = (unsigned)(header.total_sectors / (16 * 63));
    heads = 16;
    sectors = 63;

    return 1;
}

void vmware4_image_t::close()
{
    if (file_descriptor < 0)
        return;

    flush();
    flush_slb_cache();
    delete [] tlb;
    tlb = 0;
    delete [] flb_cache;
    flb_cache = 0;
    delete [] flb_copy_cache;
    flb_copy_cache = 0;
    delete [] slb_cache;
    slb_cache = 0;

    wpb_close(file_descriptor);
    file_descriptor = -1;
}

off_t vmware4_image_t::lseek(off_t offset, int whence)
{
    switch (whence) {
      case SEEK_SET:
        current_offset = offset;
        return current_offset;
      case SEEK_CUR:
        current_offset += offset;
        return current_offset;
      case SEEK_END:
        current_offset = (off_t)(header.total_sectors * SECTOR_SIZE) + offset;
        return current_offset;
      default:
        BX_DEBUG(("unknown 'whence' value (%d) when trying to seek vmware4 image", whence));
        return INVALID_OFFSET;
    }
}

ssize_t vmware4_image_t::read(void * buf, size_t count)
{
    char *cbuf = (char*)buf;
    ssize_t total = 0;
    while (count > 0) {
        off_t readable = perform_seek();
        if (readable == INVALID_OFFSET) {
            BX_DEBUG(("vmware4 disk image read failed on %u bytes at %ld", (unsigned)count, (long)current_offset));
            return -1;
            }

        off_t copysize = ((off_t)count > readable) ? readable : (off_t)count;
        memcpy(cbuf, tlb + current_offset - tlb_offset, (size_t)copysize);

        current_offset += copysize;
        total += (long)copysize;
        cbuf += copysize;
        count -= (size_t)copysize;
        }
    return total;
}

ssize_t vmware4_image_t::write(const void * buf, size_t count)
{
    char *cbuf = (char*)buf;
    ssize_t total = 0;
    while (count > 0) {
        off_t writable = perform_seek();
        if (writable == INVALID_OFFSET) {
            BX_DEBUG(("vmware4 disk image write failed on %u bytes at %ld", (unsigned)count, (long)current_offset));
            return -1;
            }

        off_t writesize = ((off_t)count > writable) ? writable : (off_t)count;
        memcpy(tlb + current_offset - tlb_offset, cbuf, (size_t)writesize);

        current_offset += writesize;
        total += (long)writesize;
        cbuf += writesize;
        count -= (size_t)writesize;
        is_dirty = true;
        }
    return total;
}

bool vmware4_image_t::read_header()
{
    if (wpb_lseek(file_descriptor, 0, SEEK_SET) < 0)
        return false;
    if (wpb_read(file_descriptor, &header, sizeof(VM4_Header)) != (long long)sizeof(VM4_Header))
        return false;

    if (header.id[0] != 'K' || header.id[1] != 'D' || header.id[2] != 'M' || header.id[3] != 'V') {
        BX_DEBUG(("not a vmware4 image"));
        return false;
        }
    if (header.version != 1) {
        BX_DEBUG(("unsupported vmware4 image version"));
        return false;
        }

    return true;
}

//
// Returns the number of bytes that can be read from the current offset before needing
// to perform another seek.
//
off_t vmware4_image_t::perform_seek()
{
    if (current_offset == INVALID_OFFSET) {
        BX_DEBUG(("invalid offset specified in vmware4 seek"));
        return INVALID_OFFSET;
        }

    off_t tlb_bytes = (off_t)(header.tlb_size_sectors * SECTOR_SIZE);

    if (tlb_offset != INVALID_OFFSET && (tlb_offset / tlb_bytes) == (current_offset / tlb_bytes))
        return tlb_bytes - (current_offset - tlb_offset);

    flush();

    Bit64u index = (Bit64u)current_offset / (Bit64u)tlb_bytes;
    unsigned slb_index = (unsigned)(index % header.slb_count);
    unsigned flb_index = (unsigned)(index / header.slb_count);

    ensure_slb_cache(flb_index);
    if (current_slb_sector == 0 && current_slb_copy_sector == 0) {
        BX_DEBUG(("loaded vmware4 disk image requires un-implemented feature"));
        return INVALID_OFFSET;
        }

    Bit32u tlb_sector = slb_cache[slb_index];
    tlb_offset = (off_t)(index * (header.tlb_size_sectors * SECTOR_SIZE));

    if (tlb_sector == 0) {
        //
        // Allocate a new tlb
        //
        memset(tlb, 0, (size_t)(header.tlb_size_sectors * SECTOR_SIZE));

        //
        // Instead of doing a write to increase the file size, we could use
        // ftruncate but it is not portable.
        //
        off_t eof = (off_t)(((wpb_lseek(file_descriptor, 0, SEEK_END) + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE);
        wpb_write(file_descriptor, tlb, (long long)(header.tlb_size_sectors * SECTOR_SIZE));
        tlb_sector = (Bit32u)(eof / SECTOR_SIZE);

        slb_cache[slb_index] = tlb_sector;
        slb_cache_dirty = true;

        wpb_lseek(file_descriptor, eof, SEEK_SET);
        }
    else {
        wpb_lseek(file_descriptor, (long long)tlb_sector * SECTOR_SIZE, SEEK_SET);
        wpb_read(file_descriptor, tlb, (long long)(header.tlb_size_sectors * SECTOR_SIZE));
        wpb_lseek(file_descriptor, (long long)tlb_sector * SECTOR_SIZE, SEEK_SET);
        }

    return tlb_bytes - (current_offset - tlb_offset);
}

void vmware4_image_t::flush()
{
    if (!is_dirty)
        return;

    //
    // Write dirty sectors to disk first. Assume that the file is already at the
    // position for the current tlb.
    //
    wpb_write(file_descriptor, tlb, (long long)(header.tlb_size_sectors * SECTOR_SIZE));
    is_dirty = false;
}

void vmware4_image_t::flush_slb_cache()
{
    if (!slb_cache_loaded || !slb_cache_dirty)
        return;

    long long slb_bytes = (long long)header.slb_count * sizeof(Bit32u);
    if (current_slb_sector != 0) {
        wpb_lseek(file_descriptor, (long long)current_slb_sector * SECTOR_SIZE, SEEK_SET);
        wpb_write(file_descriptor, slb_cache, slb_bytes);
        }
    if (current_slb_copy_sector != 0 && current_slb_copy_sector != current_slb_sector) {
        wpb_lseek(file_descriptor, (long long)current_slb_copy_sector * SECTOR_SIZE, SEEK_SET);
        wpb_write(file_descriptor, slb_cache, slb_bytes);
        }
    slb_cache_dirty = false;
}

void vmware4_image_t::ensure_slb_cache(unsigned flb_index)
{
    if (slb_cache_loaded && flb_index == current_flb_index)
        return;

    flush_slb_cache();

    Bit32u slb_sector = flb_cache[flb_index];
    Bit32u slb_copy_sector = flb_copy_cache[flb_index];
    if (slb_sector == 0)
        slb_sector = slb_copy_sector;

    current_slb_sector = slb_sector;
    current_slb_copy_sector = slb_copy_sector;
    current_flb_index = flb_index;

    if (slb_sector == 0 && slb_copy_sector == 0) {
        memset(slb_cache, 0, (size_t)header.slb_count * sizeof(Bit32u));
        slb_cache_loaded = false;
        return;
        }

    wpb_lseek(file_descriptor, (long long)slb_sector * SECTOR_SIZE, SEEK_SET);
    wpb_read(file_descriptor, slb_cache, (long long)header.slb_count * sizeof(Bit32u));
    slb_cache_loaded = true;
}
