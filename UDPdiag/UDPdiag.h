#ifndef UDPDIAG_H_INCLUDED
#define UDPDIAG_H_INCLUDED

/** Telemetry data
 * These measurements correspond to a particular direction,
 * not a particular computer or interface. L2R designates
 * Local to Remote transmission and R2L is the reverse.
 * Information that pertains to the local interface
 * is calculated locally and is likely reported with less
 * latency. Information that pertains to the remote interface
 * must be calculated at the remote system and transmitted,
 * so can only be reported when tranmission is successful.
 *
 * Since any transmission involves both interfaces, both
 * L2R and R2L will include both local and remote data. The
 * data that is local for one is remote for the other. This
 * means it is difficult to directly overlay the local and
 * remote data. For example, the Int_packets_tx records the
 * number of packets transmitted during the current interval
 * (second) and Int_packets_rx records the number of packets
 * received. In the L2R case, Int_packets_tx is reported at
 * the end of the interval. If the system clocks and telemetry
 * are in perfect sync, then the intervals would agree, but
 * the remote data will then be sent from R2L and only logged
 * with telemetry at the following interval.
 *
 * Note that Total packets transmitted is current Transmit_SN.
 * When this pertains to the Remote site, Transmit_SN of course
 * will be the Transmit_SN in the lastest packet received.
 */
typedef struct __attribute__((packed)) {
  uint32_t Int_packets_tx;
  uint32_t Int_bytes_tx;
  uint32_t Total_packets_tx;
	uint32_t Int_packets_rx;
	 int32_t Int_min_latency;
	 int32_t Int_mean_latency;
	 int32_t Int_max_latency;
  uint32_t Int_bytes_rx;
	uint32_t Total_valid_packets_rx;
	uint32_t Total_invalid_packets_rx;
	uint32_t Receive_SN;
} UDP_Stats_t;

typedef struct __attribute__((packed)) {
  UDP_Stats_t L2R;
  UDP_Stats_t R2L;
} UDPdiag_t;

extern UDPdiag_t UDPdiag;

#endif