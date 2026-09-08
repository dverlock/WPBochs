#ifndef _VBOX_H
#define _VBOX_H 1

#define VDI_SIGNATURE 0xBEDA107F

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif
typedef struct _VDI_Header {
    Bit8u  id[64];
    Bit32u signature;
    Bit32u version;
    Bit32u header_size;
    Bit32u image_type;
    Bit32u flags;
    Bit8u  description[256];
    Bit32u offset_blocks;
    Bit32u offset_data;
    Bit32u cylinders;
    Bit32u heads;
    Bit32u sectors;
    Bit32u sector_size;
    Bit32u resv;
    Bit64u disk_size;
    Bit32u block_size;
    Bit32u block_extra;
    Bit32u blocks_in_hdd;
    Bit32u blocks_allocated;
    Bit8u  uuid_this[16];
    Bit8u  uuid_snap[16];
    Bit8u  uuid_link[16];
    Bit8u  uuid_parent[16];
    Bit8u  padding[56];
} VDI_Header
#if !defined(_MSC_VER)
    __attribute__((packed))
#endif
;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

class vbox_image_t : public device_image_t
{
  public:
      vbox_image_t() : file_descriptor(-1), pathname(0), mtlb(0), block_data(0),
        current_offset(INVALID_OFFSET), mtlb_sector((Bit32u)-1), is_dirty(false),
        mtlb_dirty(false)
      { };
      int open (const char* pathname);
      void close();
      off_t lseek (off_t offset, int whence);
      ssize_t read (void* buf, size_t count);
      ssize_t write (const void* buf, size_t count);

  private:
      static const off_t INVALID_OFFSET;

      bool read_header();
      off_t perform_seek();
      void flush();
      void read_block(Bit32u index);
      void write_block(Bit32u index);

      int file_descriptor;
      const char * pathname;
      VDI_Header header;
      Bit32s * mtlb;
      Bit8u * block_data;
      off_t current_offset;
      Bit32u mtlb_sector;
      bool is_dirty;
      bool mtlb_dirty;
};
#endif
