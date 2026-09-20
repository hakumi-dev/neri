#ifndef NERI_IR_TRANSPORT_H
#define NERI_IR_TRANSPORT_H

#include <stdint.h>
#include "neri/ir_domains.h"
#include "neri/ir_protocol.h"

/* Canonical Neri IR transport v1 constants. Never parse the header by casting a C struct. */
#define NERI_IR_HEADER_SIZE_V1 UINT32_C(56)
#define NERI_IR_DIGEST_SIZE_V1 UINT32_C(32)
#define NERI_IR_VERSION_MAJOR_OFFSET_V1 UINT32_C(8)
#define NERI_IR_VERSION_MINOR_OFFSET_V1 UINT32_C(10)
#define NERI_IR_FLAGS_OFFSET_V1 UINT32_C(12)
#define NERI_IR_PAYLOAD_LENGTH_OFFSET_V1 UINT32_C(16)
#define NERI_IR_DIGEST_OFFSET_V1 UINT32_C(24)

static const uint8_t NERI_IR_MAGIC_V1[8] = {
    UINT8_C(0x4e), UINT8_C(0x45), UINT8_C(0x52), UINT8_C(0x49),
    UINT8_C(0x52), UINT8_C(0x0d), UINT8_C(0x0a), UINT8_C(0x1a),
};

#include "neri/abi_catalog.h"

#endif
