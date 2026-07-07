/*
 * scan_cs_soak - shared IPT/IFFT distance estimator
 *
 * Used by whichever device is the CS *initiator* (only the initiator computes
 * distance). With Inline PCT Transfer (cs_enhancements_1 = 1 in the CS config) the
 * reflector's phase data rides inline in the initiator's own subevent results, so
 * distance is computed locally with Nordic's cs_de library - no GATT/RAS transfer.
 */

#ifndef SCAN_CS_SOAK_CS_IFFT_H_
#define SCAN_CS_SOAK_CS_IFFT_H_

#include <zephyr/bluetooth/conn.h>

/* Accumulate one CS subevent's tones into the per-channel IQ buffer. Light enough
 * to call from the le_cs_subevent_data_available callback. Returns true when the CS
 * procedure has completed - the caller should then call cs_ifft_compute_print()
 * from THREAD context. */
bool cs_ifft_accumulate(struct bt_conn_le_cs_subevent_result *result);

/* Run the IFFT on the accumulated IQ and print the parser line:
 *   DIST:<metres>,AP:0,SAMPLES:<high-quality-tone-count>
 * Call from THREAD context only - cs_de_ifft() is heavy and must NOT run in the
 * BT RX callback (doing so stalls BT processing and aborts CS procedures). */
void cs_ifft_compute_print(void);

#endif /* SCAN_CS_SOAK_CS_IFFT_H_ */
