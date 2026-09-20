/* SPDX-License-Identifier: Apache-2.0
 * Fixed MoveNet v4 + RGB192 acceptance oracle, not inference output caching.
 * Reference transcript SHA256: fb1120133ebff09f1c88fb5484d93ac217ee71a3fb78190771c12c0fbc4b95e2
 * Model/fixture identities: docs/VELAFIT_REAL_POSE_ACCEPTANCE_2026-09-13.md
 */
#pragma once
#include <stdint.h>
static const uint32_t vf_pose_oracle[17][3] = {
  {0x3f11c84fu, 0x3eb0328fu, 0x3f4d9052u},
  {0x3f11c84fu, 0x3ea39caau, 0x3f6d0710u},
  {0x3f0a70deu, 0x3e9f6ab2u, 0x3f11c84fu},
  {0x3f0c89dau, 0x3ea183aeu, 0x3f22902cu},
  {0x3efdceedu, 0x3e9f6ab2u, 0x3f40fa6cu},
  {0x3f0ea2d5u, 0x3ed80d3cu, 0x3f4d9052u},
  {0x3ee28a26u, 0x3ecd9052u, 0x3f580d3cu},
  {0x3f29e79cu, 0x3f020cf0u, 0x3f617da8u},
  {0x3f03196eu, 0x3f020cf0u, 0x3f580d3cu},
  {0x3f25b5a5u, 0x3ed5f440u, 0x3edc3f33u},
  {0x3f1a2c3du, 0x3ed3db45u, 0x3f67c89bu},
  {0x3ec7455fu, 0x3f1e5e35u, 0x3f67c89bu},
  {0x3e991fbfu, 0x3f1a2c3du, 0x3f6d0710u},
  {0x3f12d4cdu, 0x3f24a927u, 0x3f580d3cu},
  {0x3ede582fu, 0x3f2e1994u, 0x3f580d3cu},
  {0x3f020cf0u, 0x3f5919bau, 0x3f11c84fu},
  {0x3ebaaf79u, 0x3f66bc1du, 0x3f6d0710u},
};
