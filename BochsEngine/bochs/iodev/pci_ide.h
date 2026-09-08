#ifndef BX_IODEV_PCI_IDE_H
#define BX_IODEV_PCI_IDE_H

#if BX_USE_PIDE_SMF
#  define BX_PIDE_SMF  static
#  define BX_PIDE_THIS thePciIdeController->
#  define BX_PIDE_THIS_PTR thePciIdeController
#else
#  define BX_PIDE_SMF
#  define BX_PIDE_THIS this->
#  define BX_PIDE_THIS_PTR this
#endif

class bx_pci_ide_c : public bx_devmodel_c {
public:
  bx_pci_ide_c(void);
  ~bx_pci_ide_c(void);
  virtual void init(void);
  virtual void reset(unsigned type);

private:
  static Bit32u read_handler(void *this_ptr, Bit32u address, unsigned io_len);
  static void   write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len);
  static Bit32u pci_read_handler(void *this_ptr, Bit8u address, unsigned io_len);
  static void   pci_write_handler(void *this_ptr, Bit8u address, Bit32u value, unsigned io_len);
#if !BX_USE_PIDE_SMF
  Bit32u read(Bit32u address, unsigned io_len);
  void   write(Bit32u address, Bit32u value, unsigned io_len);
  Bit32u pci_read(Bit8u address, unsigned io_len);
  void   pci_write(Bit8u address, Bit32u value, unsigned io_len);
#endif

  struct {
    Bit8u pci_conf[256];
    Bit8u bmregs[16];
  } s;
};

#endif
