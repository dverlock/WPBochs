#define BX_PLUGGABLE

#include "bochs.h"
#if BX_NE2K_SUPPORT

#include "wpb_net_io.h"
#include <map>
#include <mutex>
#include <vector>
#include <cstdio>

#define LOG_THIS bx_devices.pluginNE2kDevice->

static Bit16u nat_rd16(const Bit8u *p) { return (Bit16u)((p[0] << 8) | p[1]); }
static Bit32u nat_rd32(const Bit8u *p) { return ((Bit32u)p[0] << 24) | ((Bit32u)p[1] << 16) | ((Bit32u)p[2] << 8) | p[3]; }
static void nat_wr16(Bit8u *p, Bit16u v) { p[0] = (Bit8u)(v >> 8); p[1] = (Bit8u)v; }
static void nat_wr32(Bit8u *p, Bit32u v) { p[0] = (Bit8u)(v >> 24); p[1] = (Bit8u)(v >> 16); p[2] = (Bit8u)(v >> 8); p[3] = (Bit8u)v; }

static Bit16u nat_checksum(const Bit8u *data, unsigned len)
{
  Bit32u sum = 0;
  while (len > 1) {
    sum += ((Bit32u)data[0] << 8) | data[1];
    data += 2;
    len -= 2;
  }
  if (len) sum += ((Bit32u)data[0] << 8);
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return (Bit16u)~sum;
}

static Bit16u nat_tcp_checksum(Bit32u srcip, Bit32u dstip, const Bit8u *seg, unsigned len)
{
  Bit8u pseudo[12];
  nat_wr32(pseudo, srcip);
  nat_wr32(pseudo + 4, dstip);
  pseudo[8] = 0;
  pseudo[9] = 6;
  nat_wr16(pseudo + 10, (Bit16u)len);

  Bit32u sum = 0;
  const Bit8u *p = pseudo;
  unsigned n = 12;
  while (n > 1) { sum += ((Bit32u)p[0] << 8) | p[1]; p += 2; n -= 2; }

  p = seg; n = len;
  while (n > 1) { sum += ((Bit32u)p[0] << 8) | p[1]; p += 2; n -= 2; }
  if (n) sum += ((Bit32u)p[0] << 8);

  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return (Bit16u)~sum;
}

static void nat_ip_to_str(Bit32u ip, char *out)
{
  sprintf(out, "%u.%u.%u.%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
}

struct UdpFlow {
  Bit16u guest_port;
  Bit32u dst_ip;
  Bit16u dst_port;
  Bit32u real_ip;
};

#define NAT_DNS_SERVER_IP 0x08080808u

struct PendingSeg {
  Bit32u seq_end;
  std::vector<Bit8u> raw;
  Bit64u sent_tick;
  int retries;
};

struct TcpFlow {
  Bit16u guest_port;
  Bit32u dst_ip;
  Bit16u dst_port;
  Bit32u guest_next_seq;
  Bit32u our_next_seq;
  int state;
  std::vector<PendingSeg> unacked;
};

#define TCP_SYN_RCVD 0
#define TCP_ESTABLISHED 1
#define TCP_CLOSING 2

#define TCP_RETRANSMIT_TICKS 250
#define TCP_MAX_RETRIES 6

class bx_nat_pktmover_c : public eth_pktmover_c {
public:
  bx_nat_pktmover_c(const char *netif, const char *macaddr,
		    eth_rx_handler_t rxh, void *rxarg);
  void sendpkt(void *buf, unsigned io_len);

  void OnUdpData(int handle, const Bit8u *data, int length);
  void OnTcpConnect(int handle, int success);
  void OnTcpData(int handle, const Bit8u *data, int length);
  void OnTcpClosed(int handle);
  void DrainQueue(void);

private:
  Bit8u guest_mac[6];
  Bit8u gw_mac[6];
  Bit32u guest_ip, gw_ip, dns_ip, netmask;
  Bit32u next_isn;
  Bit64u now_tick;
  int tx_timer_index;

  std::mutex flow_mutex;
  std::map<Bit64u, int> udp_by_key;
  std::map<int, UdpFlow> udp_by_handle;
  std::map<Bit64u, int> tcp_by_key;
  std::map<int, TcpFlow> tcp_by_handle;

  std::mutex rx_mutex;
  std::vector<std::vector<Bit8u>> rx_queue;

  void handle_arp(const Bit8u *buf, unsigned len);
  void handle_ip(const Bit8u *buf, unsigned len);
  void handle_udp(const Bit8u *eth, const Bit8u *ip, const Bit8u *udp, unsigned udplen, Bit32u srcip, Bit32u dstip);
  void handle_tcp(const Bit8u *eth, const Bit8u *ip, unsigned ihl, const Bit8u *pktend, Bit32u srcip, Bit32u dstip);
  void handle_dhcp(const Bit8u *bootp, unsigned len, const Bit8u *client_mac);
  void handle_icmp(const Bit8u *eth, const Bit8u *ip, const Bit8u *icmp, unsigned icmplen);

  void send_ip_frame(Bit8u proto, Bit32u srcip, Bit32u dstip, const Bit8u *dst_mac,
                      const Bit8u *payload, unsigned paylen);
  void send_tcp_segment(TcpFlow &flow, Bit8u flags, const Bit8u *data, unsigned datalen);
  void send_synack(TcpFlow &flow);
  void ack_pending(TcpFlow &flow, Bit32u ack_num);
  void CheckRetransmits(void);
  void queue_frame(const Bit8u *frame, unsigned len);

  static void tx_timer_handler(void *this_ptr);
};

static void nat_udp_data_cb(int handle, const uint8_t *data, int length, void *user)
{
  ((bx_nat_pktmover_c *)user)->OnUdpData(handle, data, length);
}
static void nat_tcp_connect_cb(int handle, int success, void *user)
{
  ((bx_nat_pktmover_c *)user)->OnTcpConnect(handle, success);
}
static void nat_tcp_data_cb(int handle, const uint8_t *data, int length, void *user)
{
  ((bx_nat_pktmover_c *)user)->OnTcpData(handle, data, length);
}
static void nat_tcp_closed_cb(int handle, void *user)
{
  ((bx_nat_pktmover_c *)user)->OnTcpClosed(handle);
}

class bx_nat_locator_c : public eth_locator_c {
public:
  bx_nat_locator_c(void) : eth_locator_c("nat") {}
protected:
  eth_pktmover_c *allocate(const char *netif, const char *macaddr,
			   eth_rx_handler_t rxh, void *rxarg) {
    return (new bx_nat_pktmover_c(netif, macaddr, rxh, rxarg));
  }
} bx_nat_match;

bx_nat_pktmover_c::bx_nat_pktmover_c(const char *netif, const char *macaddr,
				     eth_rx_handler_t rxh, void *rxarg)
{
  this->rxh   = rxh;
  this->rxarg = rxarg;

  memcpy(guest_mac, macaddr, 6);

  gw_mac[0] = 0x52; gw_mac[1] = 0x54; gw_mac[2] = 0x00;
  gw_mac[3] = 0x12; gw_mac[4] = 0x34; gw_mac[5] = 0x56;

  gw_ip    = (10u << 24) | (0u << 16) | (2u << 8) | 2u;
  guest_ip = (10u << 24) | (0u << 16) | (2u << 8) | 15u;
  dns_ip   = gw_ip;
  netmask  = 0xffffff00u;
  next_isn = 0x10000;
  now_tick = 0;

  tx_timer_index = bx_pc_system.register_timer(this, tx_timer_handler, 2000, 1, 1, "eth_nat");

  BX_INFO(("nat backend: guest ip 10.0.2.15, gateway 10.0.2.2"));
}

void
bx_nat_pktmover_c::tx_timer_handler(void *this_ptr)
{
  ((bx_nat_pktmover_c *)this_ptr)->DrainQueue();
}

void
bx_nat_pktmover_c::DrainQueue(void)
{
  now_tick++;

  std::vector<std::vector<Bit8u>> pending;
  {
    std::lock_guard<std::mutex> lock(rx_mutex);
    pending.swap(rx_queue);
  }
  for (auto &frame : pending) {
    (rxh)(rxarg, frame.data(), (unsigned)frame.size());
  }

  CheckRetransmits();
}

void
bx_nat_pktmover_c::queue_frame(const Bit8u *frame, unsigned len)
{
  std::lock_guard<std::mutex> lock(rx_mutex);
  rx_queue.emplace_back(frame, frame + len);
}

void
bx_nat_pktmover_c::sendpkt(void *buf, unsigned io_len)
{
  const Bit8u *pkt = (const Bit8u *)buf;
  if (io_len < 14) return;

  Bit16u ethertype = nat_rd16(pkt + 12);
  if (ethertype == 0x0806) {
    handle_arp(pkt, io_len);
  } else if (ethertype == 0x0800) {
    handle_ip(pkt, io_len);
  }
}

void
bx_nat_pktmover_c::handle_arp(const Bit8u *buf, unsigned len)
{
  if (len < 14 + 28) return;
  const Bit8u *arp = buf + 14;
  Bit16u oper = nat_rd16(arp + 6);
  Bit32u tpa = nat_rd32(arp + 24);
  if (oper != 1 || tpa != gw_ip) return;

  Bit8u sender_mac[6];
  memcpy(sender_mac, arp + 8, 6);
  Bit32u sender_ip = nat_rd32(arp + 14);

  Bit8u reply[42];
  memcpy(reply, sender_mac, 6);
  memcpy(reply + 6, gw_mac, 6);
  nat_wr16(reply + 12, 0x0806);

  Bit8u *a = reply + 14;
  nat_wr16(a, 1);
  nat_wr16(a + 2, 0x0800);
  a[4] = 6; a[5] = 4;
  nat_wr16(a + 6, 2);
  memcpy(a + 8, gw_mac, 6);
  nat_wr32(a + 14, gw_ip);
  memcpy(a + 18, sender_mac, 6);
  nat_wr32(a + 24, sender_ip);

  queue_frame(reply, sizeof(reply));
}

void
bx_nat_pktmover_c::handle_ip(const Bit8u *buf, unsigned len)
{
  if (len < 14 + 20) return;
  const Bit8u *ip = buf + 14;
  unsigned ihl = (ip[0] & 0x0f) * 4;
  if (len < 14 + ihl) return;
  Bit8u proto = ip[9];
  Bit32u srcip = nat_rd32(ip + 12);
  Bit32u dstip = nat_rd32(ip + 16);

  if (proto == 17) {
    if (len < 14 + ihl + 8) return;
    const Bit8u *udp = ip + ihl;
    unsigned udplen = nat_rd16(udp + 4);
    if (udplen < 8 || (14 + ihl + udplen) > len) return;
    handle_udp(buf, ip, udp, udplen, srcip, dstip);
  } else if (proto == 1) {
    if (dstip != gw_ip) return;
    const Bit8u *icmp = ip + ihl;
    unsigned icmplen = len - (14 + ihl);
    handle_icmp(buf, ip, icmp, icmplen);
  } else if (proto == 6) {
    handle_tcp(buf, ip, ihl, buf + len, srcip, dstip);
  }
}

void
bx_nat_pktmover_c::handle_udp(const Bit8u *eth, const Bit8u *ip, const Bit8u *udp, unsigned udplen, Bit32u srcip, Bit32u dstip)
{
  Bit16u srcport = nat_rd16(udp);
  Bit16u dstport = nat_rd16(udp + 2);

  if (srcport == 68 && dstport == 67) {
    handle_dhcp(udp + 8, udplen - 8, eth + 6);
    return;
  }

  Bit32u real_ip = dstip;
  if (dstip == gw_ip) {
    if (dstport != 53) return;
    real_ip = NAT_DNS_SERVER_IP;
  }

  Bit64u key = ((Bit64u)srcport << 48) | ((Bit64u)dstip << 16) | dstport;
  int handle;
  {
    std::lock_guard<std::mutex> lock(flow_mutex);
    auto it = udp_by_key.find(key);
    if (it != udp_by_key.end()) {
      handle = it->second;
    } else {
      char ipstr[16];
      nat_ip_to_str(real_ip, ipstr);
      handle = wpb_net_udp_open(ipstr, dstport, nat_udp_data_cb, this);
      udp_by_key[key] = handle;
      UdpFlow flow;
      flow.guest_port = srcport;
      flow.dst_ip = dstip;
      flow.dst_port = dstport;
      flow.real_ip = real_ip;
      udp_by_handle[handle] = flow;
    }
  }
  wpb_net_udp_send(handle, udp + 8, udplen - 8);
}

void
bx_nat_pktmover_c::OnUdpData(int handle, const Bit8u *data, int length)
{
  UdpFlow flow;
  {
    std::lock_guard<std::mutex> lock(flow_mutex);
    auto it = udp_by_handle.find(handle);
    if (it == udp_by_handle.end()) return;
    flow = it->second;
  }

  Bit8u pkt[14 + 20 + 8 + 1472];
  if ((unsigned)length > 1472) length = 1472;
  Bit8u *udp = pkt + 14 + 20;
  memcpy(udp + 8, data, length);
  nat_wr16(udp, flow.dst_port);
  nat_wr16(udp + 2, flow.guest_port);
  nat_wr16(udp + 4, (Bit16u)(8 + length));
  nat_wr16(udp + 6, 0);

  send_ip_frame(17, flow.dst_ip, guest_ip, guest_mac, udp, 8 + length);
}

void
bx_nat_pktmover_c::send_ip_frame(Bit8u proto, Bit32u srcip, Bit32u dstip, const Bit8u *dst_mac,
                                  const Bit8u *payload, unsigned paylen)
{
  Bit8u pkt[14 + 20 + 1500];
  if (paylen > 1500) paylen = 1500;

  memcpy(pkt, dst_mac, 6);
  memcpy(pkt + 6, gw_mac, 6);
  nat_wr16(pkt + 12, 0x0800);

  Bit8u *ip = pkt + 14;
  memcpy(ip + 20, payload, paylen);

  ip[0] = 0x45; ip[1] = 0;
  nat_wr16(ip + 2, (Bit16u)(20 + paylen));
  nat_wr16(ip + 4, 0);
  nat_wr16(ip + 6, 0);
  ip[8] = 64; ip[9] = proto;
  nat_wr16(ip + 10, 0);
  nat_wr32(ip + 12, srcip);
  nat_wr32(ip + 16, dstip);
  nat_wr16(ip + 10, nat_checksum(ip, 20));

  queue_frame(pkt, 14 + 20 + paylen);
}

void
bx_nat_pktmover_c::handle_dhcp(const Bit8u *bootp, unsigned len, const Bit8u *client_mac)
{
  if (len < 240) return;
  if (bootp[0] != 1) return;
  if (bootp[236] != 99 || bootp[237] != 130 || bootp[238] != 83 || bootp[239] != 99) return;

  Bit8u msgtype = 0;
  unsigned pos = 240;
  while (pos < len) {
    Bit8u code = bootp[pos++];
    if (code == 0xff) break;
    if (code == 0) continue;
    if (pos >= len) break;
    Bit8u optlen = bootp[pos++];
    if (code == 53 && optlen >= 1 && pos < len) msgtype = bootp[pos];
    pos += optlen;
  }
  if (msgtype != 1 && msgtype != 3) return;
  Bit8u replytype = (msgtype == 1) ? 2 : 5;

  Bit8u pkt[350];
  memset(pkt, 0, sizeof(pkt));

  memcpy(pkt, client_mac, 6);
  memcpy(pkt + 6, gw_mac, 6);
  nat_wr16(pkt + 12, 0x0800);

  Bit8u *ip = pkt + 14;
  Bit8u *udp = ip + 20;
  Bit8u *out = udp + 8;

  out[0] = 2;
  out[1] = 1;
  out[2] = 6;
  out[3] = 0;
  memcpy(out + 4, bootp + 4, 4);
  nat_wr32(out + 16, guest_ip);
  nat_wr32(out + 20, gw_ip);
  memcpy(out + 28, client_mac, 6);
  out[236] = 99; out[237] = 130; out[238] = 83; out[239] = 99;

  unsigned optpos = 240;
  out[optpos++] = 53; out[optpos++] = 1; out[optpos++] = replytype;
  out[optpos++] = 1; out[optpos++] = 4; nat_wr32(out + optpos, netmask); optpos += 4;
  out[optpos++] = 3; out[optpos++] = 4; nat_wr32(out + optpos, gw_ip); optpos += 4;
  out[optpos++] = 51; out[optpos++] = 4; nat_wr32(out + optpos, 86400); optpos += 4;
  out[optpos++] = 54; out[optpos++] = 4; nat_wr32(out + optpos, gw_ip); optpos += 4;
  out[optpos++] = 6; out[optpos++] = 4; nat_wr32(out + optpos, dns_ip); optpos += 4;
  out[optpos++] = 0xff;

  unsigned bootp_len = optpos;
  unsigned udp_len = 8 + bootp_len;
  unsigned ip_len = 20 + udp_len;
  unsigned frame_len = 14 + ip_len;

  nat_wr16(udp, 67);
  nat_wr16(udp + 2, 68);
  nat_wr16(udp + 4, (Bit16u)udp_len);
  nat_wr16(udp + 6, 0);

  ip[0] = 0x45; ip[1] = 0;
  nat_wr16(ip + 2, (Bit16u)ip_len);
  nat_wr16(ip + 4, 0);
  nat_wr16(ip + 6, 0);
  ip[8] = 64; ip[9] = 17;
  nat_wr16(ip + 10, 0);
  nat_wr32(ip + 12, gw_ip);
  nat_wr32(ip + 16, 0xffffffffu);
  nat_wr16(ip + 10, nat_checksum(ip, 20));

  queue_frame(pkt, frame_len);
}

void
bx_nat_pktmover_c::handle_icmp(const Bit8u *eth, const Bit8u *ip, const Bit8u *icmp, unsigned icmplen)
{
  if (icmplen < 8 || icmp[0] != 8) return;

  Bit8u pkt[14 + 20 + 1500];
  if (icmplen > 1500) icmplen = 1500;
  unsigned frame_len = 14 + 20 + icmplen;

  memcpy(pkt, eth + 6, 6);
  memcpy(pkt + 6, gw_mac, 6);
  nat_wr16(pkt + 12, 0x0800);

  Bit8u *outip = pkt + 14;
  Bit8u *outicmp = outip + 20;
  memcpy(outicmp, icmp, icmplen);
  outicmp[0] = 0;
  nat_wr16(outicmp + 2, 0);
  nat_wr16(outicmp + 2, nat_checksum(outicmp, icmplen));

  outip[0] = 0x45; outip[1] = 0;
  nat_wr16(outip + 2, (Bit16u)(20 + icmplen));
  nat_wr16(outip + 4, 0);
  nat_wr16(outip + 6, 0);
  outip[8] = 64; outip[9] = 1;
  nat_wr16(outip + 10, 0);
  nat_wr32(outip + 12, gw_ip);
  nat_wr32(outip + 16, nat_rd32(ip + 12));
  nat_wr16(outip + 10, nat_checksum(outip, 20));

  queue_frame(pkt, frame_len);
}

void
bx_nat_pktmover_c::send_tcp_segment(TcpFlow &flow, Bit8u flags, const Bit8u *data, unsigned datalen)
{
  if (datalen > 1400) datalen = 1400;
  Bit32u seq = flow.our_next_seq;

  Bit8u pkt[14 + 20 + 20 + 1400];
  Bit8u *tcp = pkt + 14 + 20;

  nat_wr16(tcp, flow.dst_port);
  nat_wr16(tcp + 2, flow.guest_port);
  nat_wr32(tcp + 4, seq);
  nat_wr32(tcp + 8, flow.guest_next_seq);
  tcp[12] = 0x50;
  tcp[13] = flags;
  nat_wr16(tcp + 14, 65535);
  nat_wr16(tcp + 16, 0);
  nat_wr16(tcp + 18, 0);
  if (datalen) memcpy(tcp + 20, data, datalen);

  Bit16u csum = nat_tcp_checksum(flow.dst_ip, guest_ip, tcp, 20 + datalen);
  nat_wr16(tcp + 16, csum);

  memcpy(pkt, guest_mac, 6);
  memcpy(pkt + 6, gw_mac, 6);
  nat_wr16(pkt + 12, 0x0800);

  Bit8u *ip = pkt + 14;
  unsigned iplen = 20 + 20 + datalen;
  ip[0] = 0x45; ip[1] = 0;
  nat_wr16(ip + 2, (Bit16u)iplen);
  nat_wr16(ip + 4, 0);
  nat_wr16(ip + 6, 0);
  ip[8] = 64; ip[9] = 6;
  nat_wr16(ip + 10, 0);
  nat_wr32(ip + 12, flow.dst_ip);
  nat_wr32(ip + 16, guest_ip);
  nat_wr16(ip + 10, nat_checksum(ip, 20));

  unsigned framelen = 14 + iplen;
  queue_frame(pkt, framelen);

  unsigned consumed = datalen + ((flags & 0x02) ? 1 : 0) + ((flags & 0x01) ? 1 : 0);
  if (consumed > 0) {
    PendingSeg p;
    p.seq_end = seq + consumed;
    p.raw.assign(pkt, pkt + framelen);
    p.sent_tick = now_tick;
    p.retries = 0;
    flow.unacked.push_back(p);
    flow.our_next_seq = seq + consumed;
  }
}

void
bx_nat_pktmover_c::send_synack(TcpFlow &flow)
{
  Bit32u seq = flow.our_next_seq;

  Bit8u pkt[14 + 20 + 24];
  Bit8u *tcp = pkt + 14 + 20;

  nat_wr16(tcp, flow.dst_port);
  nat_wr16(tcp + 2, flow.guest_port);
  nat_wr32(tcp + 4, seq);
  nat_wr32(tcp + 8, flow.guest_next_seq);
  tcp[12] = 0x60;
  tcp[13] = 0x12;
  nat_wr16(tcp + 14, 65535);
  nat_wr16(tcp + 16, 0);
  nat_wr16(tcp + 18, 0);
  tcp[20] = 2; tcp[21] = 4; nat_wr16(tcp + 22, 1400);

  Bit16u csum = nat_tcp_checksum(flow.dst_ip, guest_ip, tcp, 24);
  nat_wr16(tcp + 16, csum);

  memcpy(pkt, guest_mac, 6);
  memcpy(pkt + 6, gw_mac, 6);
  nat_wr16(pkt + 12, 0x0800);

  Bit8u *ip = pkt + 14;
  unsigned iplen = 20 + 24;
  ip[0] = 0x45; ip[1] = 0;
  nat_wr16(ip + 2, (Bit16u)iplen);
  nat_wr16(ip + 4, 0);
  nat_wr16(ip + 6, 0);
  ip[8] = 64; ip[9] = 6;
  nat_wr16(ip + 10, 0);
  nat_wr32(ip + 12, flow.dst_ip);
  nat_wr32(ip + 16, guest_ip);
  nat_wr16(ip + 10, nat_checksum(ip, 20));

  unsigned framelen = 14 + iplen;
  queue_frame(pkt, framelen);

  PendingSeg p;
  p.seq_end = seq + 1;
  p.raw.assign(pkt, pkt + framelen);
  p.sent_tick = now_tick;
  p.retries = 0;
  flow.unacked.push_back(p);
  flow.our_next_seq = seq + 1;
}

void
bx_nat_pktmover_c::ack_pending(TcpFlow &flow, Bit32u ack_num)
{
  auto it = flow.unacked.begin();
  while (it != flow.unacked.end()) {
    if ((Bit32s)(ack_num - it->seq_end) >= 0) it = flow.unacked.erase(it);
    else ++it;
  }
}

void
bx_nat_pktmover_c::CheckRetransmits(void)
{
  std::lock_guard<std::mutex> lock(flow_mutex);
  auto it = tcp_by_handle.begin();
  while (it != tcp_by_handle.end()) {
    TcpFlow &flow = it->second;
    bool drop = false;
    for (auto &seg : flow.unacked) {
      if (now_tick - seg.sent_tick < TCP_RETRANSMIT_TICKS) continue;
      if (seg.retries >= TCP_MAX_RETRIES) { drop = true; break; }
      queue_frame(seg.raw.data(), (unsigned)seg.raw.size());
      seg.sent_tick = now_tick;
      seg.retries++;
    }
    if (drop) {
      int handle = it->first;
      Bit64u key = ((Bit64u)flow.guest_port << 48) | ((Bit64u)flow.dst_ip << 16) | flow.dst_port;
      tcp_by_key.erase(key);
      it = tcp_by_handle.erase(it);
      wpb_net_tcp_close(handle);
    } else {
      ++it;
    }
  }
}

void
bx_nat_pktmover_c::handle_tcp(const Bit8u *eth, const Bit8u *ip, unsigned ihl, const Bit8u *pktend, Bit32u srcip, Bit32u dstip)
{
  const Bit8u *tcp = ip + ihl;
  if (tcp + 20 > pktend) return;
  Bit16u srcport = nat_rd16(tcp);
  Bit16u dstport = nat_rd16(tcp + 2);
  Bit32u seq = nat_rd32(tcp + 4);
  unsigned dataoff = (tcp[12] >> 4) * 4;
  Bit8u flags = tcp[13];
  const Bit8u *payload = tcp + dataoff;
  if (payload > pktend) return;
  unsigned paylen = (unsigned)(pktend - payload);

  Bit64u key = ((Bit64u)srcport << 48) | ((Bit64u)dstip << 16) | dstport;

  std::lock_guard<std::mutex> lock(flow_mutex);
  auto it = tcp_by_key.find(key);

  if (it == tcp_by_key.end()) {
    if (!(flags & 0x02)) return;

    TcpFlow flow;
    flow.guest_port = srcport;
    flow.dst_ip = dstip;
    flow.dst_port = dstport;
    flow.guest_next_seq = seq + 1;
    flow.our_next_seq = next_isn;
    next_isn += 0x4000;
    flow.state = TCP_SYN_RCVD;

    char ipstr[16];
    nat_ip_to_str(dstip, ipstr);
    int handle = wpb_net_tcp_open(ipstr, dstport, nat_tcp_connect_cb, nat_tcp_data_cb, nat_tcp_closed_cb, this);
    tcp_by_key[key] = handle;
    tcp_by_handle[handle] = flow;
    return;
  }

  int handle = it->second;
  auto ht = tcp_by_handle.find(handle);
  if (ht == tcp_by_handle.end()) return;
  TcpFlow &flow = ht->second;

  if (flags & 0x04) {
    tcp_by_key.erase(it);
    tcp_by_handle.erase(ht);
    wpb_net_tcp_close(handle);
    return;
  }

  if (flow.state != TCP_ESTABLISHED) return;

  if (flags & 0x10) {
    ack_pending(flow, nat_rd32(tcp + 8));
  }

  if (paylen > 0) {
    wpb_net_tcp_send(handle, payload, paylen);
    flow.guest_next_seq += paylen;
    send_tcp_segment(flow, 0x10, NULL, 0);
  }

  if (flags & 0x01) {
    flow.guest_next_seq += 1;
    send_tcp_segment(flow, 0x11, NULL, 0);
    wpb_net_tcp_close(handle);
    tcp_by_key.erase(it);
    tcp_by_handle.erase(ht);
  }
}

void
bx_nat_pktmover_c::OnTcpConnect(int handle, int success)
{
  std::lock_guard<std::mutex> lock(flow_mutex);
  auto ht = tcp_by_handle.find(handle);
  if (ht == tcp_by_handle.end()) return;
  TcpFlow &flow = ht->second;

  if (!success) {
    Bit64u key = ((Bit64u)flow.guest_port << 48) | ((Bit64u)flow.dst_ip << 16) | flow.dst_port;
    send_tcp_segment(flow, 0x14, NULL, 0);
    tcp_by_key.erase(key);
    tcp_by_handle.erase(ht);
    return;
  }

  flow.state = TCP_ESTABLISHED;
  send_synack(flow);
}

void
bx_nat_pktmover_c::OnTcpData(int handle, const Bit8u *data, int length)
{
  std::lock_guard<std::mutex> lock(flow_mutex);
  auto ht = tcp_by_handle.find(handle);
  if (ht == tcp_by_handle.end()) return;
  TcpFlow &flow = ht->second;
  if (flow.state != TCP_ESTABLISHED) return;

  unsigned remaining = (unsigned)length;
  const Bit8u *p = data;
  while (remaining > 0) {
    unsigned chunk = remaining > 1400 ? 1400 : remaining;
    send_tcp_segment(flow, 0x18, p, chunk);
    p += chunk;
    remaining -= chunk;
  }
}

void
bx_nat_pktmover_c::OnTcpClosed(int handle)
{
  std::lock_guard<std::mutex> lock(flow_mutex);
  auto ht = tcp_by_handle.find(handle);
  if (ht == tcp_by_handle.end()) return;
  TcpFlow &flow = ht->second;
  if (flow.state == TCP_ESTABLISHED) {
    send_tcp_segment(flow, 0x11, NULL, 0);
  }
  Bit64u key = ((Bit64u)flow.guest_port << 48) | ((Bit64u)flow.dst_ip << 16) | flow.dst_port;
  tcp_by_key.erase(key);
  tcp_by_handle.erase(ht);
}

#endif /* if BX_NE2K_SUPPORT */
