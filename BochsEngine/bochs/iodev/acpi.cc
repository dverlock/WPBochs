/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2006  Volker Ruppert
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
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

//
// PIIX4 ACPI support
//

#define BX_PLUGGABLE

#include "bochs.h"
#if BX_PCI_SUPPORT

#define LOG_THIS theACPIController->

bx_acpi_ctrl_c* theACPIController = NULL;

const Bit8u acpi_pm_iomask[64] = {2, 0, 2, 0, 2, 0, 0, 0, 4, 0, 0, 0, 7, 7, 7, 7,
                                  7, 7, 7, 7, 1, 1, 0, 0, 7, 7, 0, 0, 7, 7, 7, 7,
                                  7, 7, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7,
                                  1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
const Bit8u acpi_sm_iomask[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0, 2, 0, 0, 0};

#define PM_FREQ 3579545

#define PM_IO_BASE 0xb000
#define SM_IO_BASE 0xb100

#define RSM_STS (1 << 15)
#define PWRBTN_STS (1 << 8)

#define RTC_EN (1 << 10)
#define PWRBTN_EN (1 << 8)
#define GBL_EN (1 << 5)
#define TMROF_EN (1 << 0)

#define SUS_EN (1 << 13)

  int
libacpi_LTX_plugin_init(plugin_t *plugin, plugintype_t type, int argc, char *argv[])
{
  theACPIController = new bx_acpi_ctrl_c();
  bx_devices.pluginACPIController = theACPIController;
  BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theACPIController, BX_PLUGIN_ACPI);
  return 0; // Success
}

  void
libacpi_LTX_plugin_fini(void)
{
  delete theACPIController;
}

/* ported from QEMU: compute with 96 bit intermediate result: (a*b)/c */
Bit64u muldiv64(Bit64u a, Bit32u b, Bit32u c)
{
  union {
    Bit64u ll;
    struct {
#if WORDS_BIGENDIAN
      Bit32u high, low;
#else
      Bit32u low, high;
#endif
    } l;
  } u, res;
  Bit64u rl, rh;

  u.ll = a;
  rl = (Bit64u)u.l.low * (Bit64u)b;
  rh = (Bit64u)u.l.high * (Bit64u)b;
  rh += (rl >> 32);
  rl &= 0xffffffff;

  res.l.high = (Bit32u)(rh / c);
  res.l.low = (Bit32u)((((rh % c) << 32) + rl) / c);

  return res.ll;
}

bx_acpi_ctrl_c::bx_acpi_ctrl_c(void)
{
  put("ACPI");
  settype(ACPILOG);
  s.timer_index = BX_NULL_TIMER_HANDLE;
}

bx_acpi_ctrl_c::~bx_acpi_ctrl_c(void)
{
  BX_DEBUG(("Exit"));
}

  void
bx_acpi_ctrl_c::init(void)
{
  // called once when bochs initializes

  unsigned i;

  DEV_register_pci_handlers(this, pci_read_handler, pci_write_handler,
                            BX_PCI_DEVICE(1,3), "PIIX4 ACPI Controller");

  if (BX_ACPI_THIS s.timer_index == BX_NULL_TIMER_HANDLE) {
    BX_ACPI_THIS s.timer_index =
      bx_pc_system.register_timer(this, timer_handler, 1000, 0, 0, "ACPI");
  }

  for (i=0; i<256; i++) {
    BX_ACPI_THIS s.pci_conf[i] = 0x0;
  }
  BX_ACPI_THIS s.pm_base = PM_IO_BASE;
  BX_ACPI_THIS s.sm_base = SM_IO_BASE;

  for (i=0; i<64; i++) {
    if (acpi_pm_iomask[i] != 0) {
      DEV_register_ioread_handler(this, read_handler, PM_IO_BASE+i, "ACPI", acpi_pm_iomask[i]);
      DEV_register_iowrite_handler(this, write_handler, PM_IO_BASE+i, "ACPI", acpi_pm_iomask[i]);
    }
  }
  for (i=0; i<16; i++) {
    if (acpi_sm_iomask[i] != 0) {
      DEV_register_ioread_handler(this, read_handler, SM_IO_BASE+i, "ACPI", acpi_sm_iomask[i]);
      DEV_register_iowrite_handler(this, write_handler, SM_IO_BASE+i, "ACPI", acpi_sm_iomask[i]);
    }
  }

  // readonly registers
  static const struct init_vals_t {
    unsigned      addr;
    unsigned char val;
  } init_vals[] = {
    { 0x00, 0x86 }, { 0x01, 0x80 },
    { 0x02, 0x13 }, { 0x03, 0x71 },
    { 0x08, 0x03 },                 // revision number
    { 0x0a, 0x80 },                 // other bridge device
    { 0x0b, 0x06 },                 // bridge device
    { 0x0e, 0x00 },                 // header type
    { 0x3d, BX_PCI_INTA }           // interrupt pin #1
  };
  for (i = 0; i < sizeof(init_vals) / sizeof(*init_vals); ++i) {
    BX_ACPI_THIS s.pci_conf[init_vals[i].addr] = init_vals[i].val;
  }

  // PM base 0x40 - 0x43
  BX_ACPI_THIS s.pci_conf[0x40] = (PM_IO_BASE | 1) & 0xff;
  BX_ACPI_THIS s.pci_conf[0x41] = (PM_IO_BASE | 1) >> 8;
  // SM base 0x90 - 0x93
  BX_ACPI_THIS s.pci_conf[0x90] = (SM_IO_BASE | 1) & 0xff;
  BX_ACPI_THIS s.pci_conf[0x91] = (SM_IO_BASE | 1) >> 8;
}

  void
bx_acpi_ctrl_c::reset(unsigned type)
{
  BX_ACPI_THIS s.pci_conf[0x04] = 0x00; // command_io + command_mem
  BX_ACPI_THIS s.pci_conf[0x05] = 0x00;
  BX_ACPI_THIS s.pci_conf[0x06] = 0x80; // status_devsel_medium
  BX_ACPI_THIS s.pci_conf[0x07] = 0x02;
  BX_ACPI_THIS s.pci_conf[0x3c] = 0x00; // IRQ

  // clear DEVACTB register on PIIX4 ACPI reset
  BX_ACPI_THIS s.pci_conf[0x58] = 0x00;
  BX_ACPI_THIS s.pci_conf[0x59] = 0x00;

  // device resources
  BX_ACPI_THIS s.pci_conf[0x5a] = 0x00;
  BX_ACPI_THIS s.pci_conf[0x5b] = 0x00;
  BX_ACPI_THIS s.pci_conf[0x5f] = 0x90;
  BX_ACPI_THIS s.pci_conf[0x63] = 0x60;
  BX_ACPI_THIS s.pci_conf[0x67] = 0x98;

  BX_ACPI_THIS s.pmsts = 0;
  BX_ACPI_THIS s.pmen = 0;
  BX_ACPI_THIS s.pmcntrl = 0;
  BX_ACPI_THIS s.tmr_overflow_time = 0xffffff;

  BX_ACPI_THIS s.smbus.stat = 0;
  BX_ACPI_THIS s.smbus.ctl = 0;
  BX_ACPI_THIS s.smbus.cmd = 0;
  BX_ACPI_THIS s.smbus.addr = 0;
  BX_ACPI_THIS s.smbus.data0 = 0;
  BX_ACPI_THIS s.smbus.data1 = 0;
  BX_ACPI_THIS s.smbus.index = 0;

  for (unsigned i = 0; i < 32; i++) {
    BX_ACPI_THIS s.smbus.data[i] = 0;
  }
}

  void
bx_acpi_ctrl_c::set_irq_level(bx_bool level)
{
  if (BX_ACPI_THIS s.pci_conf[0x3c] != 0) {
    if (level) {
      DEV_pic_raise_irq(BX_ACPI_THIS s.pci_conf[0x3c]);
    } else {
      DEV_pic_lower_irq(BX_ACPI_THIS s.pci_conf[0x3c]);
    }
  }
}

  Bit32u
bx_acpi_ctrl_c::get_pmtmr(void)
{
  Bit64u value = muldiv64(bx_pc_system.time_usec(), PM_FREQ, 1000000);
  return (Bit32u)(value & 0xffffff);
}

  Bit16u
bx_acpi_ctrl_c::get_pmsts(void)
{
  Bit16u pmsts = BX_ACPI_THIS s.pmsts;
  Bit64u value = muldiv64(bx_pc_system.time_usec(), PM_FREQ, 1000000);
  if (value >= BX_ACPI_THIS s.tmr_overflow_time)
    BX_ACPI_THIS s.pmsts |= TMROF_EN;
  return pmsts;
}

  void
bx_acpi_ctrl_c::pm_update_sci(void)
{
  Bit16u pmsts = get_pmsts();
  bx_bool sci_level = (((pmsts & BX_ACPI_THIS s.pmen) &
                      (RTC_EN | PWRBTN_EN | GBL_EN | TMROF_EN)) != 0);
  BX_ACPI_THIS set_irq_level(sci_level);
  // schedule a timer interruption if needed
  if ((BX_ACPI_THIS s.pmen & TMROF_EN) && !(pmsts & TMROF_EN)) {
    Bit64u expire_time = muldiv64(BX_ACPI_THIS s.tmr_overflow_time, 1000000, PM_FREQ);
      bx_pc_system.activate_timer(BX_ACPI_THIS s.timer_index, (Bit32u)expire_time, 0);
    } else {
      bx_pc_system.deactivate_timer(BX_ACPI_THIS s.timer_index);
    }
}

  // static IO port read callback handler
  // redirects to non-static class handler to avoid virtual functions

  Bit32u
bx_acpi_ctrl_c::read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
#if !BX_USE_ACPI_SMF
  bx_acpi_ctrl_c *class_ptr = (bx_acpi_ctrl_c *) this_ptr;
  return class_ptr->read(address, io_len);
}

  Bit32u
bx_acpi_ctrl_c::read(Bit32u address, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif // !BX_USE_ACPI_SMF
  Bit32u value = 0xffffffff;

  if ((address >= BX_ACPI_THIS s.pm_base) && (address < (BX_ACPI_THIS s.pm_base+64))) {
    Bit8u reg = address - BX_ACPI_THIS s.pm_base;
    switch (reg) {
      case 0x00:
        value = BX_ACPI_THIS get_pmsts();
        break;
      case 0x02:
        value = BX_ACPI_THIS s.pmen;
        break;
      case 0x04:
        value = BX_ACPI_THIS s.pmcntrl;
        break;
      case 0x08:
        value = BX_ACPI_THIS get_pmtmr();
        break;
      default:
        BX_INFO(("ACPI read from PM register 0x%02x not implemented yet", reg));
    }
    BX_DEBUG(("ACPI read from PM register 0x%02x returns 0x%08x", reg, value));
  } else if ((address >= BX_ACPI_THIS s.sm_base) && (address < (BX_ACPI_THIS s.sm_base+16))) {
    Bit8u reg = address - BX_ACPI_THIS s.sm_base;
    switch (reg) {
      case 0x00:
        value = BX_ACPI_THIS s.smbus.stat;
        break;
      case 0x02:
        BX_ACPI_THIS s.smbus.index = 0;
        value = BX_ACPI_THIS s.smbus.ctl & 0x1f;
        break;
      case 0x03:
        value = BX_ACPI_THIS s.smbus.cmd;
        break;
      case 0x04:
        value = BX_ACPI_THIS s.smbus.addr;
        break;
      case 0x05:
        value = BX_ACPI_THIS s.smbus.data0;
        break;
      case 0x06:
        value = BX_ACPI_THIS s.smbus.data1;
        break;
      case 0x07:
        value = BX_ACPI_THIS s.smbus.data[BX_ACPI_THIS s.smbus.index++];
        if (BX_ACPI_THIS s.smbus.index > 31) {
          BX_ACPI_THIS s.smbus.index = 0;
        }
        break;
      default:
        value = 0;
        BX_INFO(("ACPI read from SMBus register 0x%02x not implemented yet", reg));
    }
    BX_DEBUG(("ACPI read from SMBus register 0x%02x returns 0x%08x", reg, value));
  }
  return value;
}

  // static IO port write callback handler
  // redirects to non-static class handler to avoid virtual functions

  void
bx_acpi_ctrl_c::write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
#if !BX_USE_ACPI_SMF
  bx_acpi_ctrl_c *class_ptr = (bx_acpi_ctrl_c *) this_ptr;
  class_ptr->write(address, value, io_len);
}

  void
bx_acpi_ctrl_c::write(Bit32u address, Bit32u value, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif // !BX_USE_ACPI_SMF

  if ((address >= BX_ACPI_THIS s.pm_base) && (address < (BX_ACPI_THIS s.pm_base+64))) {
    Bit8u reg = address - BX_ACPI_THIS s.pm_base;
    BX_DEBUG(("ACPI write to PM register 0x%02x, value = 0x%04x", reg, value));
    switch (reg) {
      case 0x00:
        {
          Bit16u pmsts = BX_ACPI_THIS get_pmsts();
          if (pmsts & value & TMROF_EN) {
            // if TMRSTS is reset, then compute the new overflow time
            Bit64u d = muldiv64(bx_pc_system.time_usec(), PM_FREQ, 1000000);
            BX_ACPI_THIS s.tmr_overflow_time = (d + BX_CONST64(0x800000)) & ~BX_CONST64(0x7fffff);
          }
          BX_ACPI_THIS s.pmsts &= ~value;
          BX_ACPI_THIS pm_update_sci();
        }
        break;
      case 0x02:
        BX_ACPI_THIS s.pmen = value;
        BX_ACPI_THIS pm_update_sci();
        break;
      case 0x04:
        {
          BX_ACPI_THIS s.pmcntrl = value & ~(SUS_EN);
          if (value & SUS_EN) {
            // change suspend type
            Bit16u sus_typ = (value >> 10) & 7;
            switch (sus_typ) {
              case 0: // soft power off
                bx_user_quit = 1;
                LOG_THIS setonoff(LOGLEV_PANIC, ACT_FATAL);
                BX_PANIC(("ACPI control: soft power off"));
                break;
              case 1:
                BX_INFO(("ACPI control: suspend to ram"));
                BX_ACPI_THIS s.pmsts |= (RSM_STS | PWRBTN_STS);
                DEV_cmos_set_reg(0xF, 0xFE);
                break;
              default:
                break;
            }
          }
        }
        break;
      default:
        BX_INFO(("ACPI write to PM register 0x%02x not implemented yet", reg));
    }
  } else if ((address >= BX_ACPI_THIS s.sm_base) && (address < (BX_ACPI_THIS s.sm_base+16))) {
    Bit8u reg = address - BX_ACPI_THIS s.sm_base;
    BX_DEBUG(("ACPI write to SMBus register 0x%02x, value = 0x%04x", reg, value));
    switch (reg) {
      case 0x00:
        BX_ACPI_THIS s.smbus.stat = 0;
        BX_ACPI_THIS s.smbus.index = 0;
        break;
      case 0x02:
        BX_ACPI_THIS s.smbus.ctl = 0;
        // TODO: execute SMBus command
        break;
      case 0x03:
        BX_ACPI_THIS s.smbus.cmd = 0;
        break;
      case 0x04:
        BX_ACPI_THIS s.smbus.addr = 0;
        break;
      case 0x05:
        BX_ACPI_THIS s.smbus.data0 = 0;
        break;
      case 0x06:
        BX_ACPI_THIS s.smbus.data1 = 0;
        break;
      case 0x07:
        BX_ACPI_THIS s.smbus.data[BX_ACPI_THIS s.smbus.index++] = value;
        if (BX_ACPI_THIS s.smbus.index > 31) {
          BX_ACPI_THIS s.smbus.index = 0;
        }
        break;
      default:
        BX_INFO(("ACPI write to SMBus register 0x%02x not implemented yet", reg));
    }
  }
}

  void
bx_acpi_ctrl_c::timer_handler(void *this_ptr)
{
  bx_acpi_ctrl_c *class_ptr = (bx_acpi_ctrl_c *) this_ptr;
  class_ptr->timer();
}

  void
bx_acpi_ctrl_c::timer(void)
{
  BX_ACPI_THIS pm_update_sci();
}

  // static pci configuration space read callback handler
  // redirects to non-static class handler to avoid virtual functions

  Bit32u
bx_acpi_ctrl_c::pci_read_handler(void *this_ptr, Bit8u address, unsigned io_len)
{
#if !BX_USE_ACPI_SMF
  bx_acpi_ctrl_c *class_ptr = (bx_acpi_ctrl_c *) this_ptr;
  return( class_ptr->pci_read(address, io_len) );
}

  Bit32u
bx_acpi_ctrl_c::pci_read(Bit8u address, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif // !BX_USE_ACPI_SMF
  Bit32u value = 0;

  for (unsigned i=0; i<io_len; i++) {
    value |= (BX_ACPI_THIS s.pci_conf[address+i] << (i*8));
  }

  BX_DEBUG(("read  PCI register 0x%02x value 0x%08x", address, value));

  return value;
}

  // static pci configuration space write callback handler
  // redirects to non-static class handler to avoid virtual functions

  void
bx_acpi_ctrl_c::pci_write_handler(void *this_ptr, Bit8u address, Bit32u value, unsigned io_len)
{
#if !BX_USE_ACPI_SMF
  bx_acpi_ctrl_c *class_ptr = (bx_acpi_ctrl_c *) this_ptr;
  class_ptr->pci_write(address, value, io_len);
}

  void
bx_acpi_ctrl_c::pci_write(Bit8u address, Bit32u value, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif // !BX_USE_ACPI_SMF
  Bit8u value8, oldval;

  if ((address >= 0x10) && (address < 0x34))
    return;

  for (unsigned i=0; i<io_len; i++) {
    value8 = (value >> (i*8)) & 0xFF;
    oldval = BX_ACPI_THIS s.pci_conf[address+i];
    switch (address+i) {
      case 0x04:
        value8 = (value8 & 0xfe) | (value & 0x01);
        goto set_value;
        break;
      case 0x06: // disallowing write to status lo-byte (is that expected?)
        break;
      case 0x3c:
        if (value8 != oldval) {
          BX_INFO(("new irq line = %d", value8));
        }
        goto set_value;
        break;
      case 0x40:
      case 0x41:
      case 0x42:
      case 0x43:
      case 0x90:
      case 0x91:
      case 0x92:
      case 0x93:
        break;
      default:
set_value:
        BX_ACPI_THIS s.pci_conf[address+i] = value8;
    }
  }

  BX_DEBUG(("write PCI register 0x%02x value 0x%08x", address, value));
}

#endif // BX_PCI_SUPPORT
