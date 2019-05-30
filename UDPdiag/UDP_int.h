#ifndef UDP_INT_H_INCLUDED
#define UDP_INT_H_INCLUDED
#include <stdint.h>
#include "dasio/interface.h"
#include "dasio/client.h"
#include "dasio/tm_tmr.h"
#include "UDPdiag.h"

extern bool allow_remote_commands;
extern const char *remote_ip, *rx_port, *tx_port;
void UDPdiag_init_options(int argc, char **argv);

typedef struct __attribute__((packed)) {
  uint16_t Command_bytes;
  uint16_t Packet_size;
  uint16_t Packet_rate;
  /** Number of packets transmitted during last second */
  uint16_t Int_packets_tx;
  /** The SN of this packet */
  uint32_t Transmit_SN;
  /** The SN of the last packet received */
  uint32_t Receive_SN;
  /** msecs since midnight utc */
  int32_t  Transmit_timestamp;
  /** Number of packets received during last second */
  uint32_t Int_packets_rx;
  /** Minimum receive latency during last second */
  uint32_t Int_min_latency;
  /** Mean receive latency during last second */
  uint32_t Int_mean_latency;
  /** Maximum receive latency during last second */
  uint32_t Int_max_latency;
  /** Total bytes received during last second */
  uint32_t Int_bytes_rx;
  /** Interval bytes transmitted during last second */
  uint32_t Int_bytes_tx;
  /** Total valid packets received */
  uint32_t Total_valid_packets_rx;
  /** Total invalid packets received */
  uint32_t Total_invalid_packets_rx;
  uint8_t  Remainder[2];
  // All the padding and commands go in before the CRC
} UDPdiag_packet;

class UDP_interface : public DAS_IO::Interface {
  public:
    inline UDP_interface(const char *name, int bufsz) :
      DAS_IO::Interface(name, bufsz) {}
  protected:
    uint16_t crc_calc(uint8_t *buf, int len);
    int32_t get_timestamp();
};

class UDP_tmr;
class UDP_receiver;

class UDP_transmitter : public UDP_interface {
  public:
    UDP_transmitter(const char *rmt_ip, const char *rmt_port,
      UDP_tmr *tmr);
    bool parse_command(char *cmd, unsigned cmdlen);
    bool transmit(uint16_t n_pkts);
    bool tm_sync_too();
  protected:
    void crc_set();
    UDPdiag_packet *pkt;
    uint32_t L2R_Int_packets_tx;
    uint32_t L2R_Int_bytes_tx;
    uint32_t Int_packets_tx;
    uint32_t Int_bytes_tx;
    uint32_t L2R_Transmit_SN;
    uint16_t L2R_Packet_size;
    uint16_t L2R_Packet_rate;
    uint8_t L2R_command_len;
    uint8_t L2R_command[8];
    static const int max_packet_size = 8000;
    UDP_tmr *tmr;
    UDP_receiver *rx;
};

class UDP_receiver : public UDP_interface {
  public:
    UDP_receiver(const char *port, bool allow_remote_commands, UDP_transmitter *tx);
  protected:
    bool protocol_input();
    bool tm_sync();
    bool crc_ok();
    const char *recv_port;
    bool allow_remote_commands;
    UDP_transmitter *tx;
    UDPdiag_packet *pkt;
    uint32_t R2L_Total_packets_rx;
    uint32_t R2L_Total_valid_packets_rx;
    uint32_t R2L_Total_invalid_packets_rx;
    uint32_t R2L_Int_packets_rx;
    int32_t  R2L_Int_min_latency;
    int32_t  R2L_Int_max_latency;
    uint32_t R2L_Int_bytes_rx;
    int32_t  R2L_latencies;
    // uint32_t L2R_Int_packets_tx;
};

class UDP_cmd : public DAS_IO::Client {
  public:
    UDP_cmd(UDP_transmitter *tx);
  protected:
    bool protocol_input();
    UDP_transmitter *tx;
};

class UDP_tmr : public DAS_IO::tm_tmr {
  public:
    UDP_tmr();
    void set_transmitter(UDP_transmitter *tx);
  protected:
    bool protocol_input();
    UDP_transmitter *tx;
};

#endif
