#include "compact.h"
#include <string.h>
static uint16_t word(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] << 8 | b[1]);
}
static void put_word(uint8_t *b, uint16_t value) {
    b[0]=(uint8_t)(value >> 8); b[1]=(uint8_t)value;
}
static void raw_header(uint8_t family, uint8_t op, uint16_t tag,
                       uint8_t *b, uint8_t marker) {
    b[0]=marker; b[1]=1; b[2]=family; b[3]=op;
    put_word(b+4,tag);
}
static size_t length(const pc_request *r) {
    if (!r || !r->tag) return 0;
    if (!r->family) return r->op == 1 && !r->mapping && !r->value ? 6 : 0;
    if (r->family != 1 || r->mapping < 1 || r->mapping > 32) return 0;
    if (r->op == 1 || (r->op == 2 && r->value <= 16384)) return 9;
    return (r->op == 3 || r->op == 4) && !r->value ? 7 : 0;
}
int pc_decode(const uint8_t *b, size_t n, pc_request *out) {
    pc_request r = {0};
    if (!b || !out || n < 6 || n > 9 || b[0] != 0x50 || b[1] != 1) return 0;
    r.family=b[2]; r.op=b[3]; r.tag=word(b+4);
    if (n >= 7) r.mapping=b[6];
    if (n == 9) r.value=word(b+7);
    if (length(&r) != n) return 0;
    *out=r; return 1;
}
static void header(const pc_request *r, uint8_t *b, uint8_t marker) {
    raw_header(r->family,r->op,r->tag,b,marker);
}
static int qtz_range(int low, int high) {
    return low >= -8 && low <= 8 && high >= -8 && high <= 8 && low <= high;
}
static size_t qtz_length(const pc_qtz_request *r) {
    if (!r || !r->tag || r->slot < 1 || r->slot > 8) return 0;
    switch (r->op) {
    case PC_QTZ_SCALE: return r->scale <= INT16_MAX ? 9 : 0;
    case PC_QTZ_ROOT: return r->root <= 11 ? 9 : 0;
    case PC_QTZ_OCTAVE:
        return r->octave >= -8 && r->octave <= 8 ? 9 : 0;
    case PC_QTZ_MASK: return r->mask ? 9 : 0;
    case PC_QTZ_RANGE: return qtz_range(r->low,r->high) ? 9 : 0;
    case PC_QTZ_RANGE_OFF: return 7;
    case PC_QTZ_REPLACE:
        if (r->scale > INT16_MAX || r->root > 11 || r->octave < -8 ||
            r->octave > 8 || !r->mask || r->range_enabled > 1) return 0;
        if (r->range_enabled ? !qtz_range(r->low,r->high)
                             : (r->low != 1 || r->high != 0)) return 0;
        return 16;
    case PC_QTZ_CAPTURE_CHORD:
        return r->source_chord >= 1 && r->source_chord <= 8 ? 9 : 0;
    default: return 0;
    }
}
int pc_qtz_decode(const uint8_t *b, size_t n, pc_qtz_request *out) {
    pc_qtz_request r={0};
    if (!b || !out || n < 7 || n > 16 || b[0] != 0x50 || b[1] != 1 ||
        b[2] != PC_FAMILY_QUANTIZER) return 0;
    r.op=b[3]; r.tag=word(b+4); r.slot=b[6];
    switch (r.op) {
    case PC_QTZ_SCALE:
        if (n != 9) return 0;
        r.scale=word(b+7); break;
    case PC_QTZ_ROOT:
        if (n != 9 || b[8] != 0) return 0;
        r.root=b[7]; break;
    case PC_QTZ_OCTAVE:
        if (n != 9 || b[8] != 0) return 0;
        r.octave=(int8_t)b[7]; break;
    case PC_QTZ_MASK:
        if (n != 9) return 0;
        r.mask=word(b+7); break;
    case PC_QTZ_RANGE:
        if (n != 9) return 0;
        r.low=(int8_t)b[7]; r.high=(int8_t)b[8]; break;
    case PC_QTZ_RANGE_OFF:
        if (n != 7) return 0;
        r.low=1; r.high=0; break;
    case PC_QTZ_REPLACE:
        if (n != 16) return 0;
        r.scale=word(b+7); r.root=b[9]; r.octave=(int8_t)b[10];
        r.mask=word(b+11); r.range_enabled=b[13];
        r.low=(int8_t)b[14]; r.high=(int8_t)b[15]; break;
    case PC_QTZ_CAPTURE_CHORD:
        if (n != 9 || b[8] != 0) return 0;
        r.source_chord=b[7]; break;
    default: return 0;
    }
    if (qtz_length(&r) != n) return 0;
    *out=r; return 1;
}
int pc_qtz_encode(const pc_qtz_request *r, uint8_t *b, size_t cap,
                  size_t *written) {
    size_t n=qtz_length(r);
    uint8_t scratch[16]={0};
    if (!n || !b || !written || cap < n) return 0;
    raw_header(PC_FAMILY_QUANTIZER,r->op,r->tag,scratch,0x50);
    scratch[6]=r->slot;
    switch (r->op) {
    case PC_QTZ_SCALE: put_word(scratch+7,r->scale); break;
    case PC_QTZ_ROOT: scratch[7]=r->root; break;
    case PC_QTZ_OCTAVE: scratch[7]=(uint8_t)r->octave; break;
    case PC_QTZ_MASK: put_word(scratch+7,r->mask); break;
    case PC_QTZ_RANGE:
        scratch[7]=(uint8_t)r->low; scratch[8]=(uint8_t)r->high; break;
    case PC_QTZ_RANGE_OFF: break;
    case PC_QTZ_REPLACE:
        put_word(scratch+7,r->scale); scratch[9]=r->root;
        scratch[10]=(uint8_t)r->octave; put_word(scratch+11,r->mask);
        scratch[13]=r->range_enabled; scratch[14]=(uint8_t)r->low;
        scratch[15]=(uint8_t)r->high; break;
    case PC_QTZ_CAPTURE_CHORD: scratch[7]=r->source_chord; break;
    default: return 0;
    }
    memcpy(b,scratch,n); *written=n; return 1;
}
int pc_qtz_match(const pc_qtz_request *r, const uint8_t *b, size_t n,
                 uint8_t *status) {
    uint8_t h[6]; uint16_t value;
    if (!qtz_length(r) || !b || n != 9 || !status) return 0;
    raw_header(PC_FAMILY_QUANTIZER,r->op,r->tag,h,0x52);
    if (memcmp(h,b,6)) return 0;
    value=word(b+7);
    if (b[6] > PC_UNSUPPORTED || value ||
        (b[6] != PC_ADMITTED && b[6] < PC_ARGUMENT)) return 0;
    *status=b[6]; return 1;
}
int pc_encode(const pc_request *r, uint8_t *b, size_t cap, size_t *written) {
    size_t n=length(r);
    uint8_t scratch[9]={0};
    if (!n || !b || !written || cap < n) return 0;
    header(r,scratch,0x50);
    if (n >= 7) scratch[6]=r->mapping;
    if (n == 9) { scratch[7]=(uint8_t)(r->value >> 8); scratch[8]=(uint8_t)r->value; }
    memcpy(b,scratch,n); *written=n; return 1;
}
static int result_valid(const pc_request *r, uint8_t status, uint16_t value) {
    int write=r->family == 1 && r->op <= 2;
    if (status > PC_UNSUPPORTED) return 0;
    if (status >= PC_ARGUMENT) return value == 0;
    if (write) return status == PC_ADMITTED && value == 0;
    if (status != PC_READ) return 0;
    if (!r->family) return (value >> 8) <= 2 && (value & 255) <= 3;
    return r->op != 4 || value <= 1;
}
int pc_match(const pc_request *r, const uint8_t *b, size_t n,
             uint8_t *status, uint16_t *value) {
    uint8_t h[6]; uint16_t v;
    if (!length(r) || !b || n != 9 || !status || !value) return 0;
    header(r,h,0x52);
    if (memcmp(h,b,6)) return 0;
    v=word(b+7);
    if (!result_valid(r,b[6],v)) return 0;
    *status=b[6]; *value=v; return 1;
}
void pc_clear(pc_response *s) { memset(s,0,sizeof *s); }
int pc_select(pc_response *s, const uint8_t *b, size_t n, uint32_t epoch,
              uint8_t status, uint16_t data, int valid) {
    pc_response old=*s;
    pc_request r;
    pc_clear(s);
    if (!epoch || !pc_decode(b,n,&r)) return 0;
    if (r.family == 1 && r.op == 4 && status == PC_READ) {
        data=old.sample_valid && old.sample_tag == r.tag &&
             old.sample_mapping == r.mapping && old.sample_epoch == epoch;
    }
    if (!result_valid(&r,status,data)) return 0;
    s->request=r; s->epoch=epoch; s->status=status; s->data=data;
    s->valid=!!valid; s->pending=1; return 1;
}
int pc_take(pc_response *s, uint32_t epoch, pc_copy copy, void *context) {
    pc_response old=*s;
    uint8_t b[9];
    pc_clear(s);
    if (!old.pending || !epoch || old.epoch != epoch || !copy) return 0;
    header(&old.request,b,0x52); b[6]=old.status;
    b[7]=(uint8_t)(old.data >> 8); b[8]=(uint8_t)old.data;
    if (!copy(context,b,sizeof b)) return 0;
    if (old.request.family == 1 && old.request.op == 3 && old.status == PC_READ) {
        s->sample_tag=old.request.tag; s->sample_mapping=old.request.mapping;
        s->sample_epoch=epoch; s->sample_valid=old.valid;
    }
    return 1;
}
