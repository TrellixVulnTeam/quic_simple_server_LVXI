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

#ifndef LIBGAV1_SRC_DSP_CDEF_H_
#define LIBGAV1_SRC_DSP_CDEF_H_

// Pull in LIBGAV1_DspXXX defines representing the implementation status
// of each function. The resulting value of each can be used by each module to
// determine whether an implementation is needed at compile time.
// IWYU pragma: begin_exports

// ARM:
#include "src/dsp/arm/cdef_neon.h"

// IWYU pragma: end_exports

namespace libgav1 {
namespace dsp {

// Initializes Dsp::cdef_direction and Dsp::cdef_filter. This function is not
// thread-safe.
void CdefInit_C();

}  // namespace dsp
}  // namespace libgav1

#endif  // LIBGAV1_SRC_DSP_CDEF_H_
