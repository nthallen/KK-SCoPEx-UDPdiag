/** @file UDPdiag.cc */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "dasio/loop.h"
#include "dasio/appid.h"
#include "UDPdiag.h"
#include "UDP_int.h"
#include "nl.h"
#include "nl_assert.h"
#include "oui.h"
#include "crc16modbus.h"
#include "dasio/tm_data_sndr.h"

DAS_IO::AppID_t DAS_IO::AppID("UDPdiag", "UDP Performance Diagnostic Tool", "V1.0");

uint16_t UDP_interface::crc_calc(uint8_t *buf, int len) {
  return crc16modbus_word(0, (void const *)buf, len);
}

int32_t UDP_interface::get_timestamp() {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts))
    msg(MSG_FATAL, "%s: clock_gettime() returned %d: %s",
      iname, errno, strerror(errno));
  uint32_t secs_today = (ts.tv_sec % (3600*24));
  uint32_t msecs = ts.tv_nsec/1000000;
  return (secs_today*1000)+msecs;
}

UDPdiag_t UDPdiag;

UDP_transmitter::UDP_transmitter(const char *rmt_ip, const char *rmt_port, UDP_tmr *tmr)
      : UDP_interface("UDPtx", 0),
        L2R_Int_packets_tx(0),
        L2R_Int_bytes_tx(0),
        Int_packets_tx(0),
        Int_bytes_tx(0),
        L2R_Transmit_SN(0),
        L2R_Packet_size(sizeof(UDPdiag_packet)),
        L2R_Packet_rate(0),
        L2R_command_len(0),
        tmr(tmr)
{
  // Create UDP socket and bind to local tx_port and remote hostname:rx_port
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) msg(3, "Unable to create UDP socket: %s", strerror(errno));
  /* The following is just for reference if sending to a broadcast address: */
  /*
  int broadcastEnable=1;
  int ret = setsockopt(bcast_sock, SOL_SOCKET, SO_BROADCAST,
    &broadcastEnable, sizeof(broadcastEnable));
  if (ret == -1) {
    nl_error(nl_response, "setsockopt failed: %s", strerror(errno));
    return;
  }
  */
  
  /* setup the local port */
  struct sockaddr_in s;
  memset((char *)&s, 0, sizeof(s));
  s.sin_family = AF_INET;
  s.sin_addr.s_addr = htonl(INADDR_ANY);
  s.sin_port = htons(0);
  if (bind(fd, (struct sockaddr *)&s, sizeof(s)))
    msg(MSG_FATAL, "%s: bind returned errno %d: %s",
        iname, errno, strerror(errno));

  /* Setup the remote address. This assumes address is numeric */
  struct addrinfo hints, *res = 0;
  hints.ai_flags = AI_NUMERICHOST;
  hints.ai_family = PF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  hints.ai_addrlen = 0;
  hints.ai_canonname = 0;
  hints.ai_addr = 0;
  hints.ai_next = 0;
  int gai_err = getaddrinfo(rmt_ip, rmt_port, &hints, &res);
  if (gai_err) {
    msg(MSG_FATAL, "%s: getaddrinfo failed: %s",
        iname, gai_strerror(gai_err));
  }
  nl_assert(res);
  nl_assert(res->ai_next == 0);
  nl_assert(res->ai_addr != 0);
  nl_assert(((unsigned)res->ai_addrlen) <= sizeof(s));
  memcpy(&s, res->ai_addr, res->ai_addrlen);
  int addrlen = res->ai_addrlen;
  freeaddrinfo(res);
  
  if (connect(fd, (const sockaddr*)&s, addrlen))
    msg(MSG_FATAL, "%s: connect returned errno %d: %s",
        iname, errno, strerror(errno));

  pkt = (UDPdiag_packet*)new_memory(max_packet_size);
  flags = DAS_IO::Interface::gflag(0);
  nl_assert(tmr);
  tmr->set_transmitter(this);
}

// Commands:
//   S:\d+  Set packet size
//   R:\d+  Set packet rate
//   Q      Quit
//   XS:\d+ Remote Set packet size
//   XR:\d+ Remote Set packet rate
//   XQ     Remote Quit
bool UDP_transmitter::parse_command(char *cmd, unsigned cmdlen) {
  if (cmd && cmdlen <= 7) {
    buf = (unsigned char *)cmd;
    nc = cmdlen;
    switch (buf[0]) {
      case 'S':
        if (not_str("S:") || not_uint16(L2R_Packet_size)) {
          report_err("%s: Invalid S command syntax", iname);
          consume(nc);
        } else {
          report_ok(nc);
        }
        break;
      case 'R':
        if (not_str("R:") || not_uint16(L2R_Packet_rate)) {
          report_err("%s: Invalid R command syntax", iname);
          consume(nc);
        } else {
          report_ok(nc);
          int per_nsecs = L2R_Packet_rate == 0 ? 0 :
            (1000000000/(int)L2R_Packet_rate);
          tmr->settime(per_nsecs);
        }
        break;
      case 'Q':
        report_ok(nc);
        return true;
      case 'X':
        report_ok(nc);
        L2R_command_len = cmdlen-1;
        for (int i = 1; i < cmdlen; ++i) {
          L2R_command[i-1] = cmd[i];
        }
        break;
    }
    return false;
  }
}

bool UDP_transmitter::transmit(uint16_t n_pkts) {
  bool rv;
  for (int i = 0; i < n_pkts; ++i) {
    if (!obuf_empty()) return false;
    // build the packet
    pkt->Command_bytes = L2R_command_len;
    pkt->Packet_size = sizeof(UDPdiag_packet) + L2R_command_len;
    if (pkt->Packet_size < L2R_Packet_size)
      pkt->Packet_size = L2R_Packet_size;
    pkt->Packet_rate = L2R_Packet_rate;
    pkt->Int_packets_tx = L2R_Int_packets_tx;
    pkt->Transmit_SN = L2R_Transmit_SN;
    pkt->Receive_SN = UDPdiag.R2L.Receive_SN;
    pkt->Int_packets_rx = UDPdiag.R2L.Int_packets_rx;
    pkt->Int_min_latency = UDPdiag.R2L.Int_min_latency;
    pkt->Int_mean_latency = UDPdiag.R2L.Int_mean_latency;
    pkt->Int_max_latency = UDPdiag.R2L.Int_max_latency;
    pkt->Int_bytes_rx = UDPdiag.R2L.Int_bytes_rx;
    pkt->Total_valid_packets_rx = UDPdiag.R2L.Total_valid_packets_rx;
    pkt->Total_invalid_packets_rx = UDPdiag.R2L.Total_invalid_packets_rx;
    
    int j;
    for (j = 0; j < L2R_command_len; ++j) {
      pkt->Remainder[j] = L2R_command[j];
    }
    pkt->Transmit_timestamp = get_timestamp();
    
    for (; j < pkt->Packet_size - 2; ++j) {
      pkt->Remainder[j] = (uint8_t)rand();
    }
    crc_set();
    rv = iwrite((char *)pkt, pkt->Packet_size);
    ++L2R_Transmit_SN;
    ++Int_packets_tx;
    Int_bytes_tx += pkt->Packet_size;
    if (rv) return true;
  }
  return rv;
}

bool UDP_transmitter::tm_sync() {
  L2R_Int_packets_tx = Int_packets_tx;
  L2R_Int_bytes_tx = Int_bytes_tx;
  Int_packets_tx = 0;
  Int_bytes_tx = 0;
  UDPdiag.L2R.Int_packets_tx = L2R_Int_packets_tx;
  UDPdiag.L2R.Int_bytes_tx = L2R_Int_bytes_tx;
  UDPdiag.L2R.Total_packets_tx = L2R_Transmit_SN;
  return false;
}

void UDP_transmitter::crc_set() {
  uint8_t *data = (uint8_t*)pkt;
  uint16_t crc = crc_calc(data, pkt->Packet_size - 2);
  data[pkt->Packet_size-2] = crc & 0xFF;
  data[pkt->Packet_size-1] = (crc >> 8) & 0xFF;
}

UDP_receiver::UDP_receiver(const char *port, bool allow_remote_commands,
                            UDP_transmitter *tx)
      : UDP_interface("UDPrx", 10000),
        recv_port(port),
        allow_remote_commands(allow_remote_commands),
        tx(tx),
        R2L_Total_packets_rx(0),
        R2L_Total_valid_packets_rx(0),
        R2L_Total_invalid_packets_rx(0),
        R2L_Int_packets_rx(0),
        R2L_Int_min_latency(0),
        R2L_Int_max_latency(0),
        R2L_Int_bytes_rx(0),
        R2L_latencies(0)
{
  // Create UDP socket and bind to local port
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    msg(3, "%s: Unable to create UDP socket: %s",
        iname, strerror(errno));
  
  /* setup the local port */
  struct sockaddr_in s;
  memset((char *)&s, 0, sizeof(s));
  s.sin_family = AF_INET;
  s.sin_addr.s_addr = htonl(INADDR_ANY);
  s.sin_port = htons(atoi(port));
  if (bind(fd, (struct sockaddr*)&s, sizeof(s)))
    msg(MSG_FATAL, "%s: bind returned errno %d: %s",
        iname, errno, strerror(errno));

  flags = DAS_IO::Interface::Fl_Read | DAS_IO::Interface::gflag(0);
  pkt = (UDPdiag_packet *)buf;
}

bool UDP_receiver::protocol_input() {
  bool rv = false;
  int32_t now = get_timestamp();
  
  ++R2L_Total_packets_rx;
  if (nc < sizeof(UDPdiag_packet)) {
    ++R2L_Total_invalid_packets_rx;
    size_t expected = sizeof(UDPdiag_packet);
    if (nc < offsetof(UDPdiag_packet, Transmit_SN))
      expected = pkt->Packet_size;
    report_err("%s: Recd %d/%d byte packet", iname, nc, expected);
    consume(nc);
    return false;
  }
  if (pkt->Packet_size != nc ||
      sizeof(UDPdiag_packet) + pkt->Command_bytes > pkt->Packet_size) {
    ++R2L_Total_invalid_packets_rx;
    report_err("%s: Packet_size(%u) != nc(%u) or minsize(%u)+Cmd(%d) > Packet_size",
      iname, pkt->Packet_size, nc, sizeof(UDPdiag_packet), pkt->Command_bytes);
    consume(nc);
    return false;
  }
  if (!crc_ok()) {
    ++R2L_Total_invalid_packets_rx;
    report_err("%s: CRC error", iname);
    consume(nc);
    return false;
  }
  
  int32_t latency = now - pkt->Transmit_timestamp;
  if (R2L_Int_packets_rx == 0) {
    R2L_latencies = R2L_Int_min_latency = R2L_Int_max_latency = latency;
  } else {
    if (latency < R2L_Int_min_latency) R2L_Int_min_latency = latency;
    else if (latency > R2L_Int_max_latency) R2L_Int_max_latency = latency;
    R2L_latencies += latency;
  }
  ++R2L_Int_packets_rx;
  ++R2L_Total_valid_packets_rx;
  if (UDPdiag.R2L.Receive_SN > 0 && pkt->Transmit_SN <= UDPdiag.R2L.Receive_SN) {
    report_err("%s: Rx SN %u <= previous by %u", iname, pkt->Transmit_SN,
      UDPdiag.R2L.Receive_SN - pkt->Transmit_SN);
  }
  UDPdiag.R2L.Receive_SN = pkt->Transmit_SN;
  UDPdiag.L2R.Receive_SN = pkt->Receive_SN;
  R2L_Int_bytes_rx += pkt->Packet_size;
  UDPdiag.R2L.Int_packets_tx = pkt->Int_packets_tx;
  UDPdiag.L2R.Int_packets_rx = pkt->Int_packets_rx;
  UDPdiag.L2R.Int_min_latency = pkt->Int_min_latency;
  UDPdiag.L2R.Int_mean_latency = pkt->Int_mean_latency;
  UDPdiag.L2R.Int_max_latency = pkt->Int_max_latency;
  UDPdiag.L2R.Int_bytes_rx = pkt->Int_bytes_rx;
  UDPdiag.L2R.Total_valid_packets_rx = pkt->Total_valid_packets_rx;
  UDPdiag.L2R.Total_invalid_packets_rx = pkt->Total_invalid_packets_rx;
  
  if (pkt->Command_bytes > 0 && allow_remote_commands) {
    rv = tx->parse_command((char *)(&pkt->Remainder[0]), pkt->Command_bytes);
  }
  report_ok(nc);
  return rv;
}

bool UDP_receiver::tm_sync() {
  // Update UDPdiag struct with current readings,
  // then clear our interval counters
  UDPdiag.R2L.Int_packets_rx = R2L_Int_packets_rx;
  UDPdiag.R2L.Int_min_latency = R2L_Int_min_latency;
  UDPdiag.R2L.Int_max_latency = R2L_Int_max_latency;
  UDPdiag.R2L.Int_mean_latency = R2L_Int_packets_rx ?
    R2L_latencies/R2L_Int_packets_rx : 0;
  UDPdiag.R2L.Int_bytes_rx = R2L_Int_bytes_rx;
  UDPdiag.R2L.Total_valid_packets_rx = R2L_Total_valid_packets_rx;
  UDPdiag.R2L.Total_invalid_packets_rx = R2L_Total_invalid_packets_rx;
  R2L_Int_packets_rx = 0;
  R2L_Int_min_latency = 0;
  R2L_Int_max_latency = 0;
  R2L_latencies = 0;
  R2L_Int_bytes_rx = 0;
  return false;
}

bool UDP_receiver::crc_ok() {
  uint16_t crc = crc_calc(buf, nc-2);
  return buf[nc-2] == (crc & 0xFF) && buf[nc-1] == ((crc>>8)&0xFF);
}

UDP_cmd::UDP_cmd(UDP_transmitter *tx)
    : DAS_IO::Client("cmd", 40, "cmd", "UDPdiag"),
      tx(tx) {}

bool UDP_cmd::protocol_input() {
  bool rv = tx->parse_command((char*)&buf[0], nc);
  report_ok(nc);
  return rv;
}

UDP_tmr::UDP_tmr()
      : tm_tmr(),
        tx(0)
{}

void UDP_tmr::set_transmitter(UDP_transmitter *tx) {
  this->tx = tx;
}

bool UDP_tmr::protocol_input() {
  report_ok(nc);
  uint16_t n_pkts =
    (n_expirations < 60000) ? (uint16_t)n_expirations : 60000;
  return  tx ? tx->transmit(n_pkts) : false;
}

bool allow_remote_commands = false;
const char *remote_ip, *rx_port, *tx_port;

void UDPdiag_init_options(int argc, char **argv) {
  int optltr;

  optind = OPTIND_RESET;
  opterr = 0;
  while ((optltr = getopt(argc, argv, opt_string)) != -1) {
    switch (optltr) {
      case 'c': allow_remote_commands = true; break;
      case 'r': rx_port = optarg; break;
      case 't': tx_port = optarg; break;
      case 'i': remote_ip = optarg; break;
      case '?':
        msg(3, "Unrecognized Option -%c", optopt);
      default:
        break;
    }
  }
  if (remote_ip == 0)
    msg(MSG_FATAL, "Must specify remote IP address with -i option");
  if (rx_port == 0)
    msg(MSG_FATAL, "Must specify receive port with -r option");
  if (tx_port == 0)
    msg(MSG_FATAL, "Must specify remote port with -t option");
}

int main(int argc, char **argv) {
  oui_init_options(argc, argv);
  DAS_IO::Loop ELoop;
  UDP_tmr *tmr = new UDP_tmr();
  ELoop.add_child(tmr);
  UDP_transmitter *tx = new UDP_transmitter(remote_ip, tx_port, tmr);
  ELoop.add_child(tx);
  UDP_receiver *rx = new UDP_receiver(rx_port, allow_remote_commands, tx);
  ELoop.add_child(rx);
  
  DAS_IO::TM_data_sndr *tm = new DAS_IO::TM_data_sndr("TM", "UDPdiag", (const char *)&UDPdiag, sizeof(UDPdiag));
  ELoop.add_child(tm);
  tm->connect();
  
  UDP_cmd *cmd = new UDP_cmd(tx);
  cmd->connect();
  ELoop.add_child(cmd);
  msg(MSG, "%s %s Starting",
    DAS_IO::AppID.fullname, DAS_IO::AppID.rev);
  ELoop.event_loop();
  msg(MSG, "Terminating");
}
