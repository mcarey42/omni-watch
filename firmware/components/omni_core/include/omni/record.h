#pragma once
/* The versioned, append-only NVS records (spec §5): watch/shared and t_<id>/local. Every record starts with
 * uint16 version | uint16 size (little-endian; size is the whole record, header included). Fields are only appended:
 * a reader takes the prefix it knows, a writer keeps any tail it does not understand. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OMNI_REC_HDR 4          /* uint16 version | uint16 size, little-endian */
#define OMNI_REC_MAX 256        /* a whole record's cap (the namespace cap is 512 B) */

typedef enum {
    OMNI_REC_LOADED,            /* the known prefix, nothing extra */
    OMNI_REC_TAIL_KEPT,         /* a newer writer's extra bytes are held in tail[] and re-encoded verbatim */
    OMNI_REC_CITY_UNKNOWN,      /* shared only: the city is not in this table: the default city, the rest kept */
    OMNI_REC_UNKNOWN_VERSION,   /* the defaults */
    OMNI_REC_UNUSABLE           /* too short, a lying size, over the cap: the defaults */
} omni_rec_load_t;

/* false: n < 4, size != n, or size > OMNI_REC_MAX */
bool omni_rec_header(const uint8_t *buf, size_t n, uint16_t *version, uint16_t *size);
