/*
 * Copyright 2019 The libgav1 Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBGAV1_SRC_DSP_ARM_MASK_BLEND_NEON_H_
#define LIBGAV1_SRC_DSP_ARM_MASK_BLEND_NEON_H_

#include "src/dsp/dsp.h"
#include "src/utils/cpu.h"

namespace libgav1 {
namespace dsp {

// Initializes Dsp::mask_blend. This function is not thread-safe.
void MaskBlendInit_NEON();

}  // namespace dsp
}  // namespace libgav1

#if LIBGAV1_ENABLE_NEON
#define LIBGAV1_Dsp8bpp_MaskBlend444 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_MaskBlend422 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_MaskBlend420 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_MaskBlendInterIntra444 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_MaskBlendInterIntra422 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_MaskBlendInterIntra420 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_InterIntraMaskBlend8bpp444 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_InterIntraMaskBlend8bpp422 LIBGAV1_CPU_NEON
#define LIBGAV1_Dsp8bpp_InterIntraMaskBlend8bpp420 LIBGAV1_CPU_NEON
#endif  // LIBGAV1_ENABLE_NEON

#endif  // LIBGAV1_SRC_DSP_ARM_MASK_BLEND_NEON_H_
