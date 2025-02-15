// Copyright 2019 The libgav1 Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/tile.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <numeric>
#include <type_traits>
#include <utility>

#include "src/motion_vector.h"
#include "src/reconstruction.h"
#include "src/utils/bit_mask_set.h"
#include "src/utils/logging.h"
#include "src/utils/segmentation.h"
#include "src/utils/stack.h"

namespace libgav1 {
namespace {

// Import all the constants in the anonymous namespace.
#include "src/quantizer_tables.inc"
#include "src/scan_tables.inc"

// Precision bits when scaling reference frames.
constexpr int kReferenceScaleShift = 14;
// Range above kNumQuantizerBaseLevels which the exponential golomb coding
// process is activated.
constexpr int kQuantizerCoefficientBaseRange = 12;
constexpr int kNumQuantizerBaseLevels = 2;
constexpr int kQuantizerCoefficientBaseRangeContextClamp =
    kQuantizerCoefficientBaseRange + kNumQuantizerBaseLevels + 1;
constexpr int kCoeffBaseRangeMaxIterations =
    kQuantizerCoefficientBaseRange / (kCoeffBaseRangeSymbolCount - 1);
constexpr int kEntropyContextLeft = 0;
constexpr int kEntropyContextTop = 1;

constexpr uint8_t kAllZeroContextsByTopLeft[5][5] = {{1, 2, 2, 2, 3},
                                                     {2, 4, 4, 4, 5},
                                                     {2, 4, 4, 4, 5},
                                                     {2, 4, 4, 4, 5},
                                                     {3, 5, 5, 5, 6}};

// The space complexity of DFS is O(branching_factor * max_depth). For the
// parameter tree, branching_factor = 4 (there could be up to 4 children for
// every node) and max_depth (excluding the root) = 5 (to go from a 128x128
// block all the way to a 4x4 block). The worse-case stack size is 16, by
// counting the number of 'o' nodes in the diagram:
//
//   |                    128x128  The highest level (corresponding to the
//   |                             root of the tree) has no node in the stack.
//   |-----------------+
//   |     |     |     |
//   |     o     o     o  64x64
//   |
//   |-----------------+
//   |     |     |     |
//   |     o     o     o  32x32    Higher levels have three nodes in the stack,
//   |                             because we pop one node off the stack before
//   |-----------------+           pushing its four children onto the stack.
//   |     |     |     |
//   |     o     o     o  16x16
//   |
//   |-----------------+
//   |     |     |     |
//   |     o     o     o  8x8
//   |
//   |-----------------+
//   |     |     |     |
//   o     o     o     o  4x4      Only the lowest level has four nodes in the
//                                 stack.
constexpr int kDfsStackSize = 16;

// Mask indicating whether the transform sets contain a particular transform
// type. If |tx_type| is present in |tx_set|, then the |tx_type|th LSB is set.
constexpr BitMaskSet kTransformTypeInSetMask[kNumTransformSets] = {
    BitMaskSet(0x1),    BitMaskSet(0xE0F), BitMaskSet(0x20F),
    BitMaskSet(0xFFFF), BitMaskSet(0xFFF), BitMaskSet(0x201)};

constexpr PredictionMode
    kFilterIntraModeToIntraPredictor[kNumFilterIntraPredictors] = {
        kPredictionModeDc, kPredictionModeVertical, kPredictionModeHorizontal,
        kPredictionModeD157, kPredictionModeDc};

// This is computed as:
// min(transform_width_log2, 5) + min(transform_height_log2, 5) - 4.
constexpr uint8_t kEobMultiSizeLookup[kNumTransformSizes] = {
    0, 1, 2, 1, 2, 3, 4, 2, 3, 4, 5, 5, 4, 5, 6, 6, 5, 6, 6};

/* clang-format off */
constexpr uint8_t kCoeffBaseContextOffset[kNumTransformSizes][5][5] = {
    {{0, 1, 6, 6, 0}, {1, 6, 6, 21, 0}, {6, 6, 21, 21, 0}, {6, 21, 21, 21, 0},
     {0, 0, 0, 0, 0}},
    {{0, 11, 11, 11, 0}, {11, 11, 11, 11, 0}, {6, 6, 21, 21, 0},
     {6, 21, 21, 21, 0}, {21, 21, 21, 21, 0}},
    {{0, 11, 11, 11, 0}, {11, 11, 11, 11, 0}, {6, 6, 21, 21, 0},
     {6, 21, 21, 21, 0}, {21, 21, 21, 21, 0}},
    {{0, 16, 6, 6, 21}, {16, 16, 6, 21, 21}, {16, 16, 21, 21, 21},
     {16, 16, 21, 21, 21}, {0, 0, 0, 0, 0}},
    {{0, 1, 6, 6, 21}, {1, 6, 6, 21, 21}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 11, 11, 11, 11}, {11, 11, 11, 11, 11}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 11, 11, 11, 11}, {11, 11, 11, 11, 11}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 16, 6, 6, 21}, {16, 16, 6, 21, 21}, {16, 16, 21, 21, 21},
     {16, 16, 21, 21, 21}, {0, 0, 0, 0, 0}},
    {{0, 16, 6, 6, 21}, {16, 16, 6, 21, 21}, {16, 16, 21, 21, 21},
     {16, 16, 21, 21, 21}, {16, 16, 21, 21, 21}},
    {{0, 1, 6, 6, 21}, {1, 6, 6, 21, 21}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 11, 11, 11, 11}, {11, 11, 11, 11, 11}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 11, 11, 11, 11}, {11, 11, 11, 11, 11}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 16, 6, 6, 21}, {16, 16, 6, 21, 21}, {16, 16, 21, 21, 21},
     {16, 16, 21, 21, 21}, {16, 16, 21, 21, 21}},
    {{0, 16, 6, 6, 21}, {16, 16, 6, 21, 21}, {16, 16, 21, 21, 21},
     {16, 16, 21, 21, 21}, {16, 16, 21, 21, 21}},
    {{0, 1, 6, 6, 21}, {1, 6, 6, 21, 21}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 11, 11, 11, 11}, {11, 11, 11, 11, 11}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}},
    {{0, 16, 6, 6, 21}, {16, 16, 6, 21, 21}, {16, 16, 21, 21, 21},
     {16, 16, 21, 21, 21}, {16, 16, 21, 21, 21}},
    {{0, 16, 6, 6, 21}, {16, 16, 6, 21, 21}, {16, 16, 21, 21, 21},
     {16, 16, 21, 21, 21}, {16, 16, 21, 21, 21}},
    {{0, 1, 6, 6, 21}, {1, 6, 6, 21, 21}, {6, 6, 21, 21, 21},
     {6, 21, 21, 21, 21}, {21, 21, 21, 21, 21}}};
/* clang-format on */

constexpr uint8_t kCoeffBasePositionContextOffset[3] = {26, 31, 36};

constexpr PredictionMode kInterIntraToIntraMode[kNumInterIntraModes] = {
    kPredictionModeDc, kPredictionModeVertical, kPredictionModeHorizontal,
    kPredictionModeSmooth};

// Number of horizontal luma samples before intra block copy can be used.
constexpr int kIntraBlockCopyDelayPixels = 256;
// Number of 64 by 64 blocks before intra block copy can be used.
constexpr int kIntraBlockCopyDelay64x64Blocks = kIntraBlockCopyDelayPixels / 64;

// Index [i][j] corresponds to the transform size of width 1 << (i + 2) and
// height 1 << (j + 2).
constexpr TransformSize k4x4SizeToTransformSize[5][5] = {
    {kTransformSize4x4, kTransformSize4x8, kTransformSize4x16,
     kNumTransformSizes, kNumTransformSizes},
    {kTransformSize8x4, kTransformSize8x8, kTransformSize8x16,
     kTransformSize8x32, kNumTransformSizes},
    {kTransformSize16x4, kTransformSize16x8, kTransformSize16x16,
     kTransformSize16x32, kTransformSize16x64},
    {kNumTransformSizes, kTransformSize32x8, kTransformSize32x16,
     kTransformSize32x32, kTransformSize32x64},
    {kNumTransformSizes, kNumTransformSizes, kTransformSize64x16,
     kTransformSize64x32, kTransformSize64x64}};

// Defined in section 9.3 of the spec.
constexpr TransformType kModeToTransformType[kIntraPredictionModesUV] = {
    kTransformTypeDctDct,   kTransformTypeDctAdst,  kTransformTypeAdstDct,
    kTransformTypeDctDct,   kTransformTypeAdstAdst, kTransformTypeDctAdst,
    kTransformTypeAdstDct,  kTransformTypeAdstDct,  kTransformTypeDctAdst,
    kTransformTypeAdstAdst, kTransformTypeDctAdst,  kTransformTypeAdstDct,
    kTransformTypeAdstAdst, kTransformTypeDctDct};

// Defined in section 5.11.47 of the spec. This array does not contain an entry
// for kTransformSetDctOnly, so the first dimension needs to be
// |kNumTransformSets| - 1.
constexpr TransformType kInverseTransformTypeBySet[kNumTransformSets - 1][16] =
    {{kTransformTypeIdentityIdentity, kTransformTypeDctDct,
      kTransformTypeIdentityDct, kTransformTypeDctIdentity,
      kTransformTypeAdstAdst, kTransformTypeDctAdst, kTransformTypeAdstDct},
     {kTransformTypeIdentityIdentity, kTransformTypeDctDct,
      kTransformTypeAdstAdst, kTransformTypeDctAdst, kTransformTypeAdstDct},
     {kTransformTypeIdentityIdentity, kTransformTypeIdentityDct,
      kTransformTypeDctIdentity, kTransformTypeIdentityAdst,
      kTransformTypeAdstIdentity, kTransformTypeIdentityFlipadst,
      kTransformTypeFlipadstIdentity, kTransformTypeDctDct,
      kTransformTypeDctAdst, kTransformTypeAdstDct, kTransformTypeDctFlipadst,
      kTransformTypeFlipadstDct, kTransformTypeAdstAdst,
      kTransformTypeFlipadstFlipadst, kTransformTypeFlipadstAdst,
      kTransformTypeAdstFlipadst},
     {kTransformTypeIdentityIdentity, kTransformTypeIdentityDct,
      kTransformTypeDctIdentity, kTransformTypeDctDct, kTransformTypeDctAdst,
      kTransformTypeAdstDct, kTransformTypeDctFlipadst,
      kTransformTypeFlipadstDct, kTransformTypeAdstAdst,
      kTransformTypeFlipadstFlipadst, kTransformTypeFlipadstAdst,
      kTransformTypeAdstFlipadst},
     {kTransformTypeIdentityIdentity, kTransformTypeDctDct}};

// Replaces all occurrences of 64x* and *x64 with 32x* and *x32 respectively.
constexpr TransformSize kAdjustedTransformSize[kNumTransformSizes] = {
    kTransformSize4x4,   kTransformSize4x8,   kTransformSize4x16,
    kTransformSize8x4,   kTransformSize8x8,   kTransformSize8x16,
    kTransformSize8x32,  kTransformSize16x4,  kTransformSize16x8,
    kTransformSize16x16, kTransformSize16x32, kTransformSize16x32,
    kTransformSize32x8,  kTransformSize32x16, kTransformSize32x32,
    kTransformSize32x32, kTransformSize32x16, kTransformSize32x32,
    kTransformSize32x32};

// This is the same as Max_Tx_Size_Rect array in the spec but with *x64 and 64*x
// transforms replaced with *x32 and 32x* respectively.
constexpr TransformSize kUVTransformSize[kMaxBlockSizes] = {
    kTransformSize4x4,   kTransformSize4x8,   kTransformSize4x16,
    kTransformSize8x4,   kTransformSize8x8,   kTransformSize8x16,
    kTransformSize8x32,  kTransformSize16x4,  kTransformSize16x8,
    kTransformSize16x16, kTransformSize16x32, kTransformSize16x32,
    kTransformSize32x8,  kTransformSize32x16, kTransformSize32x32,
    kTransformSize32x32, kTransformSize32x16, kTransformSize32x32,
    kTransformSize32x32, kTransformSize32x32, kTransformSize32x32,
    kTransformSize32x32};

// ith entry of this array is computed as:
// DivideBy2(TransformSizeToSquareTransformIndex(kTransformSizeSquareMin[i]) +
//           TransformSizeToSquareTransformIndex(kTransformSizeSquareMax[i]) +
//           1)
constexpr uint8_t kTransformSizeContext[kNumTransformSizes] = {
    0, 1, 1, 1, 1, 2, 2, 1, 2, 2, 3, 3, 2, 3, 3, 4, 3, 4, 4};

constexpr int8_t kSgrProjDefaultMultiplier[2] = {-32, 31};

constexpr int8_t kWienerDefaultFilter[3] = {3, -7, 15};

// Maps compound prediction modes into single modes. For e.g.
// kPredictionModeNearestNewMv will map to kPredictionModeNearestMv for index 0
// and kPredictionModeNewMv for index 1. It is used to simplify the logic in
// AssignMv (and avoid duplicate code). This is section 5.11.30. in the spec.
constexpr PredictionMode
    kCompoundToSinglePredictionMode[kNumCompoundInterPredictionModes][2] = {
        {kPredictionModeNearestMv, kPredictionModeNearestMv},
        {kPredictionModeNearMv, kPredictionModeNearMv},
        {kPredictionModeNearestMv, kPredictionModeNewMv},
        {kPredictionModeNewMv, kPredictionModeNearestMv},
        {kPredictionModeNearMv, kPredictionModeNewMv},
        {kPredictionModeNewMv, kPredictionModeNearMv},
        {kPredictionModeGlobalMv, kPredictionModeGlobalMv},
        {kPredictionModeNewMv, kPredictionModeNewMv},
};
PredictionMode GetSinglePredictionMode(int index, PredictionMode y_mode) {
  if (y_mode < kPredictionModeNearestNearestMv) {
    return y_mode;
  }
  const int lookup_index = y_mode - kPredictionModeNearestNearestMv;
  assert(lookup_index >= 0);
  return kCompoundToSinglePredictionMode[lookup_index][index];
}

// log2(dqDenom) in section 7.12.3 of the spec. We use the log2 value because
// dqDenom is always a power of two and hence right shift can be used instead of
// division.
constexpr BitMaskSet kQuantizationShift2Mask(kTransformSize32x64,
                                             kTransformSize64x32,
                                             kTransformSize64x64);
constexpr BitMaskSet kQuantizationShift1Mask(kTransformSize16x32,
                                             kTransformSize16x64,
                                             kTransformSize32x16,
                                             kTransformSize32x32,
                                             kTransformSize64x16);
int GetQuantizationShift(TransformSize tx_size) {
  if (kQuantizationShift2Mask.Contains(tx_size)) {
    return 2;
  }
  if (kQuantizationShift1Mask.Contains(tx_size)) {
    return 1;
  }
  return 0;
}

// Input: 1d array index |index|, which indexes into a 2d array of width
//     1 << |tx_width_log2|.
// Output: 1d array index which indexes into a 2d array of width
//     (1 << |tx_width_log2|) + kQuantizedCoefficientBufferPadding.
int PaddedIndex(int index, int tx_width_log2) {
  return index + MultiplyBy4(index >> tx_width_log2);
}

// Returns the minimum of |length| or |max|-|start|. This is used to clamp array
// indices when accessing arrays whose bound is equal to |max|.
int GetNumElements(int length, int start, int max) {
  return std::min(length, max - start);
}

void SetTransformType(const Tile::Block& block, int x4, int y4, int w4, int h4,
                      TransformType tx_type,
                      TransformType transform_types[32][32]) {
  const int y_offset = y4 - block.row4x4;
  const int x_offset = x4 - block.column4x4;
  static_assert(sizeof(transform_types[0][0]) == 1, "");
  for (int i = 0; i < h4; ++i) {
    memset(&transform_types[y_offset + i][x_offset], tx_type, w4);
  }
}

}  // namespace

Tile::Tile(
    int tile_number, const uint8_t* const data, size_t size,
    const ObuSequenceHeader& sequence_header,
    const ObuFrameHeader& frame_header, RefCountedBuffer* const current_frame,
    const std::array<bool, kNumReferenceFrameTypes>& reference_frame_sign_bias,
    const std::array<RefCountedBufferPtr, kNumReferenceFrameTypes>&
        reference_frames,
    Array2D<TemporalMotionVector>* const motion_field_mv,
    const std::array<uint8_t, kNumReferenceFrameTypes>& reference_order_hint,
    const WedgeMaskArray& wedge_masks,
    const SymbolDecoderContext& symbol_decoder_context,
    SymbolDecoderContext* const saved_symbol_decoder_context,
    const SegmentationMap* prev_segment_ids, PostFilter* const post_filter,
    BlockParametersHolder* const block_parameters_holder,
    Array2D<int16_t>* const cdef_index,
    Array2D<TransformSize>* const inter_transform_sizes,
    const dsp::Dsp* const dsp, ThreadPool* const thread_pool,
    ResidualBufferPool* const residual_buffer_pool,
    DecoderScratchBufferPool* const decoder_scratch_buffer_pool,
    BlockingCounterWithStatus* const pending_tiles)
    : number_(tile_number),
      data_(data),
      size_(size),
      read_deltas_(false),
      subsampling_x_{0, sequence_header.color_config.subsampling_x,
                     sequence_header.color_config.subsampling_x},
      subsampling_y_{0, sequence_header.color_config.subsampling_y,
                     sequence_header.color_config.subsampling_y},
      current_quantizer_index_(frame_header.quantizer.base_index),
      sequence_header_(sequence_header),
      frame_header_(frame_header),
      current_frame_(*current_frame),
      reference_frame_sign_bias_(reference_frame_sign_bias),
      reference_frames_(reference_frames),
      motion_field_mv_(motion_field_mv),
      reference_order_hint_(reference_order_hint),
      wedge_masks_(wedge_masks),
      reader_(data_, size_, frame_header_.enable_cdf_update),
      symbol_decoder_context_(symbol_decoder_context),
      saved_symbol_decoder_context_(saved_symbol_decoder_context),
      prev_segment_ids_(prev_segment_ids),
      dsp_(*dsp),
      post_filter_(*post_filter),
      block_parameters_holder_(*block_parameters_holder),
      quantizer_(sequence_header_.color_config.bitdepth,
                 &frame_header_.quantizer),
      residual_size_((sequence_header_.color_config.bitdepth == 8)
                         ? sizeof(int16_t)
                         : sizeof(int32_t)),
      intra_block_copy_lag_(
          frame_header_.allow_intrabc
              ? (sequence_header_.use_128x128_superblock ? 3 : 5)
              : 1),
      cdef_index_(*cdef_index),
      inter_transform_sizes_(*inter_transform_sizes),
      thread_pool_(thread_pool),
      residual_buffer_pool_(residual_buffer_pool),
      decoder_scratch_buffer_pool_(decoder_scratch_buffer_pool),
      pending_tiles_(pending_tiles),
      build_bit_mask_when_parsing_(false) {
  row_ = number_ / frame_header.tile_info.tile_columns;
  column_ = number_ % frame_header.tile_info.tile_columns;
  row4x4_start_ = frame_header.tile_info.tile_row_start[row_];
  row4x4_end_ = frame_header.tile_info.tile_row_start[row_ + 1];
  column4x4_start_ = frame_header.tile_info.tile_column_start[column_];
  column4x4_end_ = frame_header.tile_info.tile_column_start[column_ + 1];
  const int block_width4x4 = kNum4x4BlocksWide[SuperBlockSize()];
  const int block_width4x4_log2 = k4x4HeightLog2[SuperBlockSize()];
  superblock_rows_ =
      (row4x4_end_ - row4x4_start_ + block_width4x4 - 1) >> block_width4x4_log2;
  superblock_columns_ =
      (column4x4_end_ - column4x4_start_ + block_width4x4 - 1) >>
      block_width4x4_log2;
  // Enable multi-threading within a tile only if there are at least as many
  // superblock columns as |intra_block_copy_lag_|.
  split_parse_and_decode_ =
      thread_pool_ != nullptr && superblock_columns_ > intra_block_copy_lag_;
  memset(delta_lf_, 0, sizeof(delta_lf_));
  delta_lf_all_zero_ = true;
  YuvBuffer* const buffer = current_frame->buffer();
  for (int plane = 0; plane < PlaneCount(); ++plane) {
    buffer_[plane].Reset(buffer->height(plane) + buffer->bottom_border(plane),
                         buffer->stride(plane), buffer->data(plane));
    const int plane_height =
        RightShiftWithRounding(frame_header_.height, subsampling_y_[plane]);
    deblock_row_limit_[plane] =
        std::min(frame_header_.rows4x4, DivideBy4(plane_height + 3)
                                            << subsampling_y_[plane]);
    const int plane_width =
        RightShiftWithRounding(frame_header_.width, subsampling_x_[plane]);
    deblock_column_limit_[plane] =
        std::min(frame_header_.columns4x4, DivideBy4(plane_width + 3)
                                               << subsampling_x_[plane]);
  }
}

bool Tile::Init() {
  assert(coefficient_levels_.size() == dc_categories_.size());
  for (size_t i = 0; i < coefficient_levels_.size(); ++i) {
    const int contexts_per_plane = (i == kEntropyContextLeft)
                                       ? frame_header_.rows4x4
                                       : frame_header_.columns4x4;
    if (!coefficient_levels_[i].Reset(PlaneCount(), contexts_per_plane)) {
      LIBGAV1_DLOG(ERROR, "coefficient_levels_[%zu].Reset() failed.", i);
      return false;
    }
    if (!dc_categories_[i].Reset(PlaneCount(), contexts_per_plane)) {
      LIBGAV1_DLOG(ERROR, "dc_categories_[%zu].Reset() failed.", i);
      return false;
    }
  }
  if (split_parse_and_decode_) {
    assert(residual_buffer_pool_ != nullptr);
    if (!residual_buffer_threaded_.Reset(superblock_rows_, superblock_columns_,
                                         /*zero_initialize=*/false)) {
      LIBGAV1_DLOG(ERROR, "residual_buffer_threaded_.Reset() failed.");
      return false;
    }
  } else {
    residual_buffer_ = MakeAlignedUniquePtr<uint8_t>(32, 4096 * residual_size_);
    if (residual_buffer_ == nullptr) {
      LIBGAV1_DLOG(ERROR, "Allocation of residual_buffer_ failed.");
      return false;
    }
    prediction_parameters_.reset(new (std::nothrow) PredictionParameters());
    if (prediction_parameters_ == nullptr) {
      LIBGAV1_DLOG(ERROR, "Allocation of prediction_parameters_ failed.");
      return false;
    }
  }
  return true;
}

bool Tile::Decode(bool is_main_thread) {
  if (!Init()) {
    pending_tiles_->Decrement(false);
    return false;
  }
  if (frame_header_.use_ref_frame_mvs) {
    SetupMotionField(sequence_header_, frame_header_, current_frame_,
                     reference_frames_, motion_field_mv_, row4x4_start_,
                     row4x4_end_, column4x4_start_, column4x4_end_);
  }
  ResetLoopRestorationParams();
  // If this is the main thread, we build the loop filter bit masks when parsing
  // so that it happens in the current thread. This ensures that the main thread
  // does as much work as possible.
  build_bit_mask_when_parsing_ = is_main_thread;
  if (split_parse_and_decode_) {
    if (!ThreadedDecode()) return false;
  } else {
    const int block_width4x4 = kNum4x4BlocksWide[SuperBlockSize()];
    std::unique_ptr<DecoderScratchBuffer> scratch_buffer =
        decoder_scratch_buffer_pool_->Get();
    if (scratch_buffer == nullptr) {
      pending_tiles_->Decrement(false);
      LIBGAV1_DLOG(ERROR, "Failed to get scratch buffer.");
      return false;
    }
    for (int row4x4 = row4x4_start_; row4x4 < row4x4_end_;
         row4x4 += block_width4x4) {
      for (int column4x4 = column4x4_start_; column4x4 < column4x4_end_;
           column4x4 += block_width4x4) {
        if (!ProcessSuperBlock(row4x4, column4x4, block_width4x4,
                               scratch_buffer.get(),
                               kProcessingModeParseAndDecode)) {
          pending_tiles_->Decrement(false);
          LIBGAV1_DLOG(ERROR, "Error decoding super block row: %d column: %d",
                       row4x4, column4x4);
          return false;
        }
      }
    }
    decoder_scratch_buffer_pool_->Release(std::move(scratch_buffer));
  }
  if (frame_header_.enable_frame_end_update_cdf &&
      number_ == frame_header_.tile_info.context_update_id) {
    *saved_symbol_decoder_context_ = symbol_decoder_context_;
  }
  if (!split_parse_and_decode_) {
    pending_tiles_->Decrement(true);
  }
  return true;
}

bool Tile::ThreadedDecode() {
  {
    std::lock_guard<std::mutex> lock(threading_.mutex);
    if (!threading_.sb_state.Reset(superblock_rows_, superblock_columns_)) {
      pending_tiles_->Decrement(false);
      LIBGAV1_DLOG(ERROR, "threading.sb_state.Reset() failed.");
      return false;
    }
    // Account for the parsing job.
    ++threading_.pending_jobs;
  }

  const int block_width4x4 = kNum4x4BlocksWide[SuperBlockSize()];

  // Begin parsing.
  std::unique_ptr<DecoderScratchBuffer> scratch_buffer =
      decoder_scratch_buffer_pool_->Get();
  if (scratch_buffer == nullptr) {
    pending_tiles_->Decrement(false);
    LIBGAV1_DLOG(ERROR, "Failed to get scratch buffer.");
    return false;
  }
  for (int row4x4 = row4x4_start_, row_index = 0; row4x4 < row4x4_end_;
       row4x4 += block_width4x4, ++row_index) {
    for (int column4x4 = column4x4_start_, column_index = 0;
         column4x4 < column4x4_end_;
         column4x4 += block_width4x4, ++column_index) {
      if (!ProcessSuperBlock(row4x4, column4x4, block_width4x4,
                             scratch_buffer.get(), kProcessingModeParseOnly)) {
        std::lock_guard<std::mutex> lock(threading_.mutex);
        threading_.abort = true;
        break;
      }
      std::unique_lock<std::mutex> lock(threading_.mutex);
      if (threading_.abort) break;
      threading_.sb_state[row_index][column_index] = kSuperBlockStateParsed;
      // Schedule the decoding of this superblock if it is allowed.
      if (CanDecode(row_index, column_index)) {
        ++threading_.pending_jobs;
        threading_.sb_state[row_index][column_index] =
            kSuperBlockStateScheduled;
        lock.unlock();
        thread_pool_->Schedule(
            [this, row_index, column_index, block_width4x4]() {
              DecodeSuperBlock(row_index, column_index, block_width4x4);
            });
      }
    }
    std::lock_guard<std::mutex> lock(threading_.mutex);
    if (threading_.abort) break;
  }
  decoder_scratch_buffer_pool_->Release(std::move(scratch_buffer));

  // We are done parsing. We can return here since the calling thread will make
  // sure that it waits for all the superblocks to be decoded.
  //
  // Finish using |threading_| before |pending_tiles_->Decrement()| because the
  // Tile object could go out of scope as soon as |pending_tiles_->Decrement()|
  // is called.
  threading_.mutex.lock();
  const bool no_pending_jobs = (--threading_.pending_jobs == 0);
  const bool job_succeeded = !threading_.abort;
  threading_.mutex.unlock();
  if (no_pending_jobs) {
    // We are done parsing and decoding this tile.
    pending_tiles_->Decrement(job_succeeded);
  }
  return job_succeeded;
}

bool Tile::CanDecode(int row_index, int column_index) const {
  assert(row_index >= 0);
  assert(column_index >= 0);
  // If |threading_.sb_state[row_index][column_index]| is not equal to
  // kSuperBlockStateParsed, then return false. This is ok because if
  // |threading_.sb_state[row_index][column_index]| is equal to:
  //   kSuperBlockStateNone - then the superblock is not yet parsed.
  //   kSuperBlockStateScheduled - then the superblock is already scheduled for
  //                               decode.
  //   kSuperBlockStateDecoded - then the superblock has already been decoded.
  if (row_index >= superblock_rows_ || column_index >= superblock_columns_ ||
      threading_.sb_state[row_index][column_index] != kSuperBlockStateParsed) {
    return false;
  }
  // First superblock has no dependencies.
  if (row_index == 0 && column_index == 0) {
    return true;
  }
  // Superblocks in the first row only depend on the superblock to the left of
  // it.
  if (row_index == 0) {
    return threading_.sb_state[0][column_index - 1] == kSuperBlockStateDecoded;
  }
  // All other superblocks depend on superblock to the left of it (if one
  // exists) and superblock to the top right with a lag of
  // |intra_block_copy_lag_| (if one exists).
  const int top_right_column_index =
      std::min(column_index + intra_block_copy_lag_, superblock_columns_ - 1);
  return threading_.sb_state[row_index - 1][top_right_column_index] ==
             kSuperBlockStateDecoded &&
         (column_index == 0 ||
          threading_.sb_state[row_index][column_index - 1] ==
              kSuperBlockStateDecoded);
}

void Tile::DecodeSuperBlock(int row_index, int column_index,
                            int block_width4x4) {
  const int row4x4 = row4x4_start_ + (row_index * block_width4x4);
  const int column4x4 = column4x4_start_ + (column_index * block_width4x4);
  std::unique_ptr<DecoderScratchBuffer> scratch_buffer =
      decoder_scratch_buffer_pool_->Get();
  bool ok = scratch_buffer != nullptr;
  if (ok) {
    ok = ProcessSuperBlock(row4x4, column4x4, block_width4x4,
                           scratch_buffer.get(), kProcessingModeDecodeOnly);
    decoder_scratch_buffer_pool_->Release(std::move(scratch_buffer));
  }
  std::unique_lock<std::mutex> lock(threading_.mutex);
  if (ok) {
    threading_.sb_state[row_index][column_index] = kSuperBlockStateDecoded;
    // Candidate rows and columns that we could potentially begin the decoding
    // (if it is allowed to do so). The candidates are:
    //   1) The superblock to the bottom-left of the current superblock with a
    //   lag of |intra_block_copy_lag_| (or the beginning of the next superblock
    //   row in case there are less than |intra_block_copy_lag_| superblock
    //   columns in the Tile).
    //   2) The superblock to the right of the current superblock.
    const int candidate_row_indices[] = {row_index + 1, row_index};
    const int candidate_column_indices[] = {
        std::max(0, column_index - intra_block_copy_lag_), column_index + 1};
    for (size_t i = 0; i < std::extent<decltype(candidate_row_indices)>::value;
         ++i) {
      const int candidate_row_index = candidate_row_indices[i];
      const int candidate_column_index = candidate_column_indices[i];
      if (!CanDecode(candidate_row_index, candidate_column_index)) {
        continue;
      }
      ++threading_.pending_jobs;
      threading_.sb_state[candidate_row_index][candidate_column_index] =
          kSuperBlockStateScheduled;
      lock.unlock();
      thread_pool_->Schedule([this, candidate_row_index, candidate_column_index,
                              block_width4x4]() {
        DecodeSuperBlock(candidate_row_index, candidate_column_index,
                         block_width4x4);
      });
      lock.lock();
    }
  } else {
    threading_.abort = true;
  }
  // Finish using |threading_| before |pending_tiles_->Decrement()| because the
  // Tile object could go out of scope as soon as |pending_tiles_->Decrement()|
  // is called.
  const bool no_pending_jobs = (--threading_.pending_jobs == 0);
  const bool job_succeeded = !threading_.abort;
  lock.unlock();
  if (no_pending_jobs) {
    // We are done parsing and decoding this tile.
    pending_tiles_->Decrement(job_succeeded);
  }
}

int Tile::GetTransformAllZeroContext(const Block& block, Plane plane,
                                     TransformSize tx_size, int x4, int y4,
                                     int w4, int h4) {
  const int max_x4x4 = frame_header_.columns4x4 >> subsampling_x_[plane];
  const int max_y4x4 = frame_header_.rows4x4 >> subsampling_y_[plane];

  const int tx_width = kTransformWidth[tx_size];
  const int tx_height = kTransformHeight[tx_size];
  const BlockSize plane_size = block.residual_size[GetPlaneType(plane)];
  const int block_width = kBlockWidthPixels[plane_size];
  const int block_height = kBlockHeightPixels[plane_size];

  int top = 0;
  int left = 0;
  const int num_top_elements = GetNumElements(w4, x4, max_x4x4);
  const int num_left_elements = GetNumElements(h4, y4, max_y4x4);
  if (plane == kPlaneY) {
    if (block_width == tx_width && block_height == tx_height) return 0;
    const uint8_t* coefficient_levels =
        &coefficient_levels_[kEntropyContextTop][plane][x4];
    for (int i = 0; i < num_top_elements; ++i) {
      top = std::max(top, static_cast<int>(coefficient_levels[i]));
    }
    coefficient_levels = &coefficient_levels_[kEntropyContextLeft][plane][y4];
    for (int i = 0; i < num_left_elements; ++i) {
      left = std::max(left, static_cast<int>(coefficient_levels[i]));
    }
    assert(top <= 4);
    assert(left <= 4);
    // kAllZeroContextsByTopLeft is pre-computed based on the logic in the spec
    // for top and left.
    return kAllZeroContextsByTopLeft[top][left];
  }
  const uint8_t* coefficient_levels =
      &coefficient_levels_[kEntropyContextTop][plane][x4];
  const int8_t* dc_categories = &dc_categories_[kEntropyContextTop][plane][x4];
  for (int i = 0; i < num_top_elements; ++i) {
    top |= coefficient_levels[i];
    top |= dc_categories[i];
  }
  coefficient_levels = &coefficient_levels_[kEntropyContextLeft][plane][y4];
  dc_categories = &dc_categories_[kEntropyContextLeft][plane][y4];
  for (int i = 0; i < num_left_elements; ++i) {
    left |= coefficient_levels[i];
    left |= dc_categories[i];
  }
  return static_cast<int>(top != 0) + static_cast<int>(left != 0) + 7 +
         3 * static_cast<int>(block_width * block_height >
                              tx_width * tx_height);
}

TransformSet Tile::GetTransformSet(TransformSize tx_size, bool is_inter) const {
  const TransformSize tx_size_square_min = kTransformSizeSquareMin[tx_size];
  const TransformSize tx_size_square_max = kTransformSizeSquareMax[tx_size];
  if (tx_size_square_max == kTransformSize64x64) return kTransformSetDctOnly;
  if (is_inter) {
    if (frame_header_.reduced_tx_set ||
        tx_size_square_max == kTransformSize32x32) {
      return kTransformSetInter3;
    }
    if (tx_size_square_min == kTransformSize16x16) return kTransformSetInter2;
    return kTransformSetInter1;
  }
  if (tx_size_square_max == kTransformSize32x32) return kTransformSetDctOnly;
  if (frame_header_.reduced_tx_set ||
      tx_size_square_min == kTransformSize16x16) {
    return kTransformSetIntra2;
  }
  return kTransformSetIntra1;
}

TransformType Tile::ComputeTransformType(const Block& block, Plane plane,
                                         TransformSize tx_size, int block_x,
                                         int block_y) {
  const BlockParameters& bp = *block.bp;
  const TransformSize tx_size_square_max = kTransformSizeSquareMax[tx_size];
  if (frame_header_.segmentation.lossless[bp.segment_id] ||
      tx_size_square_max == kTransformSize64x64) {
    return kTransformTypeDctDct;
  }
  if (plane == kPlaneY) {
    return transform_types_[block_y - block.row4x4][block_x - block.column4x4];
  }
  const TransformSet tx_set = GetTransformSet(tx_size, bp.is_inter);
  TransformType tx_type;
  if (bp.is_inter) {
    const int x4 =
        std::max(block.column4x4, block_x << subsampling_x_[kPlaneU]);
    const int y4 = std::max(block.row4x4, block_y << subsampling_y_[kPlaneU]);
    tx_type = transform_types_[y4 - block.row4x4][x4 - block.column4x4];
  } else {
    tx_type = kModeToTransformType[bp.uv_mode];
  }
  return kTransformTypeInSetMask[tx_set].Contains(tx_type)
             ? tx_type
             : kTransformTypeDctDct;
}

void Tile::ReadTransformType(const Block& block, int x4, int y4,
                             TransformSize tx_size) {
  BlockParameters& bp = *block.bp;
  const TransformSet tx_set = GetTransformSet(tx_size, bp.is_inter);

  TransformType tx_type = kTransformTypeDctDct;
  if (tx_set != kTransformSetDctOnly &&
      frame_header_.segmentation.qindex[bp.segment_id] > 0) {
    const int cdf_index = SymbolDecoderContext::TxTypeIndex(tx_set);
    const int cdf_tx_size_index =
        TransformSizeToSquareTransformIndex(kTransformSizeSquareMin[tx_size]);
    uint16_t* cdf;
    if (bp.is_inter) {
      cdf = symbol_decoder_context_
                .inter_tx_type_cdf[cdf_index][cdf_tx_size_index];
    } else {
      const PredictionMode intra_direction =
          block.bp->prediction_parameters->use_filter_intra
              ? kFilterIntraModeToIntraPredictor[block.bp->prediction_parameters
                                                     ->filter_intra_mode]
              : bp.y_mode;
      cdf =
          symbol_decoder_context_
              .intra_tx_type_cdf[cdf_index][cdf_tx_size_index][intra_direction];
    }
    tx_type = static_cast<TransformType>(
        reader_.ReadSymbol(cdf, kNumTransformTypesInSet[tx_set]));
    // This array does not contain an entry for kTransformSetDctOnly, so the
    // first dimension needs to be offset by 1.
    tx_type = kInverseTransformTypeBySet[tx_set - 1][tx_type];
  }
  SetTransformType(block, x4, y4, kTransformWidth4x4[tx_size],
                   kTransformHeight4x4[tx_size], tx_type, transform_types_);
}

// Section 8.3.2 in the spec, under coeff_base_eob.
int Tile::GetCoeffBaseContextEob(TransformSize tx_size, int index) {
  if (index == 0) return 0;
  const TransformSize adjusted_tx_size = kAdjustedTransformSize[tx_size];
  const int tx_width_log2 = kTransformWidthLog2[adjusted_tx_size];
  const int tx_height = kTransformHeight[adjusted_tx_size];
  if (index <= DivideBy8(tx_height << tx_width_log2)) return 1;
  if (index <= DivideBy4(tx_height << tx_width_log2)) return 2;
  return 3;
}

// Section 8.3.2 in the spec, under coeff_base.
int Tile::GetCoeffBaseContext2D(const int32_t* const quantized_buffer,
                                TransformSize tx_size,
                                int adjusted_tx_width_log2, uint16_t pos) {
  if (pos == 0) return 0;
  const int tx_width = 1 << adjusted_tx_width_log2;
  const int padded_tx_width = tx_width + kQuantizedCoefficientBufferPadding;
  const int32_t* const quantized =
      &quantized_buffer[PaddedIndex(pos, adjusted_tx_width_log2)];
  const int context = std::min(
      4, DivideBy2(1 + (std::min(quantized[1], 3) +                    // {0, 1}
                        std::min(quantized[padded_tx_width], 3) +      // {1, 0}
                        std::min(quantized[padded_tx_width + 1], 3) +  // {1, 1}
                        std::min(quantized[2], 3) +                    // {0, 2}
                        std::min(quantized[MultiplyBy2(padded_tx_width)],
                                 3))));  // {2, 0}
  const int row = pos >> adjusted_tx_width_log2;
  const int column = pos & (tx_width - 1);
  return context + kCoeffBaseContextOffset[tx_size][std::min(row, 4)]
                                          [std::min(column, 4)];
}

// Section 8.3.2 in the spec, under coeff_base.
int Tile::GetCoeffBaseContextHorizontal(const int32_t* const quantized_buffer,
                                        TransformSize /*tx_size*/,
                                        int adjusted_tx_width_log2,
                                        uint16_t pos) {
  const int tx_width = 1 << adjusted_tx_width_log2;
  const int padded_tx_width = tx_width + kQuantizedCoefficientBufferPadding;
  const int32_t* const quantized =
      &quantized_buffer[PaddedIndex(pos, adjusted_tx_width_log2)];
  const int context = std::min(
      4, DivideBy2(1 + (std::min(quantized[1], 3) +                // {0, 1}
                        std::min(quantized[padded_tx_width], 3) +  // {1, 0}
                        std::min(quantized[2], 3) +                // {0, 2}
                        std::min(quantized[3], 3) +                // {0, 3}
                        std::min(quantized[4], 3))));              // {0, 4}
  const int index = pos & (tx_width - 1);
  return context + kCoeffBasePositionContextOffset[std::min(index, 2)];
}

// Section 8.3.2 in the spec, under coeff_base.
int Tile::GetCoeffBaseContextVertical(const int32_t* const quantized_buffer,
                                      TransformSize /*tx_size*/,
                                      int adjusted_tx_width_log2,
                                      uint16_t pos) {
  const int tx_width = 1 << adjusted_tx_width_log2;
  const int padded_tx_width = tx_width + kQuantizedCoefficientBufferPadding;
  const int32_t* const quantized =
      &quantized_buffer[PaddedIndex(pos, adjusted_tx_width_log2)];
  const int context = std::min(
      4, DivideBy2(1 + (std::min(quantized[1], 3) +                // {0, 1}
                        std::min(quantized[padded_tx_width], 3) +  // {1, 0}
                        std::min(quantized[MultiplyBy2(padded_tx_width)],
                                 3) +                                  // {2, 0}
                        std::min(quantized[padded_tx_width * 3], 3) +  // {3, 0}
                        std::min(quantized[MultiplyBy4(padded_tx_width)],
                                 3))));  // {4, 0}

  const int index = pos >> adjusted_tx_width_log2;
  return context + kCoeffBasePositionContextOffset[std::min(index, 2)];
}

// Section 8.3.2 in the spec, under coeff_br.
int Tile::GetCoeffBaseRangeContext2D(const int32_t* const quantized_buffer,
                                     int adjusted_tx_width_log2, int pos) {
  const uint8_t tx_width = 1 << adjusted_tx_width_log2;
  const int padded_tx_width = tx_width + kQuantizedCoefficientBufferPadding;
  const int32_t* const quantized =
      &quantized_buffer[PaddedIndex(pos, adjusted_tx_width_log2)];
  const int context = std::min(
      6, DivideBy2(
             1 +
             std::min(quantized[1],
                      kQuantizerCoefficientBaseRangeContextClamp) +  // {0, 1}
             std::min(quantized[padded_tx_width],
                      kQuantizerCoefficientBaseRangeContextClamp) +  // {1, 0}
             std::min(quantized[padded_tx_width + 1],
                      kQuantizerCoefficientBaseRangeContextClamp)));  // {1, 1}
  if (pos == 0) return context;
  const int row = pos >> adjusted_tx_width_log2;
  const int column = pos & (tx_width - 1);
  return context + (((row | column) < 2) ? 7 : 14);
}

// Section 8.3.2 in the spec, under coeff_br.
int Tile::GetCoeffBaseRangeContextHorizontal(
    const int32_t* const quantized_buffer, int adjusted_tx_width_log2,
    int pos) {
  const uint8_t tx_width = 1 << adjusted_tx_width_log2;
  const int padded_tx_width = tx_width + kQuantizedCoefficientBufferPadding;
  const int32_t* const quantized =
      &quantized_buffer[PaddedIndex(pos, adjusted_tx_width_log2)];
  const int context = std::min(
      6, DivideBy2(
             1 +
             std::min(quantized[1],
                      kQuantizerCoefficientBaseRangeContextClamp) +  // {0, 1}
             std::min(quantized[padded_tx_width],
                      kQuantizerCoefficientBaseRangeContextClamp) +  // {1, 0}
             std::min(quantized[2],
                      kQuantizerCoefficientBaseRangeContextClamp)));  // {0, 2}
  if (pos == 0) return context;
  const int column = pos & (tx_width - 1);
  return context + ((column == 0) ? 7 : 14);
}

// Section 8.3.2 in the spec, under coeff_br.
int Tile::GetCoeffBaseRangeContextVertical(
    const int32_t* const quantized_buffer, int adjusted_tx_width_log2,
    int pos) {
  const uint8_t tx_width = 1 << adjusted_tx_width_log2;
  const int padded_tx_width = tx_width + kQuantizedCoefficientBufferPadding;
  const int32_t* const quantized =
      &quantized_buffer[PaddedIndex(pos, adjusted_tx_width_log2)];
  const int context = std::min(
      6, DivideBy2(
             1 +
             std::min(quantized[1],
                      kQuantizerCoefficientBaseRangeContextClamp) +  // {0, 1}
             std::min(quantized[padded_tx_width],
                      kQuantizerCoefficientBaseRangeContextClamp) +  // {1, 0}
             std::min(quantized[MultiplyBy2(padded_tx_width)],
                      kQuantizerCoefficientBaseRangeContextClamp)));  // {2, 0}
  if (pos == 0) return context;
  const int row = pos >> adjusted_tx_width_log2;
  return context + ((row == 0) ? 7 : 14);
}

// Section 8.3.2 in the spec, under coeff_br. Optimized for end of block based
// on the fact that {0, 1}, {1, 0}, {1, 1}, {0, 2} and {2, 0} will all be 0 in
// the end of block case.
int Tile::GetCoeffBaseRangeContextEob(int adjusted_tx_width_log2, int pos,
                                      TransformClass tx_class) {
  if (pos == 0) return 0;
  const uint8_t tx_width = 1 << adjusted_tx_width_log2;
  const int row = pos >> adjusted_tx_width_log2;
  const int column = pos & (tx_width - 1);
  return ((tx_class == kTransformClass2D && (row | column) < 2) ||
          (tx_class == kTransformClassHorizontal && column == 0) ||
          (tx_class == kTransformClassVertical && row == 0))
             ? 7
             : 14;
}

int Tile::GetDcSignContext(int x4, int y4, int w4, int h4, Plane plane) {
  const int max_x4x4 = frame_header_.columns4x4 >> subsampling_x_[plane];
  const int8_t* dc_categories = &dc_categories_[kEntropyContextTop][plane][x4];
  int dc_sign = std::accumulate(
      dc_categories, dc_categories + GetNumElements(w4, x4, max_x4x4), 0);
  const int max_y4x4 = frame_header_.rows4x4 >> subsampling_y_[plane];
  dc_categories = &dc_categories_[kEntropyContextLeft][plane][y4];
  dc_sign = std::accumulate(
      dc_categories, dc_categories + GetNumElements(h4, y4, max_y4x4), dc_sign);
  // This return statement is equivalent to:
  //   if (dc_sign < 0) return 1;
  //   if (dc_sign > 0) return 2;
  //   return 0;
  return static_cast<int>(dc_sign < 0) +
         MultiplyBy2(static_cast<int>(dc_sign > 0));
}

void Tile::SetEntropyContexts(int x4, int y4, int w4, int h4, Plane plane,
                              uint8_t coefficient_level, int8_t dc_category) {
  const int max_x4x4 = frame_header_.columns4x4 >> subsampling_x_[plane];
  const int num_top_elements = GetNumElements(w4, x4, max_x4x4);
  memset(&coefficient_levels_[kEntropyContextTop][plane][x4], coefficient_level,
         num_top_elements);
  memset(&dc_categories_[kEntropyContextTop][plane][x4], dc_category,
         num_top_elements);
  const int max_y4x4 = frame_header_.rows4x4 >> subsampling_y_[plane];
  const int num_left_elements = GetNumElements(h4, y4, max_y4x4);
  memset(&coefficient_levels_[kEntropyContextLeft][plane][y4],
         coefficient_level, num_left_elements);
  memset(&dc_categories_[kEntropyContextLeft][plane][y4], dc_category,
         num_left_elements);
}

void Tile::ScaleMotionVector(const MotionVector& mv, const Plane plane,
                             const int reference_frame_index, const int x,
                             const int y, int* const start_x,
                             int* const start_y, int* const step_x,
                             int* const step_y) {
  const int reference_upscaled_width =
      (reference_frame_index == -1)
          ? frame_header_.upscaled_width
          : reference_frames_[reference_frame_index]->upscaled_width();
  const int reference_height =
      (reference_frame_index == -1)
          ? frame_header_.height
          : reference_frames_[reference_frame_index]->frame_height();
  assert(2 * frame_header_.width >= reference_upscaled_width &&
         2 * frame_header_.height >= reference_height &&
         frame_header_.width <= 16 * reference_upscaled_width &&
         frame_header_.height <= 16 * reference_height);
  const bool is_scaled_x = reference_upscaled_width != frame_header_.width;
  const bool is_scaled_y = reference_height != frame_header_.height;
  const int half_sample = 1 << (kSubPixelBits - 1);
  int orig_x = (x << kSubPixelBits) + ((2 * mv.mv[1]) >> subsampling_x_[plane]);
  int orig_y = (y << kSubPixelBits) + ((2 * mv.mv[0]) >> subsampling_y_[plane]);
  const int rounding_offset =
      DivideBy2(1 << (kScaleSubPixelBits - kSubPixelBits));
  if (is_scaled_x) {
    const int scale_x = ((reference_upscaled_width << kReferenceScaleShift) +
                         DivideBy2(frame_header_.width)) /
                        frame_header_.width;
    *step_x = RightShiftWithRoundingSigned(
        scale_x, kReferenceScaleShift - kScaleSubPixelBits);
    orig_x += half_sample;
    // When frame size is 4k and above, orig_x can be above 16 bits, scale_x can
    // be up to 15 bits. So we use int64_t to hold base_x.
    const int64_t base_x = static_cast<int64_t>(orig_x) * scale_x -
                           (half_sample << kReferenceScaleShift);
    *start_x =
        RightShiftWithRoundingSigned(
            base_x, kReferenceScaleShift + kSubPixelBits - kScaleSubPixelBits) +
        rounding_offset;
  } else {
    *step_x = 1 << kScaleSubPixelBits;
    *start_x = LeftShift(orig_x, 6) + rounding_offset;
  }
  if (is_scaled_y) {
    const int scale_y = ((reference_height << kReferenceScaleShift) +
                         DivideBy2(frame_header_.height)) /
                        frame_header_.height;
    *step_y = RightShiftWithRoundingSigned(
        scale_y, kReferenceScaleShift - kScaleSubPixelBits);
    orig_y += half_sample;
    const int64_t base_y = static_cast<int64_t>(orig_y) * scale_y -
                           (half_sample << kReferenceScaleShift);
    *start_y =
        RightShiftWithRoundingSigned(
            base_y, kReferenceScaleShift + kSubPixelBits - kScaleSubPixelBits) +
        rounding_offset;
  } else {
    *step_y = 1 << kScaleSubPixelBits;
    *start_y = LeftShift(orig_y, 6) + rounding_offset;
  }
}

template <bool is_dc_coefficient>
bool Tile::ReadSignAndApplyDequantization(
    const Block& block, int32_t* const quantized_buffer,
    const uint16_t* const scan, int i, int adjusted_tx_width_log2, int tx_width,
    int q_value, const uint8_t* const quantizer_matrix, int shift,
    int max_value, uint16_t* const dc_sign_cdf, int8_t* const dc_category,
    int* const coefficient_level) {
  int pos = is_dc_coefficient ? 0 : scan[i];
  const int pos_index =
      is_dc_coefficient ? 0 : PaddedIndex(pos, adjusted_tx_width_log2);
  // If quantized_buffer[pos_index] is zero, then the rest of the function has
  // no effect.
  if (quantized_buffer[pos_index] == 0) return true;
  const int sign = is_dc_coefficient
                       ? static_cast<int>(reader_.ReadSymbol(dc_sign_cdf))
                       : reader_.ReadBit();
  if (quantized_buffer[pos_index] >
      kNumQuantizerBaseLevels + kQuantizerCoefficientBaseRange) {
    int length = 0;
    bool golomb_length_bit = false;
    do {
      golomb_length_bit = static_cast<bool>(reader_.ReadBit());
      ++length;
      if (length > 20) {
        LIBGAV1_DLOG(ERROR, "Invalid golomb_length %d", length);
        return false;
      }
    } while (!golomb_length_bit);
    int x = 1;
    for (int i = length - 2; i >= 0; --i) {
      x = (x << 1) | reader_.ReadBit();
    }
    quantized_buffer[pos_index] += x - 1;
  }
  if (is_dc_coefficient && quantized_buffer[0] > 0) {
    *dc_category = (sign != 0) ? -1 : 1;
  }
  quantized_buffer[pos_index] &= 0xfffff;
  *coefficient_level += quantized_buffer[pos_index];
  // Apply dequantization. Step 1 of section 7.12.3 in the spec.
  int q = q_value;
  if (quantizer_matrix != nullptr) {
    q = RightShiftWithRounding(q * quantizer_matrix[pos], 5);
  }
  // The intermediate multiplication can exceed 32 bits, so it has to be
  // performed by promoting one of the values to int64_t.
  int32_t dequantized_value =
      (static_cast<int64_t>(q) * quantized_buffer[pos_index]) & 0xffffff;
  dequantized_value >>= shift;
  // At this point:
  //   * |dequantized_value| is always non-negative.
  //   * |sign| can be either 0 or 1.
  //   * min_value = -(max_value + 1).
  // We need to apply the following:
  // dequantized_value = sign ? -dequantized_value : dequantized_value;
  // dequantized_value = Clip3(dequantized_value, min_value, max_value);
  //
  // Note that -x == ~(x - 1).
  //
  // Now, The above two lines can be done with a std::min and xor as follows:
  dequantized_value = std::min(dequantized_value - sign, max_value) ^ -sign;
  // Inverse transform process assumes that the quantized coefficients are
  // stored as a virtual 2d array of size |tx_width| x |tx_height|. If
  // transform width is 64, then this assumption is broken because the scan
  // order used for populating the coefficients for such transforms is the
  // same as the one used for corresponding transform with width 32 (e.g. the
  // scan order used for 64x16 is the same as the one used for 32x16). So we
  // have to recompute the value of pos so that it reflects the index of the
  // 2d array of size 64 x |tx_height|.
  if (!is_dc_coefficient && tx_width == 64) {
    const int row_index = DivideBy32(pos);
    const int column_index = Mod32(pos);
    pos = MultiplyBy64(row_index) + column_index;
  }
#if LIBGAV1_MAX_BITDEPTH >= 10
  if (sequence_header_.color_config.bitdepth > 8) {
    auto* const residual_buffer = reinterpret_cast<int32_t*>(*block.residual);
    residual_buffer[pos] = dequantized_value;
    return true;
  }
#endif
  auto* const residual_buffer = reinterpret_cast<int16_t*>(*block.residual);
  residual_buffer[pos] = dequantized_value;
  return true;
}

int Tile::ReadCoeffBaseRange(int clamped_tx_size_context, int cdf_context,
                             int plane_type) {
  int level = 0;
  for (int j = 0; j < kCoeffBaseRangeMaxIterations; ++j) {
    const int coeff_base_range = reader_.ReadSymbol<kCoeffBaseRangeSymbolCount>(
        symbol_decoder_context_.coeff_base_range_cdf[clamped_tx_size_context]
                                                    [plane_type][cdf_context]);
    level += coeff_base_range;
    if (coeff_base_range < (kCoeffBaseRangeSymbolCount - 1)) break;
  }
  return level;
}

int16_t Tile::ReadTransformCoefficients(const Block& block, Plane plane,
                                        int start_x, int start_y,
                                        TransformSize tx_size,
                                        TransformType* const tx_type) {
  const int x4 = DivideBy4(start_x);
  const int y4 = DivideBy4(start_y);
  const int w4 = kTransformWidth4x4[tx_size];
  const int h4 = kTransformHeight4x4[tx_size];
  const int tx_size_context = kTransformSizeContext[tx_size];
  int context =
      GetTransformAllZeroContext(block, plane, tx_size, x4, y4, w4, h4);
  const bool all_zero = reader_.ReadSymbol(
      symbol_decoder_context_.all_zero_cdf[tx_size_context][context]);
  if (all_zero) {
    if (plane == kPlaneY) {
      SetTransformType(block, x4, y4, w4, h4, kTransformTypeDctDct,
                       transform_types_);
    }
    SetEntropyContexts(x4, y4, w4, h4, plane, 0, 0);
    // This is not used in this case, so it can be set to any value.
    *tx_type = kNumTransformTypes;
    return 0;
  }
  const int tx_width = kTransformWidth[tx_size];
  const int tx_height = kTransformHeight[tx_size];
  memset(*block.residual, 0, tx_width * tx_height * residual_size_);
  const int clamped_tx_width = std::min(tx_width, 32);
  const int clamped_tx_height = std::min(tx_height, 32);
  const int padded_tx_width =
      clamped_tx_width + kQuantizedCoefficientBufferPadding;
  const int padded_tx_height =
      clamped_tx_height + kQuantizedCoefficientBufferPadding;
  if (plane == kPlaneY) {
    ReadTransformType(block, x4, y4, tx_size);
  }
  BlockParameters& bp = *block.bp;
  *tx_type = ComputeTransformType(block, plane, tx_size, x4, y4);
  const int eob_multi_size = kEobMultiSizeLookup[tx_size];
  const PlaneType plane_type = GetPlaneType(plane);
  const TransformClass tx_class = GetTransformClass(*tx_type);
  context = static_cast<int>(tx_class != kTransformClass2D);
  uint16_t* cdf;
  switch (eob_multi_size) {
    case 0:
      cdf = symbol_decoder_context_.eob_pt_16_cdf[plane_type][context];
      break;
    case 1:
      cdf = symbol_decoder_context_.eob_pt_32_cdf[plane_type][context];
      break;
    case 2:
      cdf = symbol_decoder_context_.eob_pt_64_cdf[plane_type][context];
      break;
    case 3:
      cdf = symbol_decoder_context_.eob_pt_128_cdf[plane_type][context];
      break;
    case 4:
      cdf = symbol_decoder_context_.eob_pt_256_cdf[plane_type][context];
      break;
    case 5:
      cdf = symbol_decoder_context_.eob_pt_512_cdf[plane_type];
      break;
    case 6:
    default:
      cdf = symbol_decoder_context_.eob_pt_1024_cdf[plane_type];
      break;
  }
  const int16_t eob_pt =
      1 + reader_.ReadSymbol(cdf, kEobPt16SymbolCount + eob_multi_size);
  int16_t eob = (eob_pt < 2) ? eob_pt : ((1 << (eob_pt - 2)) + 1);
  if (eob_pt >= 3) {
    context = eob_pt - 3;
    const bool eob_extra = reader_.ReadSymbol(
        symbol_decoder_context_
            .eob_extra_cdf[tx_size_context][plane_type][context]);
    if (eob_extra) eob += 1 << (eob_pt - 3);
    for (int i = 1; i < eob_pt - 2; ++i) {
      assert(eob_pt - i >= 3);
      assert(eob_pt <= kEobPt1024SymbolCount);
      if (static_cast<bool>(reader_.ReadBit())) {
        eob += 1 << (eob_pt - i - 3);
      }
    }
  }
  int32_t* const quantized = block.scratch_buffer->quantized_buffer;
  // Only the first |padded_tx_width| * |padded_tx_height| values of |quantized|
  // will be used by this function and the functions to which it is passed into.
  // So we simply need to zero out those values before it is being used. If
  // |eob| == 1, then only the first index will be populated and used. So there
  // is no need to initialize this array in that case.
  if (eob > 1) {
    memset(quantized, 0,
           padded_tx_width * padded_tx_height * sizeof(quantized[0]));
  }
  const uint16_t* scan = kScan[tx_class][tx_size];
  const TransformSize adjusted_tx_size = kAdjustedTransformSize[tx_size];
  const int adjusted_tx_width_log2 = kTransformWidthLog2[adjusted_tx_size];
  const int clamped_tx_size_context = std::min(tx_size_context, 3);
  // Read the last coefficient.
  {
    context = GetCoeffBaseContextEob(tx_size, eob - 1);
    const uint16_t pos = scan[eob - 1];
    int level =
        1 + reader_.ReadSymbol(
                symbol_decoder_context_
                    .coeff_base_eob_cdf[tx_size_context][plane_type][context],
                kCoeffBaseEobSymbolCount);
    if (level > kNumQuantizerBaseLevels) {
      level += ReadCoeffBaseRange(
          clamped_tx_size_context,
          GetCoeffBaseRangeContextEob(adjusted_tx_width_log2, pos, tx_class),
          plane_type);
    }
    quantized[PaddedIndex(pos, adjusted_tx_width_log2)] = level;
  }
  // Lookup used to call the right variant of GetCoeffBaseContext*() based on
  // the transform class.
  static constexpr int (Tile::*kGetCoeffBaseContextFunc[])(
      const int32_t*, TransformSize, int, uint16_t) = {
      &Tile::GetCoeffBaseContext2D, &Tile::GetCoeffBaseContextHorizontal,
      &Tile::GetCoeffBaseContextVertical};
  auto get_coeff_base_context_func = kGetCoeffBaseContextFunc[tx_class];
  // Lookup used to call the right variant of GetCoeffBaseRangeContext*() based
  // on the transform class.
  static constexpr int (Tile::*kGetCoeffBaseRangeContextFunc[])(
      const int32_t*, int, int) = {&Tile::GetCoeffBaseRangeContext2D,
                                   &Tile::GetCoeffBaseRangeContextHorizontal,
                                   &Tile::GetCoeffBaseRangeContextVertical};
  auto get_coeff_base_range_context_func =
      kGetCoeffBaseRangeContextFunc[tx_class];
  // Read all the other coefficients.
  for (int i = eob - 2; i >= 0; --i) {
    const uint16_t pos = scan[i];
    context = (this->*get_coeff_base_context_func)(quantized, tx_size,
                                                   adjusted_tx_width_log2, pos);
    int level = reader_.ReadSymbol<kCoeffBaseSymbolCount>(
        symbol_decoder_context_
            .coeff_base_cdf[tx_size_context][plane_type][context]);
    if (level > kNumQuantizerBaseLevels) {
      level += ReadCoeffBaseRange(clamped_tx_size_context,
                                  (this->*get_coeff_base_range_context_func)(
                                      quantized, adjusted_tx_width_log2, pos),
                                  plane_type);
    }
    quantized[PaddedIndex(pos, adjusted_tx_width_log2)] = level;
  }
  const int max_value = (1 << (7 + sequence_header_.color_config.bitdepth)) - 1;
  const int current_quantizer_index = GetQIndex(
      frame_header_.segmentation, bp.segment_id, current_quantizer_index_);
  const int dc_q_value = quantizer_.GetDcValue(plane, current_quantizer_index);
  const int ac_q_value = quantizer_.GetAcValue(plane, current_quantizer_index);
  const int shift = GetQuantizationShift(tx_size);
  const uint8_t* const quantizer_matrix =
      (frame_header_.quantizer.use_matrix &&
       *tx_type < kTransformTypeIdentityIdentity &&
       !frame_header_.segmentation.lossless[bp.segment_id] &&
       frame_header_.quantizer.matrix_level[plane] < 15)
          ? &kQuantizerMatrix[frame_header_.quantizer.matrix_level[plane]]
                             [plane_type][kQuantizerMatrixOffset[tx_size]]
          : nullptr;
  int coefficient_level = 0;
  int8_t dc_category = 0;
  uint16_t* const dc_sign_cdf =
      (quantized[0] != 0)
          ? symbol_decoder_context_.dc_sign_cdf[plane_type][GetDcSignContext(
                x4, y4, w4, h4, plane)]
          : nullptr;
  assert(scan[0] == 0);
  if (!ReadSignAndApplyDequantization</*is_dc_coefficient=*/true>(
          block, quantized, scan, 0, adjusted_tx_width_log2, tx_width,
          dc_q_value, quantizer_matrix, shift, max_value, dc_sign_cdf,
          &dc_category, &coefficient_level)) {
    return -1;
  }
  for (int i = 1; i < eob; ++i) {
    if (!ReadSignAndApplyDequantization</*is_dc_coefficient=*/false>(
            block, quantized, scan, i, adjusted_tx_width_log2, tx_width,
            ac_q_value, quantizer_matrix, shift, max_value, nullptr, nullptr,
            &coefficient_level)) {
      return -1;
    }
  }
  SetEntropyContexts(x4, y4, w4, h4, plane, std::min(4, coefficient_level),
                     dc_category);
  if (split_parse_and_decode_) {
    *block.residual += tx_width * tx_height * residual_size_;
  }
  return eob;
}

// CALL_BITDEPTH_FUNCTION is a macro that calls the appropriate template
// |function| depending on the value of |sequence_header_.color_config.bitdepth|
// with the variadic arguments.
#if LIBGAV1_MAX_BITDEPTH >= 10
#define CALL_BITDEPTH_FUNCTION(function, ...)         \
  do {                                                \
    if (sequence_header_.color_config.bitdepth > 8) { \
      function<uint16_t>(__VA_ARGS__);                \
    } else {                                          \
      function<uint8_t>(__VA_ARGS__);                 \
    }                                                 \
  } while (false)
#else
#define CALL_BITDEPTH_FUNCTION(function, ...) \
  do {                                        \
    function<uint8_t>(__VA_ARGS__);           \
  } while (false)
#endif

bool Tile::TransformBlock(const Block& block, Plane plane, int base_x,
                          int base_y, TransformSize tx_size, int x, int y,
                          ProcessingMode mode) {
  BlockParameters& bp = *block.bp;
  const int subsampling_x = subsampling_x_[plane];
  const int subsampling_y = subsampling_y_[plane];
  const int start_x = base_x + MultiplyBy4(x);
  const int start_y = base_y + MultiplyBy4(y);
  const int max_x = MultiplyBy4(frame_header_.columns4x4) >> subsampling_x;
  const int max_y = MultiplyBy4(frame_header_.rows4x4) >> subsampling_y;
  if (start_x >= max_x || start_y >= max_y) return true;
  const int row = DivideBy4(start_y << subsampling_y);
  const int column = DivideBy4(start_x << subsampling_x);
  const int mask = sequence_header_.use_128x128_superblock ? 31 : 15;
  const int sub_block_row4x4 = row & mask;
  const int sub_block_column4x4 = column & mask;
  const int step_x = kTransformWidth4x4[tx_size];
  const int step_y = kTransformHeight4x4[tx_size];
  const bool do_decode = mode == kProcessingModeDecodeOnly ||
                         mode == kProcessingModeParseAndDecode;
  if (do_decode && !bp.is_inter) {
    if (bp.palette_mode_info.size[GetPlaneType(plane)] > 0) {
      CALL_BITDEPTH_FUNCTION(PalettePrediction, block, plane, start_x, start_y,
                             x, y, tx_size);
    } else {
      const PredictionMode mode =
          (plane == kPlaneY)
              ? bp.y_mode
              : (bp.uv_mode == kPredictionModeChromaFromLuma ? kPredictionModeDc
                                                             : bp.uv_mode);
      const int tr_row4x4 = (sub_block_row4x4 >> subsampling_y);
      const int tr_column4x4 =
          (sub_block_column4x4 >> subsampling_x) + step_x + 1;
      const int bl_row4x4 = (sub_block_row4x4 >> subsampling_y) + step_y + 1;
      const int bl_column4x4 = (sub_block_column4x4 >> subsampling_x);
      const bool has_left =
          x > 0 || (plane == kPlaneY ? block.left_available
                                     : block.LeftAvailableChroma());
      const bool has_top =
          y > 0 ||
          (plane == kPlaneY ? block.top_available : block.TopAvailableChroma());

      CALL_BITDEPTH_FUNCTION(
          IntraPrediction, block, plane, start_x, start_y, has_left, has_top,
          block.scratch_buffer->block_decoded[plane][tr_row4x4][tr_column4x4],
          block.scratch_buffer->block_decoded[plane][bl_row4x4][bl_column4x4],
          mode, tx_size);
      if (plane != kPlaneY && bp.uv_mode == kPredictionModeChromaFromLuma) {
        CALL_BITDEPTH_FUNCTION(ChromaFromLumaPrediction, block, plane, start_x,
                               start_y, tx_size);
      }
    }
    if (plane == kPlaneY) {
      block.bp->prediction_parameters->max_luma_width =
          start_x + MultiplyBy4(step_x);
      block.bp->prediction_parameters->max_luma_height =
          start_y + MultiplyBy4(step_y);
      block.scratch_buffer->cfl_luma_buffer_valid = false;
    }
  }
  if (!bp.skip) {
    const int sb_row_index = SuperBlockRowIndex(block.row4x4);
    const int sb_column_index = SuperBlockColumnIndex(block.column4x4);
    switch (mode) {
      case kProcessingModeParseAndDecode: {
        TransformType tx_type;
        const int16_t non_zero_coeff_count = ReadTransformCoefficients(
            block, plane, start_x, start_y, tx_size, &tx_type);
        if (non_zero_coeff_count < 0) return false;
        ReconstructBlock(block, plane, start_x, start_y, tx_size, tx_type,
                         non_zero_coeff_count);
        break;
      }
      case kProcessingModeParseOnly: {
        TransformType tx_type;
        const int16_t non_zero_coeff_count = ReadTransformCoefficients(
            block, plane, start_x, start_y, tx_size, &tx_type);
        if (non_zero_coeff_count < 0) return false;
        residual_buffer_threaded_[sb_row_index][sb_column_index]
            ->transform_parameters()
            ->Push(non_zero_coeff_count, tx_type);
        break;
      }
      case kProcessingModeDecodeOnly: {
        TransformParameterQueue& tx_params =
            *residual_buffer_threaded_[sb_row_index][sb_column_index]
                 ->transform_parameters();
        ReconstructBlock(block, plane, start_x, start_y, tx_size,
                         tx_params.Type(), tx_params.NonZeroCoeffCount());
        tx_params.Pop();
        break;
      }
    }
  }
  if (do_decode) {
    bool* block_decoded =
        &block.scratch_buffer
             ->block_decoded[plane][(sub_block_row4x4 >> subsampling_y) + 1]
                            [(sub_block_column4x4 >> subsampling_x) + 1];
    for (int i = 0; i < step_y; ++i) {
      static_assert(sizeof(bool) == 1, "");
      memset(block_decoded, 1, step_x);
      block_decoded += DecoderScratchBuffer::kBlockDecodedStride;
    }
  }
  return true;
}

bool Tile::TransformTree(const Block& block, int start_x, int start_y,
                         BlockSize plane_size, ProcessingMode mode) {
  assert(plane_size <= kBlock64x64);
  // Branching factor is 4; Maximum Depth is 4; So the maximum stack size
  // required is (4 - 1) * 4 + 1 = 13.
  Stack<TransformTreeNode, 13> stack;
  // It is okay to cast BlockSize to TransformSize here since the enum are
  // equivalent for all BlockSize values <= kBlock64x64.
  stack.Push(TransformTreeNode(start_x, start_y,
                               static_cast<TransformSize>(plane_size)));

  do {
    TransformTreeNode node = stack.Pop();
    const int row = DivideBy4(node.y);
    const int column = DivideBy4(node.x);
    if (row >= frame_header_.rows4x4 || column >= frame_header_.columns4x4) {
      continue;
    }
    const TransformSize inter_tx_size = inter_transform_sizes_[row][column];
    const int width = kTransformWidth[node.tx_size];
    const int height = kTransformHeight[node.tx_size];
    if (width <= kTransformWidth[inter_tx_size] &&
        height <= kTransformHeight[inter_tx_size]) {
      if (!TransformBlock(block, kPlaneY, node.x, node.y, node.tx_size, 0, 0,
                          mode)) {
        return false;
      }
      continue;
    }
    // The split transform size look up gives the right transform size that we
    // should push in the stack.
    //   if (width > height) => transform size whose width is half.
    //   if (width < height) => transform size whose height is half.
    //   if (width == height) => transform size whose width and height are half.
    const TransformSize split_tx_size = kSplitTransformSize[node.tx_size];
    const int half_width = DivideBy2(width);
    if (width > height) {
      stack.Push(TransformTreeNode(node.x + half_width, node.y, split_tx_size));
      stack.Push(TransformTreeNode(node.x, node.y, split_tx_size));
      continue;
    }
    const int half_height = DivideBy2(height);
    if (width < height) {
      stack.Push(
          TransformTreeNode(node.x, node.y + half_height, split_tx_size));
      stack.Push(TransformTreeNode(node.x, node.y, split_tx_size));
      continue;
    }
    stack.Push(TransformTreeNode(node.x + half_width, node.y + half_height,
                                 split_tx_size));
    stack.Push(TransformTreeNode(node.x, node.y + half_height, split_tx_size));
    stack.Push(TransformTreeNode(node.x + half_width, node.y, split_tx_size));
    stack.Push(TransformTreeNode(node.x, node.y, split_tx_size));
  } while (!stack.Empty());
  return true;
}

void Tile::ReconstructBlock(const Block& block, Plane plane, int start_x,
                            int start_y, TransformSize tx_size,
                            TransformType tx_type,
                            int16_t non_zero_coeff_count) {
  // Reconstruction process. Steps 2 and 3 of Section 7.12.3 in the spec.
  assert(non_zero_coeff_count >= 0);
  if (non_zero_coeff_count == 0) return;
#if LIBGAV1_MAX_BITDEPTH >= 10
  if (sequence_header_.color_config.bitdepth > 8) {
    Array2DView<uint16_t> buffer(
        buffer_[plane].rows(), buffer_[plane].columns() / sizeof(uint16_t),
        reinterpret_cast<uint16_t*>(&buffer_[plane][0][0]));
    Reconstruct(dsp_, tx_type, tx_size,
                frame_header_.segmentation.lossless[block.bp->segment_id],
                reinterpret_cast<int32_t*>(*block.residual), start_x, start_y,
                &buffer, non_zero_coeff_count);
  } else  // NOLINT
#endif
  {
    Reconstruct(dsp_, tx_type, tx_size,
                frame_header_.segmentation.lossless[block.bp->segment_id],
                reinterpret_cast<int16_t*>(*block.residual), start_x, start_y,
                &buffer_[plane], non_zero_coeff_count);
  }
  if (split_parse_and_decode_) {
    *block.residual +=
        kTransformWidth[tx_size] * kTransformHeight[tx_size] * residual_size_;
  }
}

bool Tile::Residual(const Block& block, ProcessingMode mode) {
  const int width_chunks = std::max(1, block.width >> 6);
  const int height_chunks = std::max(1, block.height >> 6);
  const BlockSize size_chunk4x4 =
      (width_chunks > 1 || height_chunks > 1) ? kBlock64x64 : block.size;
  const BlockParameters& bp = *block.bp;
  for (int chunk_y = 0; chunk_y < height_chunks; ++chunk_y) {
    for (int chunk_x = 0; chunk_x < width_chunks; ++chunk_x) {
      for (int plane = 0; plane < (block.HasChroma() ? PlaneCount() : 1);
           ++plane) {
        const int subsampling_x = subsampling_x_[plane];
        const int subsampling_y = subsampling_y_[plane];
        // For Y Plane, when lossless is true |bp.transform_size| is always
        // kTransformSize4x4. So we can simply use |bp.transform_size| here as
        // the Y plane's transform size (part of Section 5.11.37 in the spec).
        const TransformSize tx_size =
            (plane == kPlaneY) ? bp.transform_size : bp.uv_transform_size;
        const BlockSize plane_size =
            kPlaneResidualSize[size_chunk4x4][subsampling_x][subsampling_y];
        assert(plane_size != kBlockInvalid);
        if (bp.is_inter &&
            !frame_header_.segmentation.lossless[bp.segment_id] &&
            plane == kPlaneY) {
          const int row_chunk4x4 = block.row4x4 + MultiplyBy16(chunk_y);
          const int column_chunk4x4 = block.column4x4 + MultiplyBy16(chunk_x);
          const int base_x = MultiplyBy4(column_chunk4x4 >> subsampling_x);
          const int base_y = MultiplyBy4(row_chunk4x4 >> subsampling_y);
          if (!TransformTree(block, base_x, base_y, plane_size, mode)) {
            return false;
          }
        } else {
          const int base_x = MultiplyBy4(block.column4x4 >> subsampling_x);
          const int base_y = MultiplyBy4(block.row4x4 >> subsampling_y);
          const int step_x = kTransformWidth4x4[tx_size];
          const int step_y = kTransformHeight4x4[tx_size];
          const int num4x4_wide = kNum4x4BlocksWide[plane_size];
          const int num4x4_high = kNum4x4BlocksHigh[plane_size];
          for (int y = 0; y < num4x4_high; y += step_y) {
            for (int x = 0; x < num4x4_wide; x += step_x) {
              if (!TransformBlock(
                      block, static_cast<Plane>(plane), base_x, base_y, tx_size,
                      x + (MultiplyBy16(chunk_x) >> subsampling_x),
                      y + (MultiplyBy16(chunk_y) >> subsampling_y), mode)) {
                return false;
              }
            }
          }
        }
      }
    }
  }
  return true;
}

// The purpose of this function is to limit the maximum size of motion vectors
// and also, if use_intra_block_copy is true, to additionally constrain the
// motion vector so that the data is fetched from parts of the tile that have
// already been decoded and are not too close to the current block (in order to
// make a pipelined decoder implementation feasible).
bool Tile::IsMvValid(const Block& block, bool is_compound) const {
  const BlockParameters& bp = *block.bp;
  for (int i = 0; i < 1 + static_cast<int>(is_compound); ++i) {
    for (int mv_component : bp.mv[i].mv) {
      if (std::abs(mv_component) >= (1 << 14)) {
        return false;
      }
    }
  }
  if (!block.bp->prediction_parameters->use_intra_block_copy) {
    return true;
  }
  if ((bp.mv[0].mv[0] & 7) != 0 || (bp.mv[0].mv[1] & 7) != 0) {
    return false;
  }
  const int delta_row = bp.mv[0].mv[0] >> 3;
  const int delta_column = bp.mv[0].mv[1] >> 3;
  int src_top_edge = MultiplyBy4(block.row4x4) + delta_row;
  int src_left_edge = MultiplyBy4(block.column4x4) + delta_column;
  const int src_bottom_edge = src_top_edge + block.height;
  const int src_right_edge = src_left_edge + block.width;
  if (block.HasChroma()) {
    if (block.width < 8 && subsampling_x_[kPlaneU] != 0) {
      src_left_edge -= 4;
    }
    if (block.height < 8 && subsampling_y_[kPlaneU] != 0) {
      src_top_edge -= 4;
    }
  }
  if (src_top_edge < MultiplyBy4(row4x4_start_) ||
      src_left_edge < MultiplyBy4(column4x4_start_) ||
      src_bottom_edge > MultiplyBy4(row4x4_end_) ||
      src_right_edge > MultiplyBy4(column4x4_end_)) {
    return false;
  }
  // sb_height_log2 = use_128x128_superblock ? log2(128) : log2(64)
  const int sb_height_log2 =
      6 + static_cast<int>(sequence_header_.use_128x128_superblock);
  const int active_sb_row = MultiplyBy4(block.row4x4) >> sb_height_log2;
  const int active_64x64_block_column = MultiplyBy4(block.column4x4) >> 6;
  const int src_sb_row = (src_bottom_edge - 1) >> sb_height_log2;
  const int src_64x64_block_column = (src_right_edge - 1) >> 6;
  const int total_64x64_blocks_per_row =
      ((column4x4_end_ - column4x4_start_ - 1) >> 4) + 1;
  const int active_64x64_block =
      active_sb_row * total_64x64_blocks_per_row + active_64x64_block_column;
  const int src_64x64_block =
      src_sb_row * total_64x64_blocks_per_row + src_64x64_block_column;
  if (src_64x64_block >= active_64x64_block - kIntraBlockCopyDelay64x64Blocks) {
    return false;
  }

  // Wavefront constraint: use only top left area of frame for reference.
  if (src_sb_row > active_sb_row) return false;
  const int gradient =
      1 + kIntraBlockCopyDelay64x64Blocks +
      static_cast<int>(sequence_header_.use_128x128_superblock);
  const int wavefront_offset = gradient * (active_sb_row - src_sb_row);
  return src_64x64_block_column < active_64x64_block_column -
                                      kIntraBlockCopyDelay64x64Blocks +
                                      wavefront_offset;
}

bool Tile::AssignMv(const Block& block, bool is_compound) {
  MotionVector predicted_mv[2] = {};
  BlockParameters& bp = *block.bp;
  for (int i = 0; i < 1 + static_cast<int>(is_compound); ++i) {
    const PredictionParameters& prediction_parameters =
        *block.bp->prediction_parameters;
    const PredictionMode mode = prediction_parameters.use_intra_block_copy
                                    ? kPredictionModeNewMv
                                    : GetSinglePredictionMode(i, bp.y_mode);
    if (prediction_parameters.use_intra_block_copy) {
      predicted_mv[0] = prediction_parameters.ref_mv_stack[0].mv[0];
      if (predicted_mv[0].mv32 == 0) {
        predicted_mv[0] = prediction_parameters.ref_mv_stack[1].mv[0];
      }
      if (predicted_mv[0].mv32 == 0) {
        const int super_block_size4x4 = kNum4x4BlocksHigh[SuperBlockSize()];
        if (block.row4x4 - super_block_size4x4 < row4x4_start_) {
          predicted_mv[0].mv[1] = -MultiplyBy8(
              MultiplyBy4(super_block_size4x4) + kIntraBlockCopyDelayPixels);
        } else {
          predicted_mv[0].mv[0] = -MultiplyBy32(super_block_size4x4);
        }
      }
    } else if (mode == kPredictionModeGlobalMv) {
      predicted_mv[i] = prediction_parameters.global_mv[i];
    } else {
      const int ref_mv_index = (mode == kPredictionModeNearestMv ||
                                (mode == kPredictionModeNewMv &&
                                 prediction_parameters.ref_mv_count <= 1))
                                   ? 0
                                   : prediction_parameters.ref_mv_index;
      predicted_mv[i] = prediction_parameters.ref_mv_stack[ref_mv_index].mv[i];
    }
    if (mode == kPredictionModeNewMv) {
      ReadMotionVector(block, i);
      bp.mv[i].mv[0] += predicted_mv[i].mv[0];
      bp.mv[i].mv[1] += predicted_mv[i].mv[1];
    } else {
      bp.mv[i] = predicted_mv[i];
    }
  }
  return IsMvValid(block, is_compound);
}

void Tile::ResetEntropyContext(const Block& block) {
  for (int plane = 0; plane < (block.HasChroma() ? PlaneCount() : 1); ++plane) {
    const int subsampling_x = subsampling_x_[plane];
    const int start_x = block.column4x4 >> subsampling_x;
    const int end_x =
        std::min((block.column4x4 + block.width4x4) >> subsampling_x,
                 frame_header_.columns4x4);
    memset(&coefficient_levels_[kEntropyContextTop][plane][start_x], 0,
           end_x - start_x);
    memset(&dc_categories_[kEntropyContextTop][plane][start_x], 0,
           end_x - start_x);
    const int subsampling_y = subsampling_y_[plane];
    const int start_y = block.row4x4 >> subsampling_y;
    const int end_y =
        std::min((block.row4x4 + block.height4x4) >> subsampling_y,
                 frame_header_.rows4x4);
    memset(&coefficient_levels_[kEntropyContextLeft][plane][start_y], 0,
           end_y - start_y);
    memset(&dc_categories_[kEntropyContextLeft][plane][start_y], 0,
           end_y - start_y);
  }
}

void Tile::ComputePrediction(const Block& block) {
  const int mask =
      (1 << (4 + static_cast<int>(sequence_header_.use_128x128_superblock))) -
      1;
  const int sub_block_row4x4 = block.row4x4 & mask;
  const int sub_block_column4x4 = block.column4x4 & mask;
  // Returns true if this block applies local warping. The state is determined
  // in the Y plane and carried for use in the U/V planes.
  // But the U/V planes will not apply warping when the block size is smaller
  // than 8x8, even if this variable is true.
  bool is_local_valid = false;
  // Local warping parameters, similar usage as is_local_valid.
  GlobalMotion local_warp_params;
  for (int plane = 0; plane < (block.HasChroma() ? PlaneCount() : 1); ++plane) {
    const int8_t subsampling_x = subsampling_x_[plane];
    const int8_t subsampling_y = subsampling_y_[plane];
    const BlockSize plane_size =
        block.residual_size[GetPlaneType(static_cast<Plane>(plane))];
    const int block_width4x4 = kNum4x4BlocksWide[plane_size];
    const int block_height4x4 = kNum4x4BlocksHigh[plane_size];
    const int block_width = MultiplyBy4(block_width4x4);
    const int block_height = MultiplyBy4(block_height4x4);
    const int base_x = MultiplyBy4(block.column4x4 >> subsampling_x);
    const int base_y = MultiplyBy4(block.row4x4 >> subsampling_y);
    const BlockParameters& bp = *block.bp;
    if (bp.is_inter && bp.reference_frame[1] == kReferenceFrameIntra) {
      const int tr_row4x4 = (sub_block_row4x4 >> subsampling_y);
      const int tr_column4x4 =
          (sub_block_column4x4 >> subsampling_x) + block_width4x4 + 1;
      const int bl_row4x4 =
          (sub_block_row4x4 >> subsampling_y) + block_height4x4;
      const int bl_column4x4 = (sub_block_column4x4 >> subsampling_x) + 1;
      const TransformSize tx_size =
          k4x4SizeToTransformSize[k4x4WidthLog2[plane_size]]
                                 [k4x4HeightLog2[plane_size]];
      const bool has_left =
          plane == kPlaneY ? block.left_available : block.LeftAvailableChroma();
      const bool has_top =
          plane == kPlaneY ? block.top_available : block.TopAvailableChroma();
      CALL_BITDEPTH_FUNCTION(
          IntraPrediction, block, static_cast<Plane>(plane), base_x, base_y,
          has_left, has_top,
          block.scratch_buffer->block_decoded[plane][tr_row4x4][tr_column4x4],
          block.scratch_buffer->block_decoded[plane][bl_row4x4][bl_column4x4],
          kInterIntraToIntraMode[block.bp->prediction_parameters
                                     ->inter_intra_mode],
          tx_size);
    }
    if (bp.is_inter) {
      int candidate_row = (block.row4x4 >> subsampling_y) << subsampling_y;
      int candidate_column = (block.column4x4 >> subsampling_x)
                             << subsampling_x;
      bool some_use_intra = false;
      for (int r = 0; r < (block_height4x4 << subsampling_y); ++r) {
        for (int c = 0; c < (block_width4x4 << subsampling_x); ++c) {
          auto* const bp = block_parameters_holder_.Find(candidate_row + r,
                                                         candidate_column + c);
          if (bp != nullptr && bp->reference_frame[0] == kReferenceFrameIntra) {
            some_use_intra = true;
            break;
          }
        }
        if (some_use_intra) break;
      }
      int prediction_width;
      int prediction_height;
      if (some_use_intra) {
        candidate_row = block.row4x4;
        candidate_column = block.column4x4;
        prediction_width = block_width;
        prediction_height = block_height;
      } else {
        prediction_width = block.width >> subsampling_x;
        prediction_height = block.height >> subsampling_y;
      }
      for (int r = 0, y = 0; y < block_height; y += prediction_height, ++r) {
        for (int c = 0, x = 0; x < block_width; x += prediction_width, ++c) {
          InterPrediction(block, static_cast<Plane>(plane), base_x + x,
                          base_y + y, prediction_width, prediction_height,
                          candidate_row + r, candidate_column + c,
                          &is_local_valid, &local_warp_params);
        }
      }
    }
  }
}

#undef CALL_BITDEPTH_FUNCTION

void Tile::PopulateDeblockFilterLevel(const Block& block) {
  if (!post_filter_.DoDeblock()) return;
  BlockParameters& bp = *block.bp;
  for (int i = 0; i < kFrameLfCount; ++i) {
    if (delta_lf_all_zero_) {
      bp.deblock_filter_level[i] = post_filter_.GetZeroDeltaDeblockFilterLevel(
          bp.segment_id, i, bp.reference_frame[0],
          LoopFilterMask::GetModeId(bp.y_mode));
    } else {
      bp.deblock_filter_level[i] =
          deblock_filter_levels_[bp.segment_id][i][bp.reference_frame[0]]
                                [LoopFilterMask::GetModeId(bp.y_mode)];
    }
  }
}

bool Tile::ProcessBlock(int row4x4, int column4x4, BlockSize block_size,
                        ParameterTree* const tree,
                        DecoderScratchBuffer* const scratch_buffer,
                        ResidualPtr* residual) {
  // Do not process the block if the starting point is beyond the visible frame.
  // This is equivalent to the has_row/has_column check in the
  // decode_partition() section of the spec when partition equals
  // kPartitionHorizontal or kPartitionVertical.
  if (row4x4 >= frame_header_.rows4x4 ||
      column4x4 >= frame_header_.columns4x4) {
    return true;
  }
  Block block(*this, row4x4, column4x4, block_size, scratch_buffer, residual,
              tree->parameters());
  block.bp->size = block_size;
  block_parameters_holder_.FillCache(row4x4, column4x4, block_size,
                                     tree->parameters());
  block.bp->prediction_parameters =
      split_parse_and_decode_ ? std::unique_ptr<PredictionParameters>(
                                    new (std::nothrow) PredictionParameters())
                              : std::move(prediction_parameters_);
  if (block.bp->prediction_parameters == nullptr) return false;
  if (!DecodeModeInfo(block)) return false;
  PopulateDeblockFilterLevel(block);
  if (!ReadPaletteTokens(block)) return false;
  DecodeTransformSize(block);
  BlockParameters& bp = *block.bp;
  // Part of Section 5.11.37 in the spec (implemented as a simple lookup).
  bp.uv_transform_size =
      frame_header_.segmentation.lossless[bp.segment_id]
          ? kTransformSize4x4
          : kUVTransformSize[block.residual_size[kPlaneTypeUV]];
  if (bp.skip) ResetEntropyContext(block);
  if (split_parse_and_decode_) {
    if (!Residual(block, kProcessingModeParseOnly)) return false;
  } else {
    ComputePrediction(block);
    if (!Residual(block, kProcessingModeParseAndDecode)) return false;
  }
  // If frame_header_.segmentation.enabled is false, bp.segment_id is 0 for all
  // blocks. We don't need to call save bp.segment_id in the current frame
  // because the current frame's segmentation map will be cleared to all 0s.
  //
  // If frame_header_.segmentation.enabled is true and
  // frame_header_.segmentation.update_map is false, we will copy the previous
  // frame's segmentation map to the current frame. So we don't need to call
  // save bp.segment_id in the current frame.
  if (frame_header_.segmentation.enabled &&
      frame_header_.segmentation.update_map) {
    const int x_limit = std::min(frame_header_.columns4x4 - column4x4,
                                 static_cast<int>(block.width4x4));
    const int y_limit = std::min(frame_header_.rows4x4 - row4x4,
                                 static_cast<int>(block.height4x4));
    current_frame_.segmentation_map()->FillBlock(row4x4, column4x4, x_limit,
                                                 y_limit, bp.segment_id);
  }
  if (kDeblockFilterBitMask &&
      (build_bit_mask_when_parsing_ || !split_parse_and_decode_)) {
    BuildBitMask(block);
  }
  if (!split_parse_and_decode_) {
    StoreMotionFieldMvsIntoCurrentFrame(block);
    prediction_parameters_ = std::move(block.bp->prediction_parameters);
  }
  return true;
}

bool Tile::DecodeBlock(ParameterTree* const tree,
                       DecoderScratchBuffer* const scratch_buffer,
                       ResidualPtr* residual) {
  const int row4x4 = tree->row4x4();
  const int column4x4 = tree->column4x4();
  if (row4x4 >= frame_header_.rows4x4 ||
      column4x4 >= frame_header_.columns4x4) {
    return true;
  }
  const BlockSize block_size = tree->block_size();
  Block block(*this, row4x4, column4x4, block_size, scratch_buffer, residual,
              tree->parameters());
  ComputePrediction(block);
  if (!Residual(block, kProcessingModeDecodeOnly)) return false;
  if (kDeblockFilterBitMask && !build_bit_mask_when_parsing_) {
    BuildBitMask(block);
  }
  StoreMotionFieldMvsIntoCurrentFrame(block);
  block.bp->prediction_parameters.reset(nullptr);
  return true;
}

bool Tile::ProcessPartition(int row4x4_start, int column4x4_start,
                            ParameterTree* const root,
                            DecoderScratchBuffer* const scratch_buffer,
                            ResidualPtr* residual) {
  Stack<ParameterTree*, kDfsStackSize> stack;

  // Set up the first iteration.
  ParameterTree* node = root;
  int row4x4 = row4x4_start;
  int column4x4 = column4x4_start;
  BlockSize block_size = SuperBlockSize();

  // DFS loop. If it sees a terminal node (leaf node), ProcessBlock is invoked.
  // Otherwise, the children are pushed into the stack for future processing.
  do {
    if (!stack.Empty()) {
      // Set up subsequent iterations.
      node = stack.Pop();
      row4x4 = node->row4x4();
      column4x4 = node->column4x4();
      block_size = node->block_size();
    }
    if (row4x4 >= frame_header_.rows4x4 ||
        column4x4 >= frame_header_.columns4x4) {
      continue;
    }
    const int block_width4x4 = kNum4x4BlocksWide[block_size];
    assert(block_width4x4 == kNum4x4BlocksHigh[block_size]);
    const int half_block4x4 = block_width4x4 >> 1;
    const bool has_rows = (row4x4 + half_block4x4) < frame_header_.rows4x4;
    const bool has_columns =
        (column4x4 + half_block4x4) < frame_header_.columns4x4;
    Partition partition;
    if (!ReadPartition(row4x4, column4x4, block_size, has_rows, has_columns,
                       &partition)) {
      LIBGAV1_DLOG(ERROR, "Failed to read partition for row: %d column: %d",
                   row4x4, column4x4);
      return false;
    }
    const BlockSize sub_size = kSubSize[partition][block_size];
    // Section 6.10.4: It is a requirement of bitstream conformance that
    // get_plane_residual_size( subSize, 1 ) is not equal to BLOCK_INVALID
    // every time subSize is computed.
    if (sub_size == kBlockInvalid ||
        kPlaneResidualSize[sub_size]
                          [sequence_header_.color_config.subsampling_x]
                          [sequence_header_.color_config.subsampling_y] ==
            kBlockInvalid) {
      LIBGAV1_DLOG(
          ERROR,
          "Invalid sub-block/plane size for row: %d column: %d partition: "
          "%d block_size: %d sub_size: %d subsampling_x/y: %d, %d",
          row4x4, column4x4, partition, block_size, sub_size,
          sequence_header_.color_config.subsampling_x,
          sequence_header_.color_config.subsampling_y);
      return false;
    }
    if (!node->SetPartitionType(partition)) {
      LIBGAV1_DLOG(ERROR, "node->SetPartitionType() failed.");
      return false;
    }
    switch (partition) {
      case kPartitionNone:
        if (!ProcessBlock(row4x4, column4x4, sub_size, node, scratch_buffer,
                          residual)) {
          return false;
        }
        break;
      case kPartitionSplit:
        // The children must be added in reverse order since a stack is being
        // used.
        for (int i = 3; i >= 0; --i) {
          ParameterTree* const child = node->children(i);
          assert(child != nullptr);
          stack.Push(child);
        }
        break;
      case kPartitionHorizontal:
      case kPartitionVertical:
      case kPartitionHorizontalWithTopSplit:
      case kPartitionHorizontalWithBottomSplit:
      case kPartitionVerticalWithLeftSplit:
      case kPartitionVerticalWithRightSplit:
      case kPartitionHorizontal4:
      case kPartitionVertical4:
        for (int i = 0; i < 4; ++i) {
          ParameterTree* const child = node->children(i);
          // Once a null child is seen, all the subsequent children will also be
          // null.
          if (child == nullptr) break;
          if (!ProcessBlock(child->row4x4(), child->column4x4(),
                            child->block_size(), child, scratch_buffer,
                            residual)) {
            return false;
          }
        }
        break;
    }
  } while (!stack.Empty());
  return true;
}

void Tile::ResetLoopRestorationParams() {
  for (int plane = kPlaneY; plane < kMaxPlanes; ++plane) {
    for (int i = WienerInfo::kVertical; i <= WienerInfo::kHorizontal; ++i) {
      reference_unit_info_[plane].sgr_proj_info.multiplier[i] =
          kSgrProjDefaultMultiplier[i];
      for (int j = 0; j < kNumWienerCoefficients; ++j) {
        reference_unit_info_[plane].wiener_info.filter[i][j] =
            kWienerDefaultFilter[j];
      }
    }
  }
}

void Tile::ResetCdef(const int row4x4, const int column4x4) {
  if (!sequence_header_.enable_cdef) return;
  const int row = DivideBy16(row4x4);
  const int column = DivideBy16(column4x4);
  cdef_index_[row][column] = -1;
  if (sequence_header_.use_128x128_superblock) {
    const int cdef_size4x4 = kNum4x4BlocksWide[kBlock64x64];
    const int border_row = DivideBy16(row4x4 + cdef_size4x4);
    const int border_column = DivideBy16(column4x4 + cdef_size4x4);
    cdef_index_[row][border_column] = -1;
    cdef_index_[border_row][column] = -1;
    cdef_index_[border_row][border_column] = -1;
  }
}

void Tile::ClearBlockDecoded(DecoderScratchBuffer* const scratch_buffer,
                             int row4x4, int column4x4) {
  // Set everything to false.
  memset(scratch_buffer->block_decoded, 0,
         sizeof(scratch_buffer->block_decoded));
  // Set specific edge cases to true.
  const int sb_size4 = sequence_header_.use_128x128_superblock ? 32 : 16;
  for (int plane = 0; plane < PlaneCount(); ++plane) {
    const int subsampling_x = subsampling_x_[plane];
    const int subsampling_y = subsampling_y_[plane];
    const int sb_width4 = (column4x4_end_ - column4x4) >> subsampling_x;
    const int sb_height4 = (row4x4_end_ - row4x4) >> subsampling_y;
    // The memset is equivalent to the following lines in the spec:
    // for ( x = -1; x <= ( sbSize4 >> subX ); x++ ) {
    //   if ( y < 0 && x < sbWidth4 ) {
    //     BlockDecoded[plane][y][x] = 1
    //   }
    // }
    const int num_elements =
        std::min((sb_size4 >> subsampling_x_[plane]) + 1, sb_width4) + 1;
    memset(&scratch_buffer->block_decoded[plane][0][0], 1, num_elements);
    // The for loop is equivalent to the following lines in the spec:
    // for ( y = -1; y <= ( sbSize4 >> subY ); y++ )
    //   if ( x < 0 && y < sbHeight4 )
    //     BlockDecoded[plane][y][x] = 1
    //   }
    // }
    // BlockDecoded[plane][sbSize4 >> subY][-1] = 0
    for (int y = -1; y < std::min((sb_size4 >> subsampling_y), sb_height4);
         ++y) {
      scratch_buffer->block_decoded[plane][y + 1][0] = true;
    }
  }
}

bool Tile::ProcessSuperBlock(int row4x4, int column4x4, int block_width4x4,
                             DecoderScratchBuffer* const scratch_buffer,
                             ProcessingMode mode) {
  const bool parsing =
      mode == kProcessingModeParseOnly || mode == kProcessingModeParseAndDecode;
  const bool decoding = mode == kProcessingModeDecodeOnly ||
                        mode == kProcessingModeParseAndDecode;
  if (parsing) {
    read_deltas_ = frame_header_.delta_q.present;
    ResetCdef(row4x4, column4x4);
  }
  if (decoding) {
    ClearBlockDecoded(scratch_buffer, row4x4, column4x4);
  }
  const BlockSize block_size = SuperBlockSize();
  if (parsing) {
    ReadLoopRestorationCoefficients(row4x4, column4x4, block_size);
  }
  const int row = row4x4 / block_width4x4;
  const int column = column4x4 / block_width4x4;
  if (parsing && decoding) {
    uint8_t* residual_buffer = residual_buffer_.get();
    if (!ProcessPartition(row4x4, column4x4,
                          block_parameters_holder_.Tree(row, column),
                          scratch_buffer, &residual_buffer)) {
      LIBGAV1_DLOG(ERROR, "Error decoding partition row: %d column: %d", row4x4,
                   column4x4);
      return false;
    }
    return true;
  }
  const int sb_row_index = SuperBlockRowIndex(row4x4);
  const int sb_column_index = SuperBlockColumnIndex(column4x4);
  if (parsing) {
    residual_buffer_threaded_[sb_row_index][sb_column_index] =
        residual_buffer_pool_->Get();
    if (residual_buffer_threaded_[sb_row_index][sb_column_index] == nullptr) {
      LIBGAV1_DLOG(ERROR, "Failed to get residual buffer.");
      return false;
    }
    uint8_t* residual_buffer =
        residual_buffer_threaded_[sb_row_index][sb_column_index]->buffer();
    if (!ProcessPartition(row4x4, column4x4,
                          block_parameters_holder_.Tree(row, column),
                          scratch_buffer, &residual_buffer)) {
      LIBGAV1_DLOG(ERROR, "Error parsing partition row: %d column: %d", row4x4,
                   column4x4);
      return false;
    }
  } else {
    uint8_t* residual_buffer =
        residual_buffer_threaded_[sb_row_index][sb_column_index]->buffer();
    if (!DecodeSuperBlock(block_parameters_holder_.Tree(row, column),
                          scratch_buffer, &residual_buffer)) {
      LIBGAV1_DLOG(ERROR, "Error decoding superblock row: %d column: %d",
                   row4x4, column4x4);
      return false;
    }
    residual_buffer_pool_->Release(
        std::move(residual_buffer_threaded_[sb_row_index][sb_column_index]));
  }
  return true;
}

bool Tile::DecodeSuperBlock(ParameterTree* const tree,
                            DecoderScratchBuffer* const scratch_buffer,
                            ResidualPtr* residual) {
  Stack<ParameterTree*, kDfsStackSize> stack;
  stack.Push(tree);
  do {
    ParameterTree* const node = stack.Pop();
    if (node->partition() != kPartitionNone) {
      for (int i = 3; i >= 0; --i) {
        if (node->children(i) == nullptr) continue;
        stack.Push(node->children(i));
      }
      continue;
    }
    if (!DecodeBlock(node, scratch_buffer, residual)) {
      LIBGAV1_DLOG(ERROR, "Error decoding block row: %d column: %d",
                   node->row4x4(), node->column4x4());
      return false;
    }
  } while (!stack.Empty());
  return true;
}

void Tile::ReadLoopRestorationCoefficients(int row4x4, int column4x4,
                                           BlockSize block_size) {
  if (frame_header_.allow_intrabc) return;
  LoopRestorationInfo* const restoration_info = post_filter_.restoration_info();
  const bool is_superres_scaled =
      frame_header_.width != frame_header_.upscaled_width;
  for (int plane = kPlaneY; plane < PlaneCount(); ++plane) {
    LoopRestorationUnitInfo unit_info;
    if (restoration_info->PopulateUnitInfoForSuperBlock(
            static_cast<Plane>(plane), block_size, is_superres_scaled,
            frame_header_.superres_scale_denominator, row4x4, column4x4,
            &unit_info)) {
      for (int unit_row = unit_info.row_start; unit_row < unit_info.row_end;
           ++unit_row) {
        for (int unit_column = unit_info.column_start;
             unit_column < unit_info.column_end; ++unit_column) {
          const int unit_id = unit_row * restoration_info->num_horizontal_units(
                                             static_cast<Plane>(plane)) +
                              unit_column;
          restoration_info->ReadUnitCoefficients(
              &reader_, &symbol_decoder_context_, static_cast<Plane>(plane),
              unit_id, &reference_unit_info_);
        }
      }
    }
  }
}

void Tile::BuildBitMask(const Block& block) {
  if (!post_filter_.DoDeblock()) return;
  if (block.size <= kBlock64x64) {
    BuildBitMaskHelper(block, block.row4x4, block.column4x4, block.size, true,
                       true);
  } else {
    const int block_width4x4 = kNum4x4BlocksWide[block.size];
    const int block_height4x4 = kNum4x4BlocksHigh[block.size];
    for (int y = 0; y < block_height4x4; y += 16) {
      for (int x = 0; x < block_width4x4; x += 16) {
        BuildBitMaskHelper(block, block.row4x4 + y, block.column4x4 + x,
                           kBlock64x64, x == 0, y == 0);
      }
    }
  }
}

void Tile::BuildBitMaskHelper(const Block& block, int row4x4, int column4x4,
                              BlockSize block_size,
                              const bool is_vertical_block_border,
                              const bool is_horizontal_block_border) {
  const int block_width4x4 = kNum4x4BlocksWide[block_size];
  const int block_height4x4 = kNum4x4BlocksHigh[block_size];
  BlockParameters& bp = *block.bp;
  const bool skip = bp.skip && bp.is_inter;
  LoopFilterMask* const masks = post_filter_.masks();
  const int unit_id = DivideBy16(row4x4) * masks->num_64x64_blocks_per_row() +
                      DivideBy16(column4x4);

  for (int plane = kPlaneY; plane < PlaneCount(); ++plane) {
    // For U and V planes, do not build bit masks if level == 0.
    if (plane > kPlaneY && frame_header_.loop_filter.level[plane + 1] == 0) {
      continue;
    }
    // Build bit mask for vertical edges.
    const int subsampling_x = subsampling_x_[plane];
    const int subsampling_y = subsampling_y_[plane];
    const int column_limit =
        std::min(column4x4 + block_width4x4, deblock_column_limit_[plane]);
    const int row_limit =
        std::min(row4x4 + block_height4x4, deblock_row_limit_[plane]);
    const int row_start = GetDeblockPosition(row4x4, subsampling_y);
    const int column_start = GetDeblockPosition(column4x4, subsampling_x);
    if (row_start >= row_limit || column_start >= column_limit) {
      continue;
    }
    const int vertical_step = 1 << subsampling_y;
    const int horizontal_step = 1 << subsampling_x;
    const BlockParameters& bp =
        *block_parameters_holder_.Find(row_start, column_start);
    const int horizontal_level_index =
        kDeblockFilterLevelIndex[plane][kLoopFilterTypeHorizontal];
    const int vertical_level_index =
        kDeblockFilterLevelIndex[plane][kLoopFilterTypeVertical];
    const uint8_t vertical_level =
        bp.deblock_filter_level[vertical_level_index];

    for (int row = row_start; row < row_limit; row += vertical_step) {
      for (int column = column_start; column < column_limit;) {
        const TransformSize tx_size = (plane == kPlaneY)
                                          ? inter_transform_sizes_[row][column]
                                          : bp.uv_transform_size;
        // (1). Don't filter frame boundary.
        // (2). For tile boundary, we don't know whether the previous tile is
        // available or not, thus we handle it after all tiles are decoded.
        const bool is_vertical_border =
            (column == column_start) && is_vertical_block_border;
        if (column == GetDeblockPosition(column4x4_start_, subsampling_x) ||
            (skip && !is_vertical_border)) {
          column += kNum4x4BlocksWide[tx_size] << subsampling_x;
          continue;
        }

        // bp_left is the parameter of the left prediction block which
        // is guaranteed to be inside the tile.
        const BlockParameters& bp_left =
            *block_parameters_holder_.Find(row, column - horizontal_step);
        const uint8_t left_level =
            is_vertical_border
                ? bp_left.deblock_filter_level[vertical_level_index]
                : vertical_level;
        // We don't have to check if the left block is skipped or not,
        // because if the current transform block is on the edge of the coding
        // block, is_vertical_border is true; if it's not on the edge,
        // left skip is equal to skip.
        if (vertical_level != 0 || left_level != 0) {
          const TransformSize left_tx_size =
              (plane == kPlaneY)
                  ? inter_transform_sizes_[row][column - horizontal_step]
                  : bp_left.uv_transform_size;
          const LoopFilterTransformSizeId transform_size_id =
              GetTransformSizeIdWidth(tx_size, left_tx_size);
          const int r = row & (kNum4x4InLoopFilterMaskUnit - 1);
          const int c = column & (kNum4x4InLoopFilterMaskUnit - 1);
          const int shift = LoopFilterMask::GetShift(r, c);
          const int index = LoopFilterMask::GetIndex(r);
          const auto mask = static_cast<uint64_t>(1) << shift;
          masks->SetLeft(mask, unit_id, plane, transform_size_id, index);
          const uint8_t current_level =
              (vertical_level == 0) ? left_level : vertical_level;
          masks->SetLevel(current_level, unit_id, plane,
                          kLoopFilterTypeVertical,
                          LoopFilterMask::GetLevelOffset(r, c));
        }
        column += kNum4x4BlocksWide[tx_size] << subsampling_x;
      }
    }

    // Build bit mask for horizontal edges.
    const uint8_t horizontal_level =
        bp.deblock_filter_level[horizontal_level_index];
    for (int column = column_start; column < column_limit;
         column += horizontal_step) {
      for (int row = row_start; row < row_limit;) {
        const TransformSize tx_size = (plane == kPlaneY)
                                          ? inter_transform_sizes_[row][column]
                                          : bp.uv_transform_size;

        // (1). Don't filter frame boundary.
        // (2). For tile boundary, we don't know whether the previous tile is
        // available or not, thus we handle it after all tiles are decoded.
        const bool is_horizontal_border =
            (row == row_start) && is_horizontal_block_border;
        if (row == GetDeblockPosition(row4x4_start_, subsampling_y) ||
            (skip && !is_horizontal_border)) {
          row += kNum4x4BlocksHigh[tx_size] << subsampling_y;
          continue;
        }

        // bp_top is the parameter of the top prediction block which is
        // guaranteed to be inside the tile.
        const BlockParameters& bp_top =
            *block_parameters_holder_.Find(row - vertical_step, column);
        const uint8_t top_level =
            is_horizontal_border
                ? bp_top.deblock_filter_level[horizontal_level_index]
                : horizontal_level;
        // We don't have to check it the top block is skipped or not,
        // because if the current transform block is on the edge of the coding
        // block, is_horizontal_border is true; if it's not on the edge,
        // top skip is equal to skip.
        if (horizontal_level != 0 || top_level != 0) {
          const TransformSize top_tx_size =
              (plane == kPlaneY)
                  ? inter_transform_sizes_[row - vertical_step][column]
                  : bp_top.uv_transform_size;
          const LoopFilterTransformSizeId transform_size_id =
              static_cast<LoopFilterTransformSizeId>(
                  std::min({kTransformHeightLog2[tx_size] - 2,
                            kTransformHeightLog2[top_tx_size] - 2, 2}));
          const int r = row & (kNum4x4InLoopFilterMaskUnit - 1);
          const int c = column & (kNum4x4InLoopFilterMaskUnit - 1);
          const int shift = LoopFilterMask::GetShift(r, c);
          const int index = LoopFilterMask::GetIndex(r);
          const auto mask = static_cast<uint64_t>(1) << shift;
          masks->SetTop(mask, unit_id, plane, transform_size_id, index);
          const uint8_t current_level =
              (horizontal_level == 0) ? top_level : horizontal_level;
          masks->SetLevel(current_level, unit_id, plane,
                          kLoopFilterTypeHorizontal,
                          LoopFilterMask::GetLevelOffset(r, c));
        }
        row += kNum4x4BlocksHigh[tx_size] << subsampling_y;
      }
    }
  }
}

void Tile::StoreMotionFieldMvsIntoCurrentFrame(const Block& block) {
  // The largest reference MV component that can be saved.
  constexpr int kRefMvsLimit = (1 << 12) - 1;
  const BlockParameters& bp = *block.bp;
  ReferenceFrameType reference_frame_to_store = kReferenceFrameNone;
  MotionVector mv_to_store = {};
  for (int i = 1; i >= 0; --i) {
    if (bp.reference_frame[i] > kReferenceFrameIntra &&
        std::abs(bp.mv[i].mv[MotionVector::kRow]) <= kRefMvsLimit &&
        std::abs(bp.mv[i].mv[MotionVector::kColumn]) <= kRefMvsLimit &&
        GetRelativeDistance(
            reference_order_hint_
                [frame_header_.reference_frame_index[bp.reference_frame[i] -
                                                     kReferenceFrameLast]],
            frame_header_.order_hint, sequence_header_.enable_order_hint,
            sequence_header_.order_hint_bits) < 0) {
      reference_frame_to_store = bp.reference_frame[i];
      mv_to_store = bp.mv[i];
      break;
    }
  }
  // Iterate over odd rows/columns beginning at the first odd row/column for the
  // block. It is done this way because motion field mvs are only needed at a
  // 8x8 granularity.
  const int row_start = block.row4x4 | 1;
  const int row_limit = std::min(block.row4x4 + kNum4x4BlocksHigh[block.size],
                                 frame_header_.rows4x4);
  const int column_start = block.column4x4 | 1;
  const int column_limit =
      std::min(block.column4x4 + block.width4x4, frame_header_.columns4x4);
  for (int row = row_start; row < row_limit; row += 2) {
    const int row_index = DivideBy2(row);
    ReferenceFrameType* const reference_frame_row_start =
        current_frame_.motion_field_reference_frame(row_index,
                                                    DivideBy2(column_start));
    static_assert(sizeof(reference_frame_to_store) == sizeof(int8_t), "");
    memset(reference_frame_row_start, reference_frame_to_store,
           DivideBy2(column_limit - column_start + 1));
    if (reference_frame_to_store <= kReferenceFrameIntra) continue;
    for (int column = column_start; column < column_limit; column += 2) {
      MotionVector* const mv =
          current_frame_.motion_field_mv(row_index, DivideBy2(column));
      mv->mv32 = mv_to_store.mv32;
    }
  }
}

}  // namespace libgav1
