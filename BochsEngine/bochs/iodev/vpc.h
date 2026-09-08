#ifndef _VPC_H
#define _VPC_H 1

#define VPC_HEADER_SIZE 512

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif
typedef struct _VHD_Footer {
    char    creator[8];
    Bit32u  features;
    Bit32u  version;
    Bit64u  data_offset;
    Bit32u  timestamp;
    char    creator_app[4];
    Bit16u  major;
    Bit16u  minor;
    char    creator_os[4];
    Bit64u  orig_size;
    Bit64u  size;
    Bit16u  cyls;
    Bit8u   heads;
    Bit8u   secs_per_cyl;
    Bit32u  type;
    Bit32u  checksum;
    Bit8u   uuid[16];
    Bit8u   in_saved_state;
} VHD_Footer
#if !defined(_MSC_VER)
    __attribute__((packed))
#endif
;

typedef struct _VHD_DynHeader {
    Bit8u   magic[8];
    Bit64u  data_offset;
    Bit64u  table_offset;
    Bit32u  version;
    Bit32u  max_table_entries;
    Bit32u  block_size;
    Bit32u  checksum;
    Bit8u   parent_uuid[16];
    Bit32u  parent_timestamp;
    Bit32u  reserved;
    Bit8u   parent_name[512];
    Bit8u   parent_locator[192];
} VHD_DynHeader
#if !defined(_MSC_VER)
    __attribute__((packed))
#endif
;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

class vpc_image_t : public device_image_t
{
  public:
      vpc_image_t() : file_descriptor(-1), pagetable(0), max_table_entries(0),
        bat_offset(0), free_data_block_offset(0), last_bitmap_offset((Bit64u)-1),
        block_size(0), bitmap_size(0), sector_count(0), cur_sector(0)
      { };
      int open (const char* pathname);
      void close();
      off_t lseek (off_t offset, int whence);
      ssize_t read (void* buf, size_t count);
      ssize_t write (const void* buf, size_t count);

  private:
      off_t get_sector_offset(off_t sector_num, bool for_write);
      off_t alloc_block(off_t sector_num);
      void rewrite_footer();

      int file_descriptor;
      const char * pathname;
      Bit8u footer_buf[VPC_HEADER_SIZE];
      Bit32u * pagetable;
      Bit32u max_table_entries;
      Bit64u bat_offset;
      Bit64u free_data_block_offset;
      Bit64u last_bitmap_offset;
      Bit32u block_size;
      Bit32u bitmap_size;
      off_t sector_count;
      off_t cur_sector;
};
#endif
