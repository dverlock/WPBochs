#define BX_PLUGGABLE

#include "bochs.h"
#if BX_NE2K_SUPPORT

#define LOG_THIS bx_devices.pluginNE2kDevice->

class bx_null_pktmover_c : public eth_pktmover_c {
public:
  bx_null_pktmover_c(const char *netif, const char *macaddr,
		     eth_rx_handler_t rxh,
		     void *rxarg);
  void sendpkt(void *buf, unsigned io_len);
};

class bx_null_locator_c : public eth_locator_c {
public:
  bx_null_locator_c(void) : eth_locator_c("null") {}
protected:
  eth_pktmover_c *allocate(const char *netif, const char *macaddr,
			   eth_rx_handler_t rxh,
			   void *rxarg) {
    return (new bx_null_pktmover_c(netif, macaddr, rxh, rxarg));
  }
} bx_null_match;

bx_null_pktmover_c::bx_null_pktmover_c(const char *netif,
				       const char *macaddr,
				       eth_rx_handler_t rxh,
				       void *rxarg)
{
  this->rxh   = rxh;
  this->rxarg = rxarg;
}

void
bx_null_pktmover_c::sendpkt(void *buf, unsigned io_len)
{
  BX_DEBUG (("sendpkt length %u, discarded (null backend)", io_len));
}

#endif /* if BX_NE2K_SUPPORT */
