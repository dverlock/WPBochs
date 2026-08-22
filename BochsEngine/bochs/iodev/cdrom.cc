/////////////////////////////////////////////////////////////////////////
// $Id: cdrom.cc,v 1.30.2.1 2002/06/10 21:15:44 cbothamy Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2002  MandrakeSoft S.A.
//
//    MandrakeSoft S.A.
//    43, rue d'Aboukir
//    75002 Paris - France
//    http://www.linux-mandrake.com/
//    http://www.mandrakesoft.com/
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA


// These are the low-level CDROM functions which are called
// from 'harddrv.cc'.  They effect the OS specific functionality
// needed by the CDROM emulation in 'harddrv.cc'.  Mostly, just
// ioctl() calls and such.  Should be fairly easy to add support
// for your OS if it is not supported yet.

#include "bochs.h"
#include "wpb_file_io.h"

#define LOG_THIS

extern "C" {
#include <errno.h>
}

#define BX_CD_FRAMESIZE 2048

#include <stdio.h>

cdrom_interface::cdrom_interface(char *dev)
{
  put("CD");
  settype(CDLOG);
  fd = -1; // File descriptor not yet allocated

  if ( dev == NULL )
    path = NULL;
  else {
    path = strdup(dev);
  }
  using_file=0;
}

void
cdrom_interface::init(void) {
  BX_DEBUG(("Init $Id: cdrom.cc,v 1.30.2.1 2002/06/10 21:15:44 cbothamy Exp $"));
  BX_INFO(("file = '%s'",path));
}

cdrom_interface::~cdrom_interface(void)
{
	if (fd >= 0)
		wpb_close(fd);
	if (path)
		free(path);
	BX_DEBUG(("Exit"));
}

  bx_bool
cdrom_interface::insert_cdrom(char *dev)
{
  unsigned char buffer[BX_CD_FRAMESIZE];
  ssize_t ret;

  // Load CD-ROM. Returns false if CD is not ready.
  if (dev != NULL) path = strdup(dev);
  BX_INFO (("load cdrom with path=%s", path));

  fd = wpb_open(path, O_RDONLY | _O_BINARY);
  if (fd < 0) {
     BX_ERROR(( "open cd failed for %s: %s", path, strerror(errno)));
     return(0);
  }

  using_file = 1;
  BX_INFO (("Opening image file %s as a cd.", path));

  // I just see if I can read a sector to verify that a
  // CD is in the drive and readable.
  ret = (ssize_t)wpb_read(fd, &buffer, BX_CD_FRAMESIZE);
  if (ret < 0) {
     wpb_close(fd);
     fd = -1;
     BX_DEBUG(( "insert_cdrom: read returns error: %s", strerror (errno) ));
     return(0);
  }
  return(1);
}


  void
cdrom_interface::eject_cdrom()
{
  // Logically eject the CD.

  if (fd >= 0) {
    wpb_close(fd);
    fd = -1;
    }
}


  bx_bool
cdrom_interface::read_toc(uint8* buf, int* length, bx_bool msf, int start_track)
{
  // Read CD TOC. Returns false if start track is out of bounds.
  // Single data track only, matching how our plain ISO images are exposed.

  if (fd < 0) {
    BX_PANIC(("cdrom: read_toc: file not open."));
    }

  if ((start_track > 1) && (start_track != 0xaa)) {
    return 0;
    }

  int len = 4;
  buf[2] = 1;
  buf[3] = 1;

  if (start_track <= 1) {
    buf[len++] = 0; // Reserved
    buf[len++] = 0x14; // ADR, control
    buf[len++] = 1; // Track number
    buf[len++] = 0; // Reserved

    if (msf) {
      buf[len++] = 0;
      buf[len++] = 0;
      buf[len++] = 2;
      buf[len++] = 0;
      } else {
      buf[len++] = 0;
      buf[len++] = 0;
      buf[len++] = 0;
      buf[len++] = 0;
      }
    }

  buf[len++] = 0; // Reserved
  buf[len++] = 0x16; // ADR, control
  buf[len++] = 0xaa; // Track number
  buf[len++] = 0; // Reserved

  uint32 blocks = capacity();

  if (msf) {
    buf[len++] = 0;
    buf[len++] = (uint8)(((blocks + 150) / 75) / 60);
    buf[len++] = (uint8)(((blocks + 150) / 75) % 60);
    buf[len++] = (uint8)((blocks + 150) % 75);
    } else {
    buf[len++] = (blocks >> 24) & 0xff;
    buf[len++] = (blocks >> 16) & 0xff;
    buf[len++] = (blocks >> 8) & 0xff;
    buf[len++] = (blocks >> 0) & 0xff;
    }

  buf[0] = ((len-2) >> 8) & 0xff;
  buf[1] = (len-2) & 0xff;

  *length = len;
  return 1;
}


  uint32
cdrom_interface::capacity()
{
  long long size = wpb_length(fd);
  if (size < 0) {
     BX_PANIC (("fstat on cdrom image returned err: %s", strerror(errno)));
  }
  BX_INFO (("cdrom size is %ld bytes", (long) size));
  if ((size % 2048) != 0)  {
    BX_ERROR (("expected cdrom image to be a multiple of 2048 bytes"));
  }
  return (uint32)(size / 2048);
}

  void
cdrom_interface::read_block(uint8* buf, int lba)
{
  // Read a single block from the CD image file.

  long long pos;
  ssize_t n;

  pos = wpb_lseek(fd, (long long)lba*BX_CD_FRAMESIZE, SEEK_SET);
  if (pos < 0) {
    BX_PANIC(("cdrom: read_block: lseek returned error."));
  }
  n = (ssize_t)wpb_read(fd, buf, BX_CD_FRAMESIZE);

  if (n != BX_CD_FRAMESIZE) {
    BX_PANIC(("cdrom: read_block: read returned %d",
      (int) n));
    }
}

  int
cdrom_interface::start_cdrom()
{
  // Spinning up a real drive is a no-op for an image file.
  return 1;
}
