#define BX_PLUGGABLE

#include "bochs.h"
#if BX_NE2K_SUPPORT

#define LOG_THIS /* not needed */

eth_locator_c *eth_locator_c::all;

eth_locator_c::eth_locator_c(const char *type)
{
  next = all;
  all  = this;
  this->type = type;
}

extern class bx_null_locator_c bx_null_match;
extern class bx_nat_locator_c bx_nat_match;

eth_pktmover_c *
eth_locator_c::create(const char *type, const char *netif,
		      const char *macaddr,
		      eth_rx_handler_t rxh, void *rxarg)
{
  eth_locator_c *ptr = 0;

  if (!strcmp(type, "null"))
    ptr = (eth_locator_c *) &bx_null_match;
  else if (!strcmp(type, "nat"))
    ptr = (eth_locator_c *) &bx_nat_match;

  if (ptr)
    return (ptr->allocate(netif, macaddr, rxh, rxarg));

  return (NULL);
}

#endif /* if BX_NE2K_SUPPORT */
