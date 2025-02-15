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

#ifndef LIBGAV1_SRC_GAV1_DECODER_H_
#define LIBGAV1_SRC_GAV1_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <memory>

// IWYU pragma: begin_exports
#include "gav1/decoder_buffer.h"
#include "gav1/decoder_settings.h"
#include "gav1/frame_buffer.h"
#include "gav1/status_code.h"
#include "gav1/symbol_visibility.h"
#include "gav1/version.h"
// IWYU pragma: end_exports

namespace libgav1 {

// Forward declaration.
class DecoderImpl;

class LIBGAV1_PUBLIC Decoder {
 public:
  Decoder();
  ~Decoder();

  // Init must be called exactly once per instance. Subsequent calls will do
  // nothing. If |settings| is nullptr, the decoder will be initialized with
  // default settings. Returns kLibgav1StatusOk on success, an error status
  // otherwise.
  StatusCode Init(const DecoderSettings* settings);

  // Enqueues a compressed frame to be decoded. Applications can continue
  // enqueue'ing up to |GetMaxAllowedFrames()|. The decoder can be thought of as
  // a queue of size |GetMaxAllowedFrames()|. Returns kLibgav1StatusOk on
  // success and an error status otherwise. Returning an error status here isn't
  // a fatal error and the decoder can continue decoding further frames.
  //
  // |user_private_data| may be used to associate application specific private
  // data with the compressed frame. It will be copied to the user_private_data
  // field of the DecoderBuffer returned by the corresponding |DequeueFrame()|
  // call.
  //
  // NOTE: |EnqueueFrame()| does not copy the data. Therefore, after a
  // successful |EnqueueFrame()| call, the caller must keep the |data| buffer
  // alive until the corresponding |DequeueFrame()| call returns.
  StatusCode EnqueueFrame(const uint8_t* data, size_t size,
                          int64_t user_private_data);

  // Dequeues a decompressed frame. If there are enqueued compressed frames,
  // decodes one and sets |*out_ptr| to the last displayable frame in the
  // compressed frame. If there are no displayable frames available, sets
  // |*out_ptr| to nullptr. Returns an error status if there is an error.
  StatusCode DequeueFrame(const DecoderBuffer** out_ptr);

  // Signals the end of stream.
  //
  // In non-frame-parallel mode, this function will release all the frames held
  // by the decoder. If the frame buffers were allocated by libgav1, then the
  // pointer obtained by the prior DequeueFrame call will no longer be valid. If
  // the frame buffers were allocated by the application, then any references
  // that libgav1 is holding on to will be released.
  //
  // Once this function returns successfully, the decoder state will be reset
  // and the decoder is ready to start decoding a new coded video sequence.
  StatusCode SignalEOS();

  // Returns the maximum number of frames allowed to be enqueued at a time. The
  // decoder will reject frames beyond this count. If |settings_.frame_parallel|
  // is false, then this function will always return 1.
  int GetMaxAllowedFrames() const;

  // Returns the maximum bitdepth that is supported by this decoder.
  static int GetMaxBitdepth();

 private:
  bool initialized_ = false;
  DecoderSettings settings_;
  std::unique_ptr<DecoderImpl> impl_;
};

}  // namespace libgav1

#endif  // LIBGAV1_SRC_GAV1_DECODER_H_
