/*
 * Copyright (c) 2011 Nicira Networks.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include "autopath.h"

#include <inttypes.h>
#include <stdlib.h>

#include "flow.h"
#include "nx-match.h"
#include "ofp-util.h"
#include "openflow/nicira-ext.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(autopath);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

/* Loads 'ofp_port' into the appropriate register in accordance with the
 * autopath action. */
void
autopath_execute(const struct nx_action_autopath *ap, struct flow *flow,
                 uint16_t ofp_port)
{
    uint32_t *reg = &flow->regs[NXM_NX_REG_IDX(ntohl(ap->dst))];
    int ofs = nxm_decode_ofs(ap->ofs_nbits);
    int n_bits = nxm_decode_n_bits(ap->ofs_nbits);
    uint32_t mask = n_bits == 32 ? UINT32_MAX : (UINT32_C(1) << n_bits) - 1;
    *reg = (*reg & ~(mask << ofs)) | ((ofp_port & mask) << ofs);
}

void
autopath_parse(struct nx_action_autopath *ap, const char *s_)
{
    char *s;
    uint32_t reg;
    int id_int, ofs, n_bits;
    char *id_str, *dst, *save_ptr;

    s = xstrdup(s_);
    save_ptr = NULL;
    id_str = strtok_r(s, ", ", &save_ptr);
    dst = strtok_r(NULL, ", ", &save_ptr);

    if (!dst) {
        ovs_fatal(0, "%s: not enough arguments to autopath action", s_);
    }

    id_int = atoi(id_str);
    if (id_int < 1 || id_int > UINT32_MAX) {
        ovs_fatal(0, "%s: autopath id %d is not in valid range "
                  "1 to %"PRIu32, s_, id_int, UINT32_MAX);
    }

    nxm_parse_field_bits(dst, &reg, &ofs, &n_bits);
    if (!NXM_IS_NX_REG(reg) || NXM_NX_REG_IDX(reg) >= FLOW_N_REGS) {
        ovs_fatal(0, "%s: destination field must be a register", s_);
    }

    if (n_bits < 16) {
        ovs_fatal(0, "%s: %d-bit destination field has %u possible values, "
                  "less than required 65536", s_, n_bits, 1u << n_bits);
    }

    memset(ap, 0, sizeof *ap);
    ap->type = htons(OFPAT_VENDOR);
    ap->len = htons(sizeof *ap);
    ap->vendor = htonl(NX_VENDOR_ID);
    ap->subtype = htons(NXAST_AUTOPATH);
    ap->id = htonl(id_int);
    ap->ofs_nbits = nxm_encode_ofs_nbits(ofs, n_bits);
    ap->dst = htonl(reg);

    free(s);
}

int
autopath_check(const struct nx_action_autopath *ap)
{
    uint32_t dst = ntohl(ap->dst);
    int ofs = nxm_decode_ofs(ap->ofs_nbits);
    int n_bits = nxm_decode_n_bits(ap->ofs_nbits);

    if (!NXM_IS_NX_REG(dst) || NXM_NX_REG_IDX(dst) >= FLOW_N_REGS) {
        VLOG_WARN_RL(&rl, "unsupported destination field %#"PRIx32, dst);
    } else if (ofs + n_bits > nxm_field_bits(dst)) {
        VLOG_WARN_RL(&rl, "destination overflows output field");
    } else if (n_bits < 16) {
        VLOG_WARN_RL(&rl, "minimum of 16 bits required in output field");
    } else {
        return 0;
    }

    return ofp_mkerr(OFPET_BAD_ACTION, OFPBAC_BAD_ARGUMENT);
}
