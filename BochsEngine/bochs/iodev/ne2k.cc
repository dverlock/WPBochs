#define BX_PLUGGABLE

#include "bochs.h"
#if BX_NE2K_SUPPORT

#define BX_NE2K_NEVER_FULL_RING (1)

#define LOG_THIS theNE2kDevice->

bx_ne2k_c *theNE2kDevice = NULL;

  int
libne2k_LTX_plugin_init(plugin_t *plugin, plugintype_t type, int argc, char *argv[])
{
  theNE2kDevice = new bx_ne2k_c ();
  bx_devices.pluginNE2kDevice = theNE2kDevice;
  BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theNE2kDevice, BX_PLUGIN_NE2K);
  return(0);
}

  void
libne2k_LTX_plugin_fini(void)
{
}

bx_ne2k_c::bx_ne2k_c(void)
{
  put("NE2K");
  settype(NE2KLOG);
  s.tx_timer_index = BX_NULL_TIMER_HANDLE;
  link_enabled = 1;
}

void
bx_ne2k_c::set_link_enabled(bx_bool enabled)
{
  link_enabled = enabled;
}

bx_ne2k_c::~bx_ne2k_c(void)
{
}

void
bx_ne2k_c::reset(unsigned type)
{
  BX_DEBUG (("reset"));
  memset( & BX_NE2K_THIS s.CR,  0, sizeof(BX_NE2K_THIS s.CR) );
  memset( & BX_NE2K_THIS s.ISR, 0, sizeof(BX_NE2K_THIS s.ISR));
  memset( & BX_NE2K_THIS s.IMR, 0, sizeof(BX_NE2K_THIS s.IMR));
  memset( & BX_NE2K_THIS s.DCR, 0, sizeof(BX_NE2K_THIS s.DCR));
  memset( & BX_NE2K_THIS s.TCR, 0, sizeof(BX_NE2K_THIS s.TCR));
  memset( & BX_NE2K_THIS s.TSR, 0, sizeof(BX_NE2K_THIS s.TSR));
  memset( & BX_NE2K_THIS s.RCR, 0, sizeof(BX_NE2K_THIS s.RCR));
  memset( & BX_NE2K_THIS s.RSR, 0, sizeof(BX_NE2K_THIS s.RSR));
  BX_NE2K_THIS s.local_dma  = 0;
  BX_NE2K_THIS s.page_start = 0;
  BX_NE2K_THIS s.page_stop  = 0;
  BX_NE2K_THIS s.bound_ptr  = 0;
  BX_NE2K_THIS s.tx_page_start = 0;
  BX_NE2K_THIS s.num_coll   = 0;
  BX_NE2K_THIS s.tx_bytes   = 0;
  BX_NE2K_THIS s.fifo       = 0;
  BX_NE2K_THIS s.remote_dma = 0;
  BX_NE2K_THIS s.remote_start = 0;
  BX_NE2K_THIS s.remote_bytes = 0;
  BX_NE2K_THIS s.tallycnt_0 = 0;
  BX_NE2K_THIS s.tallycnt_1 = 0;
  BX_NE2K_THIS s.tallycnt_2 = 0;

  memset( & BX_NE2K_THIS s.physaddr, 0, sizeof(BX_NE2K_THIS s.physaddr));
  memset( & BX_NE2K_THIS s.mchash, 0, sizeof(BX_NE2K_THIS s.mchash));
  BX_NE2K_THIS s.curr_page = 0;

  BX_NE2K_THIS s.rempkt_ptr   = 0;
  BX_NE2K_THIS s.localpkt_ptr = 0;
  BX_NE2K_THIS s.address_cnt  = 0;

  memset( & BX_NE2K_THIS s.mem, 0, sizeof(BX_NE2K_THIS s.mem));

  BX_NE2K_THIS s.CR.stop      = 1;
    BX_NE2K_THIS s.CR.rdma_cmd  = 4;
  BX_NE2K_THIS s.ISR.reset    = 1;
  BX_NE2K_THIS s.DCR.longaddr = 1;
  DEV_pic_lower_irq(BX_NE2K_THIS s.base_irq);
}

Bit32u
bx_ne2k_c::read_cr(void)
{
  Bit32u val =
         (((BX_NE2K_THIS s.CR.pgsel    & 0x03) << 6) |
	  ((BX_NE2K_THIS s.CR.rdma_cmd & 0x07) << 3) |
	  (BX_NE2K_THIS s.CR.tx_packet << 2) |
	  (BX_NE2K_THIS s.CR.start     << 1) |
	  (BX_NE2K_THIS s.CR.stop));
  BX_DEBUG(("read CR returns 0x%08x", val));
  return val;
}

void
bx_ne2k_c::write_cr(Bit32u value)
{
  BX_DEBUG(("wrote 0x%02x to CR", value));

  if ((value & 0x38) == 0x00) {
    BX_DEBUG(("CR write - invalid rDMA value 0"));
    value |= 0x20;
  }

  if (value & 0x01) {
    BX_NE2K_THIS s.ISR.reset = 1;
    BX_NE2K_THIS s.CR.stop   = 1;
  } else {
    BX_NE2K_THIS s.CR.stop = 0;
  }

  BX_NE2K_THIS s.CR.rdma_cmd = (value & 0x38) >> 3;

  if ((value & 0x02) && !BX_NE2K_THIS s.CR.start) {
    BX_NE2K_THIS s.ISR.reset = 0;
  }

  BX_NE2K_THIS s.CR.start = ((value & 0x02) == 0x02);
  BX_NE2K_THIS s.CR.pgsel = (value & 0xc0) >> 6;

    if (BX_NE2K_THIS s.CR.rdma_cmd == 3) {
	BX_NE2K_THIS s.remote_start = BX_NE2K_THIS s.remote_dma = BX_NE2K_THIS s.bound_ptr * 256;
	BX_NE2K_THIS s.remote_bytes = *((Bit16u*) & BX_NE2K_THIS s.mem[BX_NE2K_THIS s.bound_ptr * 256 + 2 - BX_NE2K_MEMSTART]);
	BX_INFO(("Sending buffer #x%x length %d",
		  BX_NE2K_THIS s.remote_start,
		  BX_NE2K_THIS s.remote_bytes));
    }

    if ((value & 0x04) && BX_NE2K_THIS s.TCR.loop_cntl) {
	if (BX_NE2K_THIS s.TCR.loop_cntl != 1) {
	    BX_INFO(("Loop mode %d not supported.", BX_NE2K_THIS s.TCR.loop_cntl));
	} else {
	    rx_frame (& BX_NE2K_THIS s.mem[BX_NE2K_THIS s.tx_page_start*256 - BX_NE2K_MEMSTART],
		      BX_NE2K_THIS s.tx_bytes);
	}
    } else if (value & 0x04) {
    if (BX_NE2K_THIS s.CR.stop || !BX_NE2K_THIS s.CR.start)
      BX_PANIC(("CR write - tx start, dev in reset"));

    if (BX_NE2K_THIS s.tx_bytes == 0)
      BX_PANIC(("CR write - tx start, tx bytes == 0"));

    if (BX_NE2K_THIS link_enabled)
      BX_NE2K_THIS ethdev->sendpkt(& BX_NE2K_THIS s.mem[BX_NE2K_THIS s.tx_page_start*256 - BX_NE2K_MEMSTART], BX_NE2K_THIS s.tx_bytes);

    if (BX_NE2K_THIS s.tx_timer_active)
      BX_PANIC(("CR write, tx timer still active"));

    bx_pc_system.activate_timer(BX_NE2K_THIS s.tx_timer_index,
				(64 + 96 + 4*8 + BX_NE2K_THIS s.tx_bytes*8)/10,
				0);
  }

  if (BX_NE2K_THIS s.CR.rdma_cmd == 0x01 &&
      BX_NE2K_THIS s.CR.start &&
      BX_NE2K_THIS s.remote_bytes == 0) {
    BX_NE2K_THIS s.ISR.rdma_done = 1;
    if (BX_NE2K_THIS s.IMR.rdma_inte) {
      DEV_pic_raise_irq(BX_NE2K_THIS s.base_irq);
    }
  }
}

Bit32u BX_CPP_AttrRegparmN(2)
bx_ne2k_c::chipmem_read(Bit32u address, unsigned int io_len)
{
  Bit32u retval = 0;

  if ((io_len == 2) && (address & 0x1))
    BX_PANIC(("unaligned chipmem word read"));

  if ((address >=0) && (address <= 31)) {
    retval = BX_NE2K_THIS s.macaddr[address];
    if (io_len == 2) {
      retval |= (BX_NE2K_THIS s.macaddr[address + 1] << 8);
    }
    return (retval);
  }

  if ((address >= BX_NE2K_MEMSTART) && (address < BX_NE2K_MEMEND)) {
    retval = BX_NE2K_THIS s.mem[address - BX_NE2K_MEMSTART];
    if (io_len == 2) {
      retval |= (BX_NE2K_THIS s.mem[address - BX_NE2K_MEMSTART + 1] << 8);
    }
    return (retval);
  }

  BX_DEBUG(("out-of-bounds chipmem read, %04X", address));

  return (0xff);
}

void BX_CPP_AttrRegparmN(3)
bx_ne2k_c::chipmem_write(Bit32u address, Bit32u value, unsigned io_len)
{
  if ((io_len == 2) && (address & 0x1))
    BX_PANIC(("unaligned chipmem word write"));

  if ((address >= BX_NE2K_MEMSTART) && (address < BX_NE2K_MEMEND)) {
    BX_NE2K_THIS s.mem[address - BX_NE2K_MEMSTART] = value & 0xff;
    if (io_len == 2)
      BX_NE2K_THIS s.mem[address - BX_NE2K_MEMSTART + 1] = value >> 8;
  } else
    BX_DEBUG(("out-of-bounds chipmem write, %04X", address));
}

Bit32u BX_CPP_AttrRegparmN(2)
bx_ne2k_c::asic_read(Bit32u offset, unsigned int io_len)
{
  Bit32u retval = 0;

  switch (offset) {
  case 0x0:
    if (io_len > BX_NE2K_THIS s.remote_bytes)
      {
       BX_ERROR(("ne2K: dma read underrun iolen=%d remote_bytes=%d",io_len,BX_NE2K_THIS s.remote_bytes));
      }

    retval = chipmem_read(BX_NE2K_THIS s.remote_dma, io_len);
    BX_NE2K_THIS s.remote_dma += (BX_NE2K_THIS s.DCR.wdsize + 1);
    if (BX_NE2K_THIS s.remote_dma == BX_NE2K_THIS s.page_stop << 8) {
      BX_NE2K_THIS s.remote_dma = BX_NE2K_THIS s.page_start << 8;
    }
    if (BX_NE2K_THIS s.remote_bytes > 1)
      BX_NE2K_THIS s.remote_bytes -= (BX_NE2K_THIS s.DCR.wdsize + 1);
    else
      BX_NE2K_THIS s.remote_bytes = 0;

	if (BX_NE2K_THIS s.remote_bytes == 0) {
	    BX_NE2K_THIS s.ISR.rdma_done = 1;
	    if (BX_NE2K_THIS s.IMR.rdma_inte) {
		DEV_pic_raise_irq(BX_NE2K_THIS s.base_irq);
	    }
	}
    break;

  case 0xf:
    theNE2kDevice->reset(BX_RESET_SOFTWARE);
    break;

  default:
    BX_INFO(("asic read invalid address %04x", (unsigned) offset));
    break;
  }

  return (retval);
}

void
bx_ne2k_c::asic_write(Bit32u offset, Bit32u value, unsigned io_len)
{
  BX_DEBUG(("asic write addr=0x%02x, value=0x%04x", (unsigned) offset, (unsigned) value));
  switch (offset) {
  case 0x0:

    if ((io_len == 2) && (BX_NE2K_THIS s.DCR.wdsize == 0)) {
      BX_PANIC(("dma write length 2 on byte mode operation"));
      break;
    }

    if (BX_NE2K_THIS s.remote_bytes == 0)
      BX_PANIC(("ne2K: dma write, byte count 0"));

    chipmem_write(BX_NE2K_THIS s.remote_dma, value, io_len);
    BX_NE2K_THIS s.remote_dma   += io_len;
    if (BX_NE2K_THIS s.remote_dma == BX_NE2K_THIS s.page_stop << 8) {
      BX_NE2K_THIS s.remote_dma = BX_NE2K_THIS s.page_start << 8;
    }

    BX_NE2K_THIS s.remote_bytes -= io_len;
    if (BX_NE2K_THIS s.remote_bytes > BX_NE2K_MEMSIZ)
      BX_NE2K_THIS s.remote_bytes = 0;

    if (BX_NE2K_THIS s.remote_bytes == 0) {
      BX_NE2K_THIS s.ISR.rdma_done = 1;
      if (BX_NE2K_THIS s.IMR.rdma_inte) {
	  DEV_pic_raise_irq(BX_NE2K_THIS s.base_irq);
      }
    }
    break;

  case 0xf:
    theNE2kDevice->reset(BX_RESET_SOFTWARE);
    break;

  default:
    BX_INFO(("asic write invalid address %04x, ignoring", (unsigned) offset));
    break ;
  }
}

Bit32u
bx_ne2k_c::page0_read(Bit32u offset, unsigned int io_len)
{
  BX_DEBUG(("page 0 read from port %04x, len=%u", (unsigned) offset,
	   (unsigned) io_len));
  if (io_len > 1) {
    BX_ERROR(("bad length! page 0 read from port %04x, len=%u", (unsigned) offset,
             (unsigned) io_len));
	return 0;
  }

  switch (offset) {
  case 0x0:
    return (read_cr());
    break;

  case 0x1:
    return (BX_NE2K_THIS s.local_dma & 0xff);
    break;

  case 0x2:
    return (BX_NE2K_THIS s.local_dma >> 8);
    break;

  case 0x3:
    return (BX_NE2K_THIS s.bound_ptr);
    break;

  case 0x4:
    return ((BX_NE2K_THIS s.TSR.ow_coll    << 7) |
	    (BX_NE2K_THIS s.TSR.cd_hbeat   << 6) |
	    (BX_NE2K_THIS s.TSR.fifo_ur    << 5) |
	    (BX_NE2K_THIS s.TSR.no_carrier << 4) |
	    (BX_NE2K_THIS s.TSR.aborted    << 3) |
	    (BX_NE2K_THIS s.TSR.collided   << 2) |
	    (BX_NE2K_THIS s.TSR.tx_ok));
    break;

  case 0x5:
    return (BX_NE2K_THIS s.num_coll);
    break;

  case 0x6:
    BX_ERROR(("reading FIFO not supported yet"));
    return (BX_NE2K_THIS s.fifo);
    break;

  case 0x7:
    return ((BX_NE2K_THIS s.ISR.reset     << 7) |
	    (BX_NE2K_THIS s.ISR.rdma_done << 6) |
	    (BX_NE2K_THIS s.ISR.cnt_oflow << 5) |
	    (BX_NE2K_THIS s.ISR.overwrite << 4) |
	    (BX_NE2K_THIS s.ISR.tx_err    << 3) |
	    (BX_NE2K_THIS s.ISR.rx_err    << 2) |
	    (BX_NE2K_THIS s.ISR.pkt_tx    << 1) |
	    (BX_NE2K_THIS s.ISR.pkt_rx));
    break;

  case 0x8:
    return (BX_NE2K_THIS s.remote_dma & 0xff);
    break;

  case 0x9:
    return (BX_NE2K_THIS s.remote_dma >> 8);
    break;

  case 0xa:
    BX_INFO(("reserved read - page 0, 0xa"));
    return (0xff);
    break;

  case 0xb:
    BX_INFO(("reserved read - page 0, 0xb"));
    return (0xff);
    break;

  case 0xc:
    return ((BX_NE2K_THIS s.RSR.deferred    << 7) |
	    (BX_NE2K_THIS s.RSR.rx_disabled << 6) |
	    (BX_NE2K_THIS s.RSR.rx_mbit     << 5) |
	    (BX_NE2K_THIS s.RSR.rx_missed   << 4) |
	    (BX_NE2K_THIS s.RSR.fifo_or     << 3) |
	    (BX_NE2K_THIS s.RSR.bad_falign  << 2) |
	    (BX_NE2K_THIS s.RSR.bad_crc     << 1) |
	    (BX_NE2K_THIS s.RSR.rx_ok));
    break;

  case 0xd:
    return (BX_NE2K_THIS s.tallycnt_0);
    break;

  case 0xe:
    return (BX_NE2K_THIS s.tallycnt_1);
    break;

  case 0xf:
    return (BX_NE2K_THIS s.tallycnt_2);
    break;

  default:
    BX_PANIC(("page 0 offset %04x out of range", (unsigned) offset));
  }

  return(0);
}

void
bx_ne2k_c::page0_write(Bit32u offset, Bit32u value, unsigned io_len)
{
  BX_DEBUG(("page 0 write to port %04x, len=%u", (unsigned) offset,
	   (unsigned) io_len));
  if (((io_len > 1) && (offset != 5)) || (io_len > 2)) {
    BX_ERROR(("bad length! page 0 write to port %04x, len=%u", offset, io_len));
    return;
  }

  switch (offset) {
  case 0x0:
    write_cr(value);
    break;

  case 0x1:
    BX_NE2K_THIS s.page_start = value;
    break;

  case 0x2:
    BX_NE2K_THIS s.page_stop = value;
    break;

  case 0x3:
    BX_NE2K_THIS s.bound_ptr = value;
    break;

  case 0x4:
    BX_NE2K_THIS s.tx_page_start = value;
    break;

  case 0x5:
    BX_NE2K_THIS s.tx_bytes &= 0xff00;
    BX_NE2K_THIS s.tx_bytes |= (value & 0xff);
    break;

  case 0x6:
    BX_NE2K_THIS s.tx_bytes &= 0x00ff;
    BX_NE2K_THIS s.tx_bytes |= ((value & 0xff) << 8);
    break;

  case 0x7:
    value &= 0x7f;
    BX_NE2K_THIS s.ISR.pkt_rx    &= ~((bx_bool)((value & 0x01) == 0x01));
    BX_NE2K_THIS s.ISR.pkt_tx    &= ~((bx_bool)((value & 0x02) == 0x02));
    BX_NE2K_THIS s.ISR.rx_err    &= ~((bx_bool)((value & 0x04) == 0x04));
    BX_NE2K_THIS s.ISR.tx_err    &= ~((bx_bool)((value & 0x08) == 0x08));
    BX_NE2K_THIS s.ISR.overwrite &= ~((bx_bool)((value & 0x10) == 0x10));
    BX_NE2K_THIS s.ISR.cnt_oflow &= ~((bx_bool)((value & 0x20) == 0x20));
    BX_NE2K_THIS s.ISR.rdma_done &= ~((bx_bool)((value & 0x40) == 0x40));
    value = ((BX_NE2K_THIS s.ISR.rdma_done << 6) |
             (BX_NE2K_THIS s.ISR.cnt_oflow << 5) |
             (BX_NE2K_THIS s.ISR.overwrite << 4) |
             (BX_NE2K_THIS s.ISR.tx_err    << 3) |
             (BX_NE2K_THIS s.ISR.rx_err    << 2) |
             (BX_NE2K_THIS s.ISR.pkt_tx    << 1) |
             (BX_NE2K_THIS s.ISR.pkt_rx));
    value &= ((BX_NE2K_THIS s.IMR.rdma_inte << 6) |
              (BX_NE2K_THIS s.IMR.cofl_inte << 5) |
              (BX_NE2K_THIS s.IMR.overw_inte << 4) |
              (BX_NE2K_THIS s.IMR.txerr_inte << 3) |
              (BX_NE2K_THIS s.IMR.rxerr_inte << 2) |
              (BX_NE2K_THIS s.IMR.tx_inte << 1) |
              (BX_NE2K_THIS s.IMR.rx_inte));
    if (value == 0)
      DEV_pic_lower_irq(BX_NE2K_THIS s.base_irq);
    break;

  case 0x8:
    BX_NE2K_THIS s.remote_start &= 0xff00;
    BX_NE2K_THIS s.remote_start |= (value & 0xff);
    BX_NE2K_THIS s.remote_dma = BX_NE2K_THIS s.remote_start;
    break;

  case 0x9:
    BX_NE2K_THIS s.remote_start &= 0x00ff;
    BX_NE2K_THIS s.remote_start |= ((value & 0xff) << 8);
    BX_NE2K_THIS s.remote_dma = BX_NE2K_THIS s.remote_start;
    break;

  case 0xa:
    BX_NE2K_THIS s.remote_bytes &= 0xff00;
    BX_NE2K_THIS s.remote_bytes |= (value & 0xff);
    break;

  case 0xb:
    BX_NE2K_THIS s.remote_bytes &= 0x00ff;
    BX_NE2K_THIS s.remote_bytes |= ((value & 0xff) << 8);
    break;

  case 0xc:
    if (value & 0xc0)
      BX_INFO(("RCR write, reserved bits set"));

    BX_NE2K_THIS s.RCR.errors_ok = ((value & 0x01) == 0x01);
    BX_NE2K_THIS s.RCR.runts_ok  = ((value & 0x02) == 0x02);
    BX_NE2K_THIS s.RCR.broadcast = ((value & 0x04) == 0x04);
    BX_NE2K_THIS s.RCR.multicast = ((value & 0x08) == 0x08);
    BX_NE2K_THIS s.RCR.promisc   = ((value & 0x10) == 0x10);
    BX_NE2K_THIS s.RCR.monitor   = ((value & 0x20) == 0x20);

    if (value & 0x20)
      BX_INFO(("RCR write, monitor bit set!"));
    break;

  case 0xd:
    if (value & 0xe0)
      BX_ERROR(("TCR write, reserved bits set"));

    if (value & 0x06) {
      BX_NE2K_THIS s.TCR.loop_cntl = (value & 0x6) >> 1;
      BX_INFO(("TCR write, loop mode %d not supported", BX_NE2K_THIS s.TCR.loop_cntl));
    } else {
      BX_NE2K_THIS s.TCR.loop_cntl = 0;
    }

    if (value & 0x01)
      BX_PANIC(("TCR write, inhibit-CRC not supported"));

    if (value & 0x08)
      BX_PANIC(("TCR write, auto transmit disable not supported"));

    BX_NE2K_THIS s.TCR.coll_prio = ((value & 0x08) == 0x08);
    break;

  case 0xe:
    if (!(value & 0x08)) {
      BX_ERROR(("DCR write, loopback mode selected"));
    }
    if (value & 0x04)
      BX_INFO(("DCR write - LAS set ???"));
    if (value & 0x10)
      BX_INFO(("DCR write - AR set ???"));

    BX_NE2K_THIS s.DCR.wdsize   = ((value & 0x01) == 0x01);
    BX_NE2K_THIS s.DCR.endian   = ((value & 0x02) == 0x02);
    BX_NE2K_THIS s.DCR.longaddr = ((value & 0x04) == 0x04);
    BX_NE2K_THIS s.DCR.loop     = ((value & 0x08) == 0x08);
    BX_NE2K_THIS s.DCR.auto_rx  = ((value & 0x10) == 0x10);
    BX_NE2K_THIS s.DCR.fifo_size = (value & 0x50) >> 5;
    break;

  case 0xf:
    if (value & 0x80)
      BX_PANIC(("IMR write, reserved bit set"));

    BX_NE2K_THIS s.IMR.rx_inte    = ((value & 0x01) == 0x01);
    BX_NE2K_THIS s.IMR.tx_inte    = ((value & 0x02) == 0x02);
    BX_NE2K_THIS s.IMR.rxerr_inte = ((value & 0x04) == 0x04);
    BX_NE2K_THIS s.IMR.txerr_inte = ((value & 0x08) == 0x08);
    BX_NE2K_THIS s.IMR.overw_inte = ((value & 0x10) == 0x10);
    BX_NE2K_THIS s.IMR.cofl_inte  = ((value & 0x20) == 0x20);
    BX_NE2K_THIS s.IMR.rdma_inte  = ((value & 0x40) == 0x40);
    break;

  default:
    BX_PANIC(("page 0 write, bad offset %0x", offset));
  }
}

Bit32u
bx_ne2k_c::page1_read(Bit32u offset, unsigned int io_len)
{
  BX_DEBUG(("page 1 read from port %04x, len=%u", (unsigned) offset,
	   (unsigned) io_len));
  if (io_len > 1)
    BX_PANIC(("bad length! page 1 read from port %04x, len=%u", (unsigned) offset,
             (unsigned) io_len));

  switch (offset) {
  case 0x0:
    return (read_cr());
    break;

  case 0x1:
  case 0x2:
  case 0x3:
  case 0x4:
  case 0x5:
  case 0x6:
    return (BX_NE2K_THIS s.physaddr[offset - 1]);
    break;

  case 0x7:
      BX_DEBUG(("returning current page: %02x", (BX_NE2K_THIS s.curr_page)));
    return (BX_NE2K_THIS s.curr_page);

  case 0x8:
  case 0x9:
  case 0xa:
  case 0xb:
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    return (BX_NE2K_THIS s.mchash[offset - 8]);
    break;

  default:
    BX_PANIC(("page 1 r offset %04x out of range", (unsigned) offset));
  }

  return (0);
}

void
bx_ne2k_c::page1_write(Bit32u offset, Bit32u value, unsigned io_len)
{
  BX_DEBUG(("page 1 w offset %04x", (unsigned) offset));
  switch (offset) {
  case 0x0:
    write_cr(value);
    break;

  case 0x1:
  case 0x2:
  case 0x3:
  case 0x4:
  case 0x5:
  case 0x6:
    BX_NE2K_THIS s.physaddr[offset - 1] = value;
    break;

  case 0x7:
    BX_NE2K_THIS s.curr_page = value;
    break;

  case 0x8:
  case 0x9:
  case 0xa:
  case 0xb:
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    BX_NE2K_THIS s.mchash[offset - 8] = value;
    break;

  default:
    BX_PANIC(("page 1 w offset %04x out of range", (unsigned) offset));
  }
}

Bit32u
bx_ne2k_c::page2_read(Bit32u offset, unsigned int io_len)
{
  BX_DEBUG(("page 2 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len));

  if (io_len > 1)
    BX_PANIC(("bad length!  page 2 read from port %04x, len=%u", (unsigned) offset, (unsigned) io_len));

  switch (offset) {
  case 0x0:
    return (read_cr());
    break;

  case 0x1:
    return (BX_NE2K_THIS s.page_start);
    break;

  case 0x2:
    return (BX_NE2K_THIS s.page_stop);
    break;

  case 0x3:
    return (BX_NE2K_THIS s.rempkt_ptr);
    break;

  case 0x4:
    return (BX_NE2K_THIS s.tx_page_start);
    break;

  case 0x5:
    return (BX_NE2K_THIS s.localpkt_ptr);
    break;

  case 0x6:
    return (BX_NE2K_THIS s.address_cnt >> 8);
    break;

  case 0x7:
    return (BX_NE2K_THIS s.address_cnt & 0xff);
    break;

  case 0x8:
  case 0x9:
  case 0xa:
  case 0xb:
    BX_ERROR(("reserved read - page 2, 0x%02x", (unsigned) offset));
    return (0xff);
    break;

  case 0xc:
    return ((BX_NE2K_THIS s.RCR.monitor   << 5) |
	    (BX_NE2K_THIS s.RCR.promisc   << 4) |
	    (BX_NE2K_THIS s.RCR.multicast << 3) |
	    (BX_NE2K_THIS s.RCR.broadcast << 2) |
	    (BX_NE2K_THIS s.RCR.runts_ok  << 1) |
	    (BX_NE2K_THIS s.RCR.errors_ok));
    break;

  case 0xd:
    return ((BX_NE2K_THIS s.TCR.coll_prio   << 4) |
	    (BX_NE2K_THIS s.TCR.ext_stoptx  << 3) |
	    ((BX_NE2K_THIS s.TCR.loop_cntl & 0x3) << 1) |
	    (BX_NE2K_THIS s.TCR.crc_disable));
    break;

  case 0xe:
    return (((BX_NE2K_THIS s.DCR.fifo_size & 0x3) << 5) |
	    (BX_NE2K_THIS s.DCR.auto_rx  << 4) |
	    (BX_NE2K_THIS s.DCR.loop     << 3) |
	    (BX_NE2K_THIS s.DCR.longaddr << 2) |
	    (BX_NE2K_THIS s.DCR.endian   << 1) |
	    (BX_NE2K_THIS s.DCR.wdsize));
    break;

  case 0xf:
    return ((BX_NE2K_THIS s.IMR.rdma_inte  << 6) |
	    (BX_NE2K_THIS s.IMR.cofl_inte  << 5) |
	    (BX_NE2K_THIS s.IMR.overw_inte << 4) |
	    (BX_NE2K_THIS s.IMR.txerr_inte << 3) |
	    (BX_NE2K_THIS s.IMR.rxerr_inte << 2) |
	    (BX_NE2K_THIS s.IMR.tx_inte    << 1) |
	    (BX_NE2K_THIS s.IMR.rx_inte));
    break;

  default:
    BX_PANIC(("page 2 offset %04x out of range", (unsigned) offset));
  }

  return (0);
};

void
bx_ne2k_c::page2_write(Bit32u offset, Bit32u value, unsigned io_len)
{
  if (offset != 0)
    BX_ERROR(("page 2 write ?"));

  switch (offset) {
  case 0x0:
    write_cr(value);
    break;

  case 0x1:
    BX_NE2K_THIS s.local_dma &= 0xff00;
    BX_NE2K_THIS s.local_dma |= (value & 0xff);
    break;

  case 0x2:
    BX_NE2K_THIS s.local_dma &= 0x00ff;
    BX_NE2K_THIS s.local_dma |= ((value & 0xff) << 8);
    break;

  case 0x3:
    BX_NE2K_THIS s.rempkt_ptr = value;
    break;

  case 0x4:
    BX_PANIC(("page 2 write to reserved offset 4"));
    break;

  case 0x5:
    BX_NE2K_THIS s.localpkt_ptr = value;
    break;

  case 0x6:
    BX_NE2K_THIS s.address_cnt &= 0x00ff;
    BX_NE2K_THIS s.address_cnt |= ((value & 0xff) << 8);
    break;

  case 0x7:
    BX_NE2K_THIS s.address_cnt &= 0xff00;
    BX_NE2K_THIS s.address_cnt |= (value & 0xff);
    break;

  case 0x8:
  case 0x9:
  case 0xa:
  case 0xb:
  case 0xc:
  case 0xd:
  case 0xe:
  case 0xf:
    BX_PANIC(("page 2 write to reserved offset %0x", offset));
    break;

  default:
    BX_PANIC(("page 2 write, illegal offset %0x", offset));
    break;
  }
}

Bit32u
bx_ne2k_c::page3_read(Bit32u offset, unsigned int io_len)
{
  BX_PANIC(("page 3 read attempted"));
  return (0);
}

void
bx_ne2k_c::page3_write(Bit32u offset, Bit32u value, unsigned io_len)
{
  BX_PANIC(("page 3 write attempted"));
}

void
bx_ne2k_c::tx_timer_handler(void *this_ptr)
{
  bx_ne2k_c *class_ptr = (bx_ne2k_c *) this_ptr;

  class_ptr->tx_timer();
}

void
bx_ne2k_c::tx_timer(void)
{
  BX_DEBUG(("tx_timer"));
  BX_NE2K_THIS s.TSR.tx_ok = 1;
  if (BX_NE2K_THIS s.IMR.tx_inte && !BX_NE2K_THIS s.ISR.pkt_tx) {
    BX_NE2K_THIS s.ISR.pkt_tx = 1;
    DEV_pic_raise_irq(BX_NE2K_THIS s.base_irq);
  }
  BX_NE2K_THIS s.tx_timer_active = 0;
}

Bit32u
bx_ne2k_c::read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
#if !BX_USE_NE2K_SMF
  bx_ne2k_c *class_ptr = (bx_ne2k_c *) this_ptr;

  return( class_ptr->read(address, io_len) );
}

Bit32u
bx_ne2k_c::read(Bit32u address, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif
  BX_DEBUG(("read addr %x, len %d", address, io_len));
  Bit32u retval = 0;
  int offset = address - BX_NE2K_THIS s.base_address;

  if (offset >= 0x10) {
    retval = asic_read(offset - 0x10, io_len);
  } else {
    switch (BX_NE2K_THIS s.CR.pgsel) {
    case 0x00:
      retval = page0_read(offset, io_len);
      break;

    case 0x01:
      retval = page1_read(offset, io_len);
      break;

    case 0x02:
      retval = page2_read(offset, io_len);
      break;

    case 0x03:
      retval = page3_read(offset, io_len);
      break;

    default:
      BX_PANIC(("ne2K: unknown value of pgsel in read - %d",
	       BX_NE2K_THIS s.CR.pgsel));
    }
  }

  return (retval);
}

void
bx_ne2k_c::write_handler(void *this_ptr, Bit32u address, Bit32u value,
			 unsigned io_len)
{
#if !BX_USE_NE2K_SMF
  bx_ne2k_c *class_ptr = (bx_ne2k_c *) this_ptr;

  class_ptr->write(address, value, io_len);
}

void
bx_ne2k_c::write(Bit32u address, Bit32u value, unsigned io_len)
{
#else
  UNUSED(this_ptr);
#endif
  BX_DEBUG(("write with length %d", io_len));
  int offset = address - BX_NE2K_THIS s.base_address;

  if (offset >= 0x10) {
    asic_write(offset - 0x10, value, io_len);
  } else {
    switch (BX_NE2K_THIS s.CR.pgsel) {
    case 0x00:
      page0_write(offset, value, io_len);
      break;

    case 0x01:
      page1_write(offset, value, io_len);
      break;

    case 0x02:
      page2_write(offset, value, io_len);
      break;

    case 0x03:
      page3_write(offset, value, io_len);
      break;

    default:
      BX_PANIC(("ne2K: unknown value of pgsel in write - %d",
	       BX_NE2K_THIS s.CR.pgsel));
    }
  }
}

unsigned
bx_ne2k_c::mcast_index(const void *dst)
{
#define POLYNOMIAL 0x04c11db6
  unsigned long crc = 0xffffffffL;
  int carry, i, j;
  unsigned char b;
  unsigned char *ep = (unsigned char *) dst;

  for (i = 6; --i >= 0;) {
    b = *ep++;
    for (j = 8; --j >= 0;) {
      carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
      crc <<= 1;
      b >>= 1;
      if (carry)
	crc = ((crc ^ POLYNOMIAL) | carry);
    }
  }
  return (crc >> 26);
#undef POLYNOMIAL
}

void
bx_ne2k_c::rx_handler(void *arg, const void *buf, unsigned len)
{
  bx_ne2k_c *class_ptr = (bx_ne2k_c *) arg;

  class_ptr->rx_frame(buf, len);
}

void
bx_ne2k_c::rx_frame(const void *buf, unsigned io_len)
{
  unsigned pages;
  unsigned avail;
  unsigned idx;
  int wrapped;
  int nextpage;
  unsigned char pkthdr[4];
  unsigned char *pktbuf = (unsigned char *) buf;
  unsigned char *startptr;
  static unsigned char bcast_addr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

  BX_DEBUG(("rx_frame with length %d", io_len));

  if (!BX_NE2K_THIS link_enabled) {
    return;
  }

  if ((BX_NE2K_THIS s.CR.stop != 0) ||
      (BX_NE2K_THIS s.page_start == 0) ||
      ((BX_NE2K_THIS s.DCR.loop == 0) &&
       (BX_NE2K_THIS s.TCR.loop_cntl != 0))) {

    return;
  }

  pages = (io_len + 4 + 4 + 255)/256;

  if (BX_NE2K_THIS s.curr_page < BX_NE2K_THIS s.bound_ptr) {
    avail = BX_NE2K_THIS s.bound_ptr - BX_NE2K_THIS s.curr_page;
  } else {
    avail = (BX_NE2K_THIS s.page_stop - BX_NE2K_THIS s.page_start) -
      (BX_NE2K_THIS s.curr_page - BX_NE2K_THIS s.bound_ptr);
    wrapped = 1;
  }

  if ((avail < pages)
#if BX_NE2K_NEVER_FULL_RING
      || (avail == pages)
#endif
      ) {
    return;
  }

  if ((io_len < 60) && !BX_NE2K_THIS s.RCR.runts_ok) {
    BX_DEBUG(("rejected small packet, length %d", io_len));
    return;
  }

  if (! BX_NE2K_THIS s.RCR.promisc) {
    if (!memcmp(buf, bcast_addr, 6)) {
      if (!BX_NE2K_THIS s.RCR.broadcast) {
	return;
      }
    } else if (pktbuf[0] & 0x01) {
	if (! BX_NE2K_THIS s.RCR.multicast) {
	    return;
	}
      idx = mcast_index(buf);
      if (!(BX_NE2K_THIS s.mchash[idx >> 3] & (1 << (idx & 0x7)))) {
	return;
      }
    } else if (0 != memcmp(buf, BX_NE2K_THIS s.physaddr, 6)) {
      return;
    }
  } else {
      BX_DEBUG(("rx_frame promiscuous receive"));
  }

  nextpage = BX_NE2K_THIS s.curr_page + pages;
  if (nextpage >= BX_NE2K_THIS s.page_stop) {
    nextpage -= BX_NE2K_THIS s.page_stop - BX_NE2K_THIS s.page_start;
  }

  pkthdr[0] = 0;
  pkthdr[0] = 1;
  if (pktbuf[0] & 0x01) {
    pkthdr[0] |= 0x20;
  }
  pkthdr[1] = nextpage;
  pkthdr[2] = (io_len + 4) & 0xff;
  pkthdr[3] = (io_len + 4) >> 8;

  startptr = & BX_NE2K_THIS s.mem[BX_NE2K_THIS s.curr_page * 256 -
			       BX_NE2K_MEMSTART];
  if ((nextpage > BX_NE2K_THIS s.curr_page) ||
      ((BX_NE2K_THIS s.curr_page + pages) == BX_NE2K_THIS s.page_stop)) {
    memcpy(startptr, pkthdr, 4);
    memcpy(startptr + 4, buf, io_len);
    BX_NE2K_THIS s.curr_page = nextpage;
  } else {
    int endbytes = (BX_NE2K_THIS s.page_stop - BX_NE2K_THIS s.curr_page)
      * 256;
    memcpy(startptr, pkthdr, 4);
    memcpy(startptr + 4, buf, endbytes - 4);
    startptr = & BX_NE2K_THIS s.mem[BX_NE2K_THIS s.page_start * 256 -
				 BX_NE2K_MEMSTART];
    memcpy(startptr, (void *)(pktbuf + endbytes - 4),
	   io_len - endbytes + 8);
    BX_NE2K_THIS s.curr_page = nextpage;
  }

  BX_NE2K_THIS s.RSR.rx_ok = 1;
  if (pktbuf[0] & 0x80) {
    BX_NE2K_THIS s.RSR.rx_mbit = 1;
  }

  BX_NE2K_THIS s.ISR.pkt_rx = 1;

  if (BX_NE2K_THIS s.IMR.rx_inte) {
    DEV_pic_raise_irq(BX_NE2K_THIS s.base_irq);
  }

}

void
bx_ne2k_c::init(void)
{
  BX_DEBUG(("Init"));

  BX_NE2K_THIS s.base_address = bx_options.ne2k.Oioaddr->get ();
  BX_NE2K_THIS s.base_irq     = bx_options.ne2k.Oirq->get ();
  memcpy(BX_NE2K_THIS s.physaddr, bx_options.ne2k.Omacaddr->getptr (), 6);

  if (BX_NE2K_THIS s.tx_timer_index == BX_NULL_TIMER_HANDLE) {
    BX_NE2K_THIS s.tx_timer_index =
      bx_pc_system.register_timer(this, tx_timer_handler, 0,
                                  0,0, "ne2k");
  }
  DEV_register_irq(BX_NE2K_THIS s.base_irq, "NE2000 ethernet NIC");

  for (unsigned addr = BX_NE2K_THIS s.base_address;
       addr <= BX_NE2K_THIS s.base_address + 0x20;
       addr++) {
    DEV_register_ioread_handler(this, read_handler, addr, "ne2000 NIC", 3);
    DEV_register_iowrite_handler(this, write_handler, addr, "ne2000 NIC", 3);
  }
  BX_INFO(("port 0x%x/32 irq %d mac %02x:%02x:%02x:%02x:%02x:%02x",
           BX_NE2K_THIS s.base_address,
           BX_NE2K_THIS s.base_irq,
           BX_NE2K_THIS s.physaddr[0],
           BX_NE2K_THIS s.physaddr[1],
           BX_NE2K_THIS s.physaddr[2],
           BX_NE2K_THIS s.physaddr[3],
           BX_NE2K_THIS s.physaddr[4],
           BX_NE2K_THIS s.physaddr[5]));

  BX_NE2K_THIS s.macaddr[0]  = BX_NE2K_THIS s.physaddr[0];
  BX_NE2K_THIS s.macaddr[1]  = BX_NE2K_THIS s.physaddr[0];
  BX_NE2K_THIS s.macaddr[2]  = BX_NE2K_THIS s.physaddr[1];
  BX_NE2K_THIS s.macaddr[3]  = BX_NE2K_THIS s.physaddr[1];
  BX_NE2K_THIS s.macaddr[4]  = BX_NE2K_THIS s.physaddr[2];
  BX_NE2K_THIS s.macaddr[5]  = BX_NE2K_THIS s.physaddr[2];
  BX_NE2K_THIS s.macaddr[6]  = BX_NE2K_THIS s.physaddr[3];
  BX_NE2K_THIS s.macaddr[7]  = BX_NE2K_THIS s.physaddr[3];
  BX_NE2K_THIS s.macaddr[8]  = BX_NE2K_THIS s.physaddr[4];
  BX_NE2K_THIS s.macaddr[9]  = BX_NE2K_THIS s.physaddr[4];
  BX_NE2K_THIS s.macaddr[10] = BX_NE2K_THIS s.physaddr[5];
  BX_NE2K_THIS s.macaddr[11] = BX_NE2K_THIS s.physaddr[5];

  for (int i = 12; i < 32; i++)
    BX_NE2K_THIS s.macaddr[i] = 0x57;

  char *ethmod = bx_options.ne2k.Oethmod->getptr();
  BX_NE2K_THIS ethdev = eth_locator_c::create(ethmod,
                                              bx_options.ne2k.Oethdev->getptr (),
                                              (const char *) bx_options.ne2k.Omacaddr->getptr (),
                                              rx_handler,
                                              this);

  if (BX_NE2K_THIS ethdev == NULL) {
    BX_INFO(("could not find eth module %s - using null instead", ethmod));

    BX_NE2K_THIS ethdev = eth_locator_c::create("null", NULL,
                                                (const char *) bx_options.ne2k.Omacaddr->getptr (),
                                                rx_handler,
                                                this);
    if (BX_NE2K_THIS ethdev == NULL)
      BX_PANIC(("could not locate null module"));
  }

  theNE2kDevice->reset(BX_RESET_HARDWARE);
}

void
bx_ne2k_c::print_info (FILE *fp, int page, int reg, int brief)
{
}

#endif /* if BX_NE2K_SUPPORT */
