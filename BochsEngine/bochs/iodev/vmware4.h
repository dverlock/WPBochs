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

#ifndef _VMWARE4_H
#define _VMWARE4_H 1

class vmware4_image_t : public device_image_t
{
  public:
      vmware4_image_t() : file_descriptor(-1), tlb(0), tlb_offset(INVALID_OFFSET),
        current_offset(INVALID_OFFSET), is_dirty(false), gt_count(0), flb_cache(0),
        flb_copy_cache(0), slb_cache(0), current_flb_index(0), slb_cache_loaded(false),
        slb_cache_dirty(false), current_slb_sector(0), current_slb_copy_sector(0)
      { };
      int open (const char* pathname);
      void close();
      off_t lseek (off_t offset, int whence);
      ssize_t read (void* buf, size_t count);
      ssize_t write (const void* buf, size_t count);

  private:
      static const off_t INVALID_OFFSET;
      static const unsigned SECTOR_SIZE;

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif
      typedef struct _VM4_Header {
          Bit8u  id[4];
          Bit32u version;
          Bit32u flags;
          Bit64u total_sectors;
          Bit64u tlb_size_sectors;
          Bit64u description_offset_sectors;
          Bit64u description_size_sectors;
          Bit32u slb_count;
          Bit64u flb_offset_sectors;
          Bit64u flb_copy_offset_sectors;
          Bit64u tlb_offset_sectors;
          Bit8u  filler;
          Bit8u  check_bytes[4];
      } VM4_Header
#if !defined(_MSC_VER)
        __attribute__((packed))
#endif
      ;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif
      typedef char vm4_header_size_check[(sizeof(VM4_Header) == 77) ? 1 : -1];

      bool read_header();
      off_t perform_seek();
      void flush();
      void ensure_slb_cache(unsigned flb_index);
      void flush_slb_cache();

      int file_descriptor;
      const char * pathname;
      VM4_Header header;
      char * tlb;
      off_t tlb_offset;
      off_t current_offset;
      bool is_dirty;

      unsigned gt_count;
      Bit32u * flb_cache;
      Bit32u * flb_copy_cache;
      Bit32u * slb_cache;
      unsigned current_flb_index;
      bool slb_cache_loaded;
      bool slb_cache_dirty;
      Bit64u current_slb_sector;
      Bit64u current_slb_copy_sector;
};
#endif
