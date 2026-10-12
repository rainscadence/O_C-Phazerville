/* Compact PHZ request/reply codec shared by the follower and host tests. */
#ifndef PHZ_COMPACT_H
#define PHZ_COMPACT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
enum { PC_READ, PC_ADMITTED, PC_ARGUMENT, PC_WRONG_APP, PC_UNAVAILABLE,
       PC_BUSY, PC_FULL, PC_UNSUPPORTED };
enum { PC_FAMILY_COMMON, PC_FAMILY_QUADRANTS, PC_FAMILY_MIDI,
       PC_FAMILY_TXO, PC_FAMILY_TXI, PC_FAMILY_QUANTIZER };
typedef struct {
    uint8_t family, op, mapping;
    uint16_t tag, value;
} pc_request;
int pc_decode(const uint8_t *b, size_t n, pc_request *out);
int pc_encode(const pc_request *r, uint8_t *b, size_t capacity, size_t *written);
/* Matching an error reply is not success. Outputs untouched on rejection. */
int pc_match(const pc_request *r, const uint8_t *b, size_t n,
             uint8_t *status, uint16_t *value);
enum { PC_QTZ_SCALE = 1, PC_QTZ_ROOT, PC_QTZ_OCTAVE, PC_QTZ_MASK,
       PC_QTZ_RANGE, PC_QTZ_RANGE_OFF, PC_QTZ_REPLACE,
       PC_QTZ_CAPTURE_CHORD };
typedef struct {
    uint8_t op, slot, root, range_enabled, source_chord;
    uint16_t tag, scale, mask;
    int8_t octave, low, high;
} pc_qtz_request;
int pc_qtz_decode(const uint8_t *b, size_t n, pc_qtz_request *out);
int pc_qtz_encode(const pc_qtz_request *r, uint8_t *b, size_t capacity,
                  size_t *written);
int pc_qtz_match(const pc_qtz_request *r, const uint8_t *b, size_t n,
                 uint8_t *status);
typedef struct {
    pc_request request;
    uint32_t epoch, sample_epoch;
    uint16_t data, sample_tag;
    uint8_t status, pending, valid, sample_valid, sample_mapping;
} pc_response;
void pc_clear(pc_response *s);
/* Caller serializes select/take/clear and lifecycle changes under one lock.
 * status/data/valid come from an already validated snapshot or queue admission.
 * For VALID these are calculated here, never supplied by a resampling caller.
 * Call even on malformed receives to invalidate previous response/sample. */
int pc_select(pc_response *s, const uint8_t *b, size_t n, uint32_t epoch,
              uint8_t status, uint16_t data, int valid);
/* Bounded buffer-copy callback, not a bus wait. Must copy all 9 bytes or fail.
 * True means copied, NOT received by the leader. No concurrent lifecycle change
 * is allowed during this call. Every attempt consumes the selected response. */
typedef int (*pc_copy)(void *, const uint8_t *, size_t);
int pc_take(pc_response *s, uint32_t epoch, pc_copy copy, void *context);
#ifdef __cplusplus
}
#endif
#endif
