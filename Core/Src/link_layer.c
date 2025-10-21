#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "link_layer.h"

/* ================= Hamming(12,8) ================= */
void hamming128_encode(uint8_t d, uint16_t *cw12)
{
  uint16_t c = 0;
  const int pos[8] = {3,5,6,7,9,10,11,12};
  for (int i=0;i<8;i++){
    int bit = (d >> (7-i)) & 1;
    c |= (uint16_t)bit << (pos[i]-1);
  }
  int p1=0,p2=0,p4=0,p8=0;
  for (int k=1;k<=12;k++){
    if (k==1||k==2||k==4||k==8) continue;
    int bit = (c >> (k-1)) & 1;
    if (k & 1)  p1 ^= bit;
    if (k & 2)  p2 ^= bit;
    if (k & 4)  p4 ^= bit;
    if (k & 8)  p8 ^= bit;
  }
  if (p1) c |= 1u << 0;
  if (p2) c |= 1u << 1;
  if (p4) c |= 1u << 3;
  if (p8) c |= 1u << 7;
  *cw12 = c;
}

uint8_t hamming128_decode(uint16_t c, int *corrected)
{
  int p1=0,p2=0,p4=0,p8=0;
  for (int k=1;k<=12;k++){
    int bit = (c >> (k-1)) & 1;
    if (k & 1)  p1 ^= bit;
    if (k & 2)  p2 ^= bit;
    if (k & 4)  p4 ^= bit;
    if (k & 8)  p8 ^= bit;
  }
  int syndrome = (p8<<3)|(p4<<2)|(p2<<1)|p1;
  if (corrected) *corrected = 0;
  if (syndrome >= 1 && syndrome <= 12) {
    c ^= 1u << (syndrome-1);
    if (corrected) *corrected = 1;
  }
  const int pos[8] = {3,5,6,7,9,10,11,12};
  uint8_t d=0;
  for (int i=0;i<8;i++){
    int bit = (c >> (pos[i]-1)) & 1;
    d |= (uint8_t)bit << (7-i);
  }
  return d;
}

void hamming128_encode_buf(const uint8_t *in, size_t n, uint8_t *out)
{
  for (size_t i=0;i<n;i++){
    uint16_t cw; hamming128_encode(in[i], &cw);
    out[2*i+0] = (uint8_t)(cw & 0xFF);
    out[2*i+1] = (uint8_t)((cw >> 8) & 0x0F);
  }
}

int hamming128_decode_buf(const uint8_t *in, size_t n,
                          uint8_t *out, int *corrected_total, int *uncorr_total)
{
  int corr=0, unc=0;
  for (size_t i=0;i<n;i++){
    uint16_t cw = (uint16_t)in[2*i+0] | ((uint16_t)(in[2*i+1] & 0x0F) << 8);
    int p1=0,p2=0,p4=0,p8=0;
    for (int k=1;k<=12;k++){
      int bit = (cw >> (k-1)) & 1;
      if (k & 1)  p1 ^= bit;
      if (k & 2)  p2 ^= bit;
      if (k & 4)  p4 ^= bit;
      if (k & 8)  p8 ^= bit;
    }
    int synd = (p8<<3)|(p4<<2)|(p2<<1)|p1;
    uint16_t c = cw;
    if (synd){
      if (synd>=1 && synd<=12){ c ^= 1u<<(synd-1); corr++; }
      else { unc++; }
    }
    const int pos[8]={3,5,6,7,9,10,11,12}; uint8_t d=0;
    for (int b=0;b<8;b++){
      int bit = (c >> (pos[b]-1)) & 1; d |= (uint8_t)bit << (7-b);
    }
    out[i] = d;
  }
  if (corrected_total) *corrected_total = corr;
  if (uncorr_total)    *uncorr_total    = unc;
  return (unc==0) ? (int)n : -1;
}

/* ============= Manchester coding =============== */
static inline void put_bit(uint8_t *buf, size_t bit_idx, int bit)
{
  size_t byte = bit_idx >> 3;
  int    off  = 7 - (bit_idx & 7);
  if (bit) buf[byte] |=  (1u << off);
  else     buf[byte] &= ~(1u << off);
}
static inline int get_bit(const uint8_t *buf, size_t bit_idx)
{
  size_t byte = bit_idx >> 3;
  int    off  = 7 - (bit_idx & 7);
  return (buf[byte] >> off) & 1;
}

size_t manchester_encode_bits(const uint8_t *in_bytes, size_t in_bits_len,
                              uint8_t *out_bytes, size_t out_cap)
{
  size_t out_bits = in_bits_len * 2;
  size_t out_bytes_need = (out_bits + 7)/8;
  if (out_bytes_need > out_cap) return 0;
  memset(out_bytes, 0, out_bytes_need);
  size_t out_bit_idx = 0;
  for (size_t b=0; b<in_bits_len; ++b){
    size_t in_byte = b >> 3;
    int    in_off  = 7 - (b & 7);
    int bit = (in_bytes[in_byte] >> in_off) & 1;
    /* 0 -> 01 ; 1 -> 10 */
    put_bit(out_bytes, out_bit_idx++, bit ? 1:0);
    put_bit(out_bytes, out_bit_idx++, bit ? 0:1);
  }
  return out_bytes_need;
}

size_t manchester_encode_bytes(const uint8_t *in, size_t in_len,
                               uint8_t *out, size_t out_cap)
{
  return manchester_encode_bits(in, in_len*8, out, out_cap);
}

size_t manchester_decode_bits(const uint8_t *in_bytes, size_t in_bits_len,
                              uint8_t *out_bytes, size_t out_cap,
                              int *violations)
{
  if (in_bits_len & 1) return 0;
  size_t out_bits = in_bits_len / 2;
  size_t out_bytes_need = (out_bits + 7)/8;
  if (out_bytes_need > out_cap) return 0;
  memset(out_bytes, 0, out_bytes_need);
  int vio = 0;
  size_t out_bit_idx = 0;
  for (size_t i=0; i<in_bits_len; i+=2){
    int a = get_bit(in_bytes, i+0);
    int b = get_bit(in_bytes, i+1);
    int bit;
    if (a==0 && b==1) bit = 0;        /* 01 -> 0 */
    else if (a==1 && b==0) bit = 1;   /* 10 -> 1 */
    else { bit = 0; vio++; }
    size_t ob = out_bit_idx >> 3;
    int    oo = 7 - (out_bit_idx & 7);
    if (bit) out_bytes[ob] |= (1u<<oo);
    out_bit_idx++;
  }
  if (violations) *violations = vio;
  return out_bytes_need;
}

size_t manchester_decode_bytes(const uint8_t *in, size_t in_len,
                               uint8_t *out, size_t out_cap,
                               int *violations)
{
  return manchester_decode_bits(in, in_len*8, out, out_cap, violations);
}

/* ================== Framing (no CRC) ===================== */
size_t frame_build(uint8_t type, uint8_t seq,
                   const uint8_t *payload, uint16_t payload_len,
                   uint8_t *out, size_t out_cap)
{
  size_t need = 2 + 1 + 1 + 2 + payload_len;
  if (need > out_cap) return 0;
  size_t i=0;
  out[i++] = 0xA5; out[i++] = 0x5A;
  out[i++] = type;
  out[i++] = seq;
  out[i++] = (uint8_t)(payload_len & 0xFF);
  out[i++] = (uint8_t)((payload_len >> 8) & 0xFF);
  if (payload_len && payload) { memcpy(&out[i], payload, payload_len); i += payload_len; }
  return i;
}

size_t frame_build_ack (uint8_t seq, uint8_t *out, size_t out_cap)
{ return frame_build(FT_ACK,  seq, NULL, 0, out, out_cap); }
size_t frame_build_nack(uint8_t seq, uint8_t *out, size_t out_cap)
{ return frame_build(FT_NACK, seq, NULL, 0, out, out_cap); }

int frame_parse_header(const uint8_t *frm, size_t frlen,
                       uint8_t *type, uint8_t *seq, uint16_t *paylen)
{
  if (frlen < 6) return 0;
  if (frm[0]!=0xA5 || frm[1]!=0x5A) return 0;
  if (type)   *type   = frm[2];
  if (seq)    *seq    = frm[3];
  if (paylen) *paylen = (uint16_t)frm[4] | ((uint16_t)frm[5]<<8);
  return 1;
}

int frame_find_preamble(const uint8_t *buf, size_t len)
{
  if (len < 2) return -1;
  for (size_t i=0;i+1<len;i++){
    if (buf[i]==0xA5 && buf[i+1]==0x5A) return (int)i;
  }
  return -1;
}

/* ================== Timing (DWT) ====================== */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
  #define DEMCR              (*((volatile uint32_t*)0xE000EDFC))
  #define DEMCR_TRCENA       (1u<<24)
  #define DWT_CTRL           (*((volatile uint32_t*)0xE0001000))
  #define DWT_CYCCNT         (*((volatile uint32_t*)0xE0001004))
  #define DWT_CTRL_CYCCNTENA (1u<<0)
#endif
void     timing_init(void){ DEMCR |= DEMCR_TRCENA; DWT_CYCCNT = 0; DWT_CTRL |= DWT_CTRL_CYCCNTENA; }
void     timing_reset(void){ DWT_CYCCNT = 0; }
uint32_t timing_cycles(void){ return DWT_CYCCNT; }

/* ================= Loopback channel (ACK/NACK) ================= */
static uint8_t  g_chan_tx[512];
static size_t   g_chan_tx_len = 0;
static int      g_nack_period = 0;
static uint32_t g_data_count  = 0;

void ll_loopback_set_nack_period(int period){ g_nack_period = period; }

int ll_loopback_tx(const uint8_t *bytes, size_t len)
{
  if (len > sizeof g_chan_tx) len = sizeof g_chan_tx;
  memcpy(g_chan_tx, bytes, len);
  g_chan_tx_len = len;
  return (int)len;
}

int ll_loopback_rx(uint8_t *buf, size_t cap)
{
  if (!g_chan_tx_len) return 0;

  /* Decode Manchester DATA frame */
  uint8_t dec[512]; int vio=0;
  size_t dec_len = manchester_decode_bytes(g_chan_tx, g_chan_tx_len, dec, sizeof dec, &vio);
  g_chan_tx_len = 0; /* consumed */
  if (!dec_len) return 0;

  int idx = frame_find_preamble(dec, dec_len);
  if (idx < 0 || (size_t)(idx+6) > dec_len) return 0;

  uint8_t type=0, seq=0; uint16_t pay=0;
  if (!frame_parse_header(&dec[idx], dec_len - (size_t)idx, &type, &seq, &pay)) return 0;
  if (type != FT_DATA) return 0;

  /* Ignore decoded payload in demo; respond with ACK/NACK based on policy */
  int nack_now = (g_nack_period>0 && ((++g_data_count) % g_nack_period)==0);
  (void)pay; (void)vio;

  uint8_t resp[16];
  size_t  rlen = nack_now ? frame_build_nack(seq, resp, sizeof resp)
                          : frame_build_ack (seq, resp, sizeof resp);
  uint8_t manc[64];
  size_t ml = manchester_encode_bytes(resp, rlen, manc, sizeof manc);
  if (!ml || ml > cap) return 0;
  memcpy(buf, manc, ml);
  return (int)ml;
}

/* ============ Stop-and-Wait ARQ send (classic) =========== */
extern uint32_t HAL_GetTick(void);

int ll_send_with_ack(const uint8_t *payload, uint16_t payload_len,
                     uint8_t *seq_io,
                     ll_tx_fn tx, ll_rx_poll_fn rx_poll,
                     uint32_t ack_timeout_ms, int max_retries,
                     size_t *out_len)
{
  return ll_send_with_ack_prof(payload, payload_len, seq_io,
                               tx, rx_poll, ack_timeout_ms, max_retries,
                               out_len, NULL);
}

/* ============ Stop-and-Wait ARQ send (profiled) =========== */
int ll_send_with_ack_prof(const uint8_t *payload, uint16_t payload_len,
                          uint8_t *seq_io,
                          ll_tx_fn tx, ll_rx_poll_fn rx_poll,
                          uint32_t ack_timeout_ms, int max_retries,
                          size_t *out_len, ll_prof_t *prof)
{
  #define FRM_MAX   (2+1+1+2 + 512)
  #define HAM_MAX   (512*2)
  #define TX_MAX    (6 + HAM_MAX)
  #define MANC_MAX  ( (TX_MAX) * 2 )

  uint8_t frm[FRM_MAX];
  uint8_t ham[HAM_MAX];
  uint8_t txbuf[TX_MAX];
  uint8_t manc[MANC_MAX];

  if (payload_len > 512) payload_len = 512;

  uint8_t seq = *seq_io;
  int attempt = 0;

  if (prof) memset(prof, 0, sizeof *prof);
  uint32_t t_total0 = timing_cycles();

  for (;;){
    uint32_t t_f0 = timing_cycles();
    size_t frlen = frame_build(FT_DATA, seq, payload, payload_len, frm, sizeof frm);
    if (!frlen) return 0;
    uint32_t t_f1 = timing_cycles();

    uint16_t paylen = (uint16_t)frm[4] | ((uint16_t)frm[5] << 8);

    uint32_t t_h0 = timing_cycles();
    hamming128_encode_buf(&frm[6], paylen, ham);
    uint32_t t_h1 = timing_cycles();

    size_t tx_len = 0;
    memcpy(&txbuf[0], &frm[0], 6); tx_len = 6;
    memcpy(&txbuf[tx_len], ham, 2*paylen); tx_len += 2*paylen;

    uint32_t t_m0 = timing_cycles();
    size_t manc_len = manchester_encode_bytes(txbuf, tx_len, manc, sizeof manc);
    uint32_t t_m1 = timing_cycles();
    if (!manc_len) return 0;

    uint32_t t_tx0 = timing_cycles();
    if (tx(manc, manc_len) <= 0) return 0;
    uint32_t t_tx1 = timing_cycles();
    if (out_len) *out_len = manc_len;

    uint32_t t_wait0 = timing_cycles();
    uint32_t t_ack_mdec = 0, t_ack_hdr = 0;
    uint32_t t_ack_m0=0, t_ack_m1=0, t_ack_h0=0, t_ack_h1=0;

    uint32_t tick0 = HAL_GetTick();
    for (;;){
      uint8_t rbuf[32];
      int got = rx_poll(rbuf, sizeof rbuf);
      if (got > 0){
        uint8_t dec[32]; int vio=0;
        t_ack_m0 = timing_cycles();
        size_t decr = manchester_decode_bytes(rbuf, (size_t)got, dec, sizeof dec, &vio);
        t_ack_m1 = timing_cycles();
        if (decr >= 6){
          uint8_t rtype=0, rseq=0; uint16_t rpay=0;
          t_ack_h0 = timing_cycles();
          int ok_hdr = frame_parse_header(dec, decr, &rtype, &rseq, &rpay);
          t_ack_h1 = timing_cycles();
          if (ok_hdr){
            t_ack_mdec += (t_ack_m1 - t_ack_m0);
            t_ack_hdr  += (t_ack_h1 - t_ack_h0);
            if ((rtype==FT_ACK || rtype==FT_NACK) && rseq==seq){
              if (rtype==FT_ACK){
                if (prof){
                  prof->cyc_frame     += (t_f1 - t_f0);
                  prof->cyc_hamm_enc  += (t_h1 - t_h0);
                  prof->cyc_manc_enc  += (t_m1 - t_m0);
                  prof->cyc_tx        += (t_tx1 - t_tx0);
                  prof->cyc_wait_ack  += (timing_cycles() - t_wait0);
                  prof->cyc_ack_mdec  += t_ack_mdec;
                  prof->cyc_ack_hdr   += t_ack_hdr;
                  prof->cyc_total      =  (timing_cycles() - t_total0);
                }
                *seq_io = (uint8_t)(seq + 1);
                return 1;
              } else {
                /* NACK: retry */
                break;
              }
            }
          }
        }
      }
      if ((HAL_GetTick() - tick0) > ack_timeout_ms) break; /* timeout: retry */
    }

    if (attempt++ >= max_retries) {
      if (prof){
        prof->cyc_frame    += (t_f1 - t_f0);
        prof->cyc_hamm_enc += (t_h1 - t_h0);
        prof->cyc_manc_enc += (t_m1 - t_m0);
        prof->cyc_tx       += (t_tx1 - t_tx0);
        prof->cyc_wait_ack += (timing_cycles() - t_wait0);
        prof->cyc_ack_mdec += t_ack_mdec;
        prof->cyc_ack_hdr  += t_ack_hdr;
        prof->cyc_total     = (timing_cycles() - t_total0);
      }
      return 0; /* give up */
    }
    /* else loop to retry; stats accumulate across attempts */
  }
}
