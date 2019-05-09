/** @file UDPdiag.cc */
#include "UDPdiag.h"
#include "UDP_int.h"

UDPdiag_t UDPdiag;

UDP_transmitter::UDP_transmitter(const char *hostname, int port, UDP_tmr *tmr)
      : DAS_IO::Interface("UDPtx", 0),
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
  // ### Create UDP socket and bind to hostname:port
  pkt = (UDP_diag_packet*)new_memory(max_packet_size);
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
bool UDP_transmitter::parse_command(const char *cmd, unsigned cmdlen) {
  if (cmd && cmdlen <= 7) {
    buf = cmd;
    nc = cmdlen;
    switch (buf[0]) {
      case 'S':
        if (not_str(":") || not_uint16(L2R_Packet_size)) {
          report_err("%s: Invalid S command syntax", iname);
        } else {
          report_ok(nc);
        }
        break;
      case 'R':
        if (not_str(":") || not_uint16(L2R_Packet_rate)) {
          report_err("%s: Invalid R command syntax", iname);
        } else {
          report_ok(nc);
          int per_nsecs = L2R_Packet_rate == 0 ? 0 :
            (1000000000/(int)L2R_Packet_rate);
          tmr->settime(0, per_nsecs);
        }
        break;
      case 'Q':
        return true;
      case 'X':
        L2R_command_len = cmdlen-1;
        for (i = 1; i < cmdlen; ++i) {
          L2R_command[i-1] = cmd[i];
        }
        break;
    }
    return false;
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
    //### Get the receive statistics
    for (int j = 0; j < L2R_command_len; ++j) {
      pkt->Remainder[j] = L2R_command[j];
    }
    pkt->Transmit_timestamp = get_timestamp();
    for (; j < pkt->Packet_size - 2; ++j) {
      pkt->Remainder[j] = random_stuff();
    }
    crc_set();
    rv = iwrite(pkt, pkt->Packet_size);
    ++L2R_Transmit_SN;
    ++Int_packets_tx;
    Int_bytes_tx += pkt->Packet_size);
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
  UDPdiag.L2R.Total_packets_tx = L2R_Transmit_SN;
  return false;
}

UDP_receiver::UDP_receiver(int port, bool allow_remote_commands,
                            UDP_transmitter *tx)
      : DAS_IO::Interface("UDPrx", 10000),
        recv_port(port),
        allow_remote_commands(allow_remote_commands),
        tx(tx),
        R2L_Total_packets_rx(0),
        R2L_Total_valid_packets_rx(0),
        R2L_n_corrupt(0),
        R2L_Total_CRC_errors(0),
        R2L_Total_packets_lost(0),
        R2L_Int_packets_rx(0),
        R2L_Int_min_latency(0),
        R2L_Int_max_latency(0),
        R2L_Int_bytes_rx(0),
        R2L_latencies(0),
        R2L_Receive_SN(0),
        Rmt_Int_packets_tx(0),
        Rmt_Total_packets_rx(0)
{
  // Create UDP socket and bind to local port
  flags = DAS_IO::Interface::Fl_Read | DAS_IO::Interface::gflag(0);
  pkt = (UDPdiag_packet *)buf;
  tx->set_receiver(this);
}

bool UDP_receiver::protocol_input() {
  bool rv = false;
  int32_t now = get_timestamp();
  
  // ### Check the incoming packet, log and record data for telemetry
  ++R2L_Total_packets_rx;
  if (nc < sizeof(UDPdiag_packet)) {
    ++R2L_n_corrupt;
    size_t expected = sizeof(UDPdiag_packet);
    if (nc < offsetof(UDPdiag_packet, Transmit_SN))
      expected = pkt->Packet_size;
    report_err("%s: Recd %d/%d byte packet", iname, nc, expected);
    consume(nc);
    return false;
  }
  if (pkt->Packet_size != nc ||
      sizeof(UDPdiag_packet) + pkt->Command_bytes > pkt->Packet_size) {
    ++R2L_n_corrupt;
    report_err("%s: Packet_size(%u) != nc(%u) or minsize(%u)+Cmd(%d) > Packet_size",
      iname, pkt->Packet_size, nc, sizeof(UDPdiag_packet), pkt->Command_bytes);
    consume(nc);
    return false;
  }
  if (!crc_ok()) {
    ++R2L_Total_CRC_errors;
    report_err("%s: CRC error", iname);
    consume(nc);
    return false;
  }
  
  int32_t latency = now - pkt->Transmit_timestamp;
  if (R2L_Int_packets_rx == 0) {
    R2L_latencies = R2L_Int_min_latency = R2L_Int_mean_latency = R2L_Int_max_latency = latency;
  } else {
    if (latency < R2L_Int_min_latency) R2L_Int_min_latency = latency;
    else if (latency > R2L_Int_max_latency) R2L_Int_max_latency = latency;
    R2L_latencies += latency;
  }
  ++R2L_Int_packets_rx;
  ++R2L_Total_valid_packets_rx;
  if (pkt->Transmit_SN <= R2L_Receive_SN) {
    report_err("%s: Rx SN <= previous by %d", iname, R2L_Receive_SN - pkt->Transmit_SN);
  }
  R2L_Receive_SN = pkt->Transmit_SN;
  UDPdiag.L2R.Receive_SN = pkt->Receive_SN;
  R2L_Int_bytes_rx += pkt->Packet_size;
  R2L_Int_packets_tx = pkt->Int_packets_tx;
  UDPdiag.L2R.Int_packets_rx = pkt->Int_packets_rx;
  UDPdiag.L2R.Int_min_latency = pkt->Int_min_latency;
  UDPdiag.L2R.Int_mean_latency = pkt->Int_mean_latency;
  UDPdiag.L2R.Int_max_latency = pkt->Int_max_latency;
  UDPdiag.L2R.Int_bytes_rx = pkt->Int_bytes_rx;
  UDPdiag.L2R.Total_valid_packets_rx = pkt->Total_valid_packets_rx;
  UDPdiag.L2R.Total_invalid_packets_rx = pkt->Total_invalid_packets_rx;
  
  if (pkt->Command_bytes > 0 && allow_remote_commands) {
    rv = tx->parse_command((const char *)(pkt->Remainder[0]));
  }
  report_ok(nc);
  return rv;
}

bool UDP_receiver::tm_sync() {
  // Update UDPdiag struct with current readings,
  // then clear our interval counters
  UDPdiag.R2L.Int_valid_packets_rx = R2L_Int_packets_rx;
  UDPdiag.R2L.Int_min_latency = R2L_Int_min_latency;
  UDPdiag.R2L.Int_max_latency = R2L_Int_max_latency;
  UDPdiag.R2L_Int_mean_latency = R2L_Int_packets_rx ?
    R2L_latencies/R2L_Int_packets_rx : 0;
  UDPdiag.R2L_Int_bytes_rx = R2L_Int_bytes_rx;
  UDPdiag.R2L_Total_packets_rx = R2L_Total_valid_packets_rx;
  UDPdiag.R2L_Total_packets_rx = R2L_Total_invalid_packets_rx;
  R2L_Int_packets_rx = 0;
  R2L_Int_min_latency = 0;
  R2L_Int_max_latency = 0;
  R2L_latencies = 0;
  R2L_Int_bytes_rx = 0;
  return false;
}

UDP_cmd::UDP_cmd(UDP_transmitter *tx)
    : DAS_IO::Client("cmd", 40, "cmd", "UDPdiag"),
      tx(tx) {}

bool UDP_cmd::protocol_input() {
  bool rv = tx->parse_command(&buf[0]);
  report_ok(nc);
  return rv;
}

UDP_tmr::UDP_tmr()
      : UDP_tmr(),
        tx(0)
{}

UDP_tmr::set_transmitter(UDP_transmitter *tx) {
  this->tx = tx;
}

bool UDP_tmr::protocol_input() {
  uint16_t n_pkts =
    (n_expirations < 60000) ? (uint16_t)n_expirations : 60000;
  return  tx ? tx->transmit(n_pkts) : false;
}
