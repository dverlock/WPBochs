//
// PIIX3 Bus Master IDE PCI function (device 1, function 1)
//

#define BX_PLUGGABLE

#include "bochs.h"

#define LOG_THIS thePciIdeController->

#define BMIDE_IO_BASE 0xB400

bx_pci_ide_c *thePciIdeController = NULL;

  int
libpci_ide_LTX_plugin_init(plugin_t *plugin, plugintype_t type, int argc, char *argv[])
{
  thePciIdeController = new bx_pci_ide_c ();
  bx_devices.pluginPciIdeController = thePciIdeController;
  BX_REGISTER_DEVICE_DEVMODEL(plugin, type, thePciIdeController, BX_PLUGIN_PCI_IDE);
  return(0);
}

  void
libpci_ide_LTX_plugin_fini(void)
{
}

bx_pci_ide_c::bx_pci_ide_c(void)
{
  put("PIDE");
  settype(PCIIDELOG);
}

bx_pci_ide_c::~bx_pci_ide_c(void)
{
}

  void
bx_pci_ide_c::init(void)
{
  DEV_register_pci_handlers(this, pci_read_handler, pci_write_handler,
                            BX_PCI_DEVICE(1,1), "PIIX3 Bus Master IDE");

  for (unsigned i=0; i<16; i++)
    DEV_register_iowrite_handler(this, write_handler, BMIDE_IO_BASE+i, "PIIX3 Bus Master IDE", 1);
  for (unsigned i=0; i<16; i++)
    DEV_register_ioread_handler(this, read_handler, BMIDE_IO_BASE+i, "PIIX3 Bus Master IDE", 1);

  for (unsigned i=0; i<256; i++)
    BX_PIDE_THIS s.pci_conf[i] = 0x0;

  BX_PIDE_THIS s.pci_conf[0x00] = 0x86;
  BX_PIDE_THIS s.pci_conf[0x01] = 0x80;
  BX_PIDE_THIS s.pci_conf[0x02] = 0x10;
  BX_PIDE_THIS s.pci_conf[0x03] = 0x70;
  BX_PIDE_THIS s.pci_conf[0x09] = 0x80;
  BX_PIDE_THIS s.pci_conf[0x0a] = 0x01;
  BX_PIDE_THIS s.pci_conf[0x0b] = 0x01;
  BX_PIDE_THIS s.pci_conf[0x0e] = 0x00;
  BX_PIDE_THIS s.pci_conf[0x20] = (BMIDE_IO_BASE & 0xff) | 0x01;
  BX_PIDE_THIS s.pci_conf[0x21] = (BMIDE_IO_BASE >> 8) & 0xff;
  BX_PIDE_THIS s.pci_conf[0x22] = 0x00;
  BX_PIDE_THIS s.pci_conf[0x23] = 0x00;

  for (unsigned i=0; i<16; i++)
    BX_PIDE_THIS s.bmregs[i] = 0x00;
}

  void
bx_pci_ide_c::reset(unsigned type)
{
  BX_PIDE_THIS s.pci_conf[0x04] = 0x00;
  BX_PIDE_THIS s.pci_conf[0x05] = 0x00;
  BX_PIDE_THIS s.pci_conf[0x06] = 0x80;
  BX_PIDE_THIS s.pci_conf[0x07] = 0x02;

  for (unsigned i=0; i<16; i++)
    BX_PIDE_THIS s.bmregs[i] = 0x00;
}

  Bit32u
bx_pci_ide_c::read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
#if !BX_USE_PIDE_SMF
  bx_pci_ide_c *class_ptr = (bx_pci_ide_c *) this_ptr;
  return class_ptr->read(address, io_len);
}

  Bit32u
bx_pci_ide_c::read(Bit32u address, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif
  unsigned reg = address - BMIDE_IO_BASE;
  if (reg < 16)
    return BX_PIDE_THIS s.bmregs[reg];
  return 0xff;
}

  void
bx_pci_ide_c::write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
#if !BX_USE_PIDE_SMF
  bx_pci_ide_c *class_ptr = (bx_pci_ide_c *) this_ptr;
  class_ptr->write(address, value, io_len);
}

  void
bx_pci_ide_c::write(Bit32u address, Bit32u value, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif
  unsigned reg = address - BMIDE_IO_BASE;
  if (reg < 16)
    BX_PIDE_THIS s.bmregs[reg] = (Bit8u)value;
}

  Bit32u
bx_pci_ide_c::pci_read_handler(void *this_ptr, Bit8u address, unsigned io_len)
{
#if !BX_USE_PIDE_SMF
  bx_pci_ide_c *class_ptr = (bx_pci_ide_c *) this_ptr;
  return class_ptr->pci_read(address, io_len);
}

  Bit32u
bx_pci_ide_c::pci_read(Bit8u address, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif
  Bit32u value = 0;
  for (unsigned i=0; i<io_len; i++) {
    if ((unsigned)(address+i) > 255) break;
    value |= (BX_PIDE_THIS s.pci_conf[address+i] << (i*8));
  }
  return value;
}

  void
bx_pci_ide_c::pci_write_handler(void *this_ptr, Bit8u address, Bit32u value, unsigned io_len)
{
#if !BX_USE_PIDE_SMF
  bx_pci_ide_c *class_ptr = (bx_pci_ide_c *) this_ptr;
  class_ptr->pci_write(address, value, io_len);
}

  void
bx_pci_ide_c::pci_write(Bit8u address, Bit32u value, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif
  for (unsigned i=0; i<io_len; i++) {
    unsigned reg = address+i;
    if (reg > 255) break;
    Bit8u value8 = (Bit8u)(value >> (i*8));
    switch (reg) {
      case 0x04:
        BX_PIDE_THIS s.pci_conf[reg] = value8 & 0x05;
        break;
      case 0x20:
        BX_PIDE_THIS s.pci_conf[reg] = (value8 & 0xf0) | 0x01;
        break;
      case 0x21:
        BX_PIDE_THIS s.pci_conf[reg] = value8;
        break;
      default:
        break;
    }
  }
}
