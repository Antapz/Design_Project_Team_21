#pragma once
#include <stdint.h>
#include <stddef.h>

/* ================= Frame Types ================= */
#define FT_DATA   0x01
#define FT_ACK    0xA0
#define FT_NACK   0xA1

/* ============== Hamming(12,8) ================== */
void    hamming128_encode(uint8_t data8, uint16_t *cw12);
uint8_t hamming128_decode(uint16_t cw12, int *corrected);
void    hamming128_encode_buf(const uint8_t *in, size_t n, uint8_t *out);
/* decode 2N-byte coded payload back to N bytes; returns N or -1 if any uncorrectable */
int     hamming128_decode_buf(const uint8_t *in /*2N*/, size_t n /*N*/,
                              uint8_t *out, int *corrected_total, int *uncorr_total);

/* ============= Manchester coding =============== */
size_t  manchester_encode_bits (const uint8_t *in_bytes, size_t in_bits_len,
                                uint8_t *out_bytes, size_t out_bytes_cap);
size_t  manchester_encode_bytes(const uint8_t *in, size_t in_len,
                                uint8_t *out, size_t out_cap);
size_t  manchester_decode_bits (const uint8_t *in_bytes, size_t in_bits_len,
                                uint8_t *out_bytes, size_t out_bytes_cap,
                                int *violations);
size_t  manchester_decode_bytes(const uint8_t *in, size_t in_len,
                                uint8_t *out, size_t out_cap,
                                int *violations);

/* ================== Framing (no CRC) ===================== */
/* Frame: [A5][5A][TYPE][SEQ][LENL][LENH][PAYLOAD...] */
size_t  frame_build(uint8_t type, uint8_t seq,
                    const uint8_t *payload, uint16_t payload_len,
                    uint8_t *out, size_t out_cap);
size_t  frame_build_ack (uint8_t seq, uint8_t *out, size_t out_cap);
size_t  frame_build_nack(uint8_t seq, uint8_t *out, size_t out_cap);
int     frame_parse_header(const uint8_t *frm, size_t frlen,
                           uint8_t *type, uint8_t *seq, uint16_t *paylen);
int     frame_find_preamble(const uint8_t *buf, size_t len);

/* ================== Timing (DWT) ====================== */
void     timing_init(void);
void     timing_reset(void);
uint32_t timing_cycles(void);

/* integer µs conversion (no float printf required) */
static inline uint32_t cycles_to_us_u32(uint32_t cyc, uint32_t core_hz)
{
  return (uint32_t)(((uint64_t)cyc * 1000000ULL) / (uint64_t)core_hz);
}

/* ================== ARQ API ===================== */
typedef int (*ll_tx_fn)(const uint8_t *bytes, size_t len);
typedef int (*ll_rx_poll_fn)(uint8_t *buf, size_t cap);

/* Classic stop-and-wait (kept for compatibility) */
int ll_send_with_ack(const uint8_t *payload, uint16_t payload_len,
                     uint8_t *seq_io,
                     ll_tx_fn tx, ll_rx_poll_fn rx_poll,
                     uint32_t ack_timeout_ms, int max_retries,
                     size_t *out_len);

/* ---- Profiled stop-and-wait ---- */
typedef struct {
  uint32_t cyc_total;       /* everything combined */
  uint32_t cyc_frame;       /* build header + copy payload */
  uint32_t cyc_hamm_enc;    /* Hamming(12,8) encode payload */
  uint32_t cyc_manc_enc;    /* Manchester encode DATA frame */
  uint32_t cyc_tx;          /* user TX function */
  uint32_t cyc_wait_ack;    /* time spent waiting/polling for ACK/NACK */
  uint32_t cyc_ack_mdec;    /* Manchester decode of ACK/NACK */
  uint32_t cyc_ack_hdr;     /* frame_parse_header on decoded ACK/NACK */
} ll_prof_t;

/* Returns 1 on ACK, 0 on failure; fills prof if non-NULL */
int ll_send_with_ack_prof(const uint8_t *payload, uint16_t payload_len,
                          uint8_t *seq_io,
                          ll_tx_fn tx, ll_rx_poll_fn rx_poll,
                          uint32_t ack_timeout_ms, int max_retries,
                          size_t *out_len, ll_prof_t *prof);

/* Loopback channel used for demos (produces ACK/NACK) */
int  ll_loopback_tx(const uint8_t *bytes, size_t len);
int  ll_loopback_rx(uint8_t *buf, size_t cap);
void ll_loopback_set_nack_period(int period);
