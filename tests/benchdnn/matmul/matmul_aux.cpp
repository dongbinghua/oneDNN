/*******************************************************************************
* Copyright 2019-2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oneapi/dnnl/dnnl.h"

#include "dnnl_common.hpp"
#include "dnnl_debug.hpp"

#include "matmul/matmul.hpp"

namespace matmul {

dnnl_data_type_t prb_t::get_dt(data_kind_t data_kind) const {
    switch (data_kind) {
        case SRC: return src_dt();
        case WEI: return wei_dt();
        case BIA: return bia_dt;
        case DST: return dst_dt();
        default: assert(!"unexpected"); return dnnl_data_type_undef;
    }
}

void prb_t::generate_oscales() {
    if (attr.oscale.is_def()) return;

    if (attr.oscale.policy == policy_t::COMMON) {
        scales = (float *)zmalloc(sizeof(float), 4);
        SAFE_V(scales != nullptr ? OK : FAIL);
        scales[0] = attr.oscale.scale;
        return;
    }

    assert(attr.oscale.policy == policy_t::PER_OC);

    scales = (float *)zmalloc(sizeof(float) * n, 64);
    SAFE_V(scales != nullptr ? OK : FAIL);

    const float K = 32;
    /* scale in [1/K .. K], with starting point at oscale.scale */
    float s[2] = {attr.oscale.scale, attr.oscale.scale / 2};
    for (int64_t i = 0; i < n; ++i) {
        int64_t si = i % 2; // 0 -> left, 1 -> right
        scales[i] = s[si];
        if (si == 0) {
            s[si] /= 2.;
            if (s[si] < 1. / K) s[si] *= K * K; // turn around to become ~K
        } else {
            s[si] *= 2.;
            if (s[si] > K) s[si] /= K * K; // turn around to become ~K
        }
    }
}

int32_t *prb_t::generate_zero_points(
        int arg, const attr_t::zero_points_t &zero_points, int N) {
    if (zero_points.is_def(arg)) return nullptr;

    const auto &e = zero_points.get(arg);
    if (e.policy == policy_t::COMMON) {
        int32_t *zp = (int32_t *)zmalloc(sizeof(int32_t), 4);
        SAFE_V(zp != nullptr ? OK : FAIL);
        zp[0] = e.value;
        return zp;
    }

    assert(e.policy == policy_t::PER_DIM_1);

    int32_t *zp = (int32_t *)zmalloc(sizeof(int32_t) * N, 64);
    SAFE_V(zp != nullptr ? OK : FAIL);

    for (int i = 0; i < N; ++i)
        zp[i] = e.value + i % 3;
    return zp;
}

std::ostream &operator<<(std::ostream &s, const prb_t &prb) {
    dump_global_params(s);
    settings_t def;

    bool has_default_dts = true;
    for (const auto &i_dt : prb.dt)
        has_default_dts = has_default_dts && i_dt == dnnl_f32;

    if (canonical || !has_default_dts) s << "--dt=" << prb.dt << " ";
    if (canonical || prb.stag != def.stag[0]) s << "--stag=" << prb.stag << " ";
    if (canonical || prb.wtag != def.wtag[0]) s << "--wtag=" << prb.wtag << " ";
    if (canonical || prb.dtag != def.dtag[0]) s << "--dtag=" << prb.dtag << " ";
    if (canonical || prb.strides != def.strides[0])
        s << "--strides=" << vdims2str(prb.strides) << " ";

    if (canonical || prb.src_runtime_dim_mask().any()
            || prb.weights_runtime_dim_mask().any())
        s << "--runtime_dims_masks=" << prb.src_runtime_dim_mask().to_ulong()
          << ":" << prb.weights_runtime_dim_mask().to_ulong() << " ";

    if (canonical || prb.bia_dt != def.bia_dt[0]) {
        s << "--bia_dt=" << prb.bia_dt << " ";

        if (canonical || prb.bia_mask != def.bia_mask[0])
            s << "--bia_mask=" << prb.bia_mask << " ";
    }

    s << prb.attr;
    if (canonical || prb.ctx_init != def.ctx_init[0])
        s << "--ctx-init=" << prb.ctx_init << " ";
    if (canonical || prb.ctx_exe != def.ctx_exe[0])
        s << "--ctx-exe=" << prb.ctx_exe << " ";

    s << static_cast<const prb_vdims_t &>(prb);

    return s;
}

} // namespace matmul
