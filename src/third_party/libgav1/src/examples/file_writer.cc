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

#include "examples/file_writer.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"

namespace libgav1 {
namespace {

FILE* SetBinaryMode(FILE* stream) {
#if defined(_WIN32)
  _setmode(_fileno(stream), _O_BINARY);
#endif
  return stream;
}

std::string GetY4mColorSpaceString(
    const FileWriter::Y4mParameters& y4m_parameters) {
  std::string color_space_string;
  switch (y4m_parameters.image_format) {
    case kImageFormatMonochrome400:
      color_space_string = "mono";
      break;
    case kImageFormatYuv420:
      if (y4m_parameters.bitdepth == 8) {
        if (y4m_parameters.chroma_sample_position ==
            kChromaSamplePositionVertical) {
          color_space_string = "420mpeg2";
        } else if (y4m_parameters.chroma_sample_position ==
                   kChromaSamplePositionColocated) {
          color_space_string = "420";
        } else {
          color_space_string = "420jpeg";
        }
      } else {
        color_space_string = "420";
      }
      break;
    case kImageFormatYuv422:
      color_space_string = "422";
      break;
    case kImageFormatYuv444:
      color_space_string = "444";
      break;
  }

  if (y4m_parameters.bitdepth > 8) {
    const bool monochrome =
        y4m_parameters.image_format == kImageFormatMonochrome400;
    color_space_string =
        absl::StrFormat("%s%s%d", color_space_string, monochrome ? "" : "p",
                        y4m_parameters.bitdepth);
  }

  return color_space_string;
}

}  // namespace

#define FILEWRITER_LOG_ERROR(error_string)                             \
  do {                                                                 \
    fprintf(stderr, "%s:%d (%s): %s.\n", __FILE__, __LINE__, __func__, \
            error_string);                                             \
  } while (false)

FileWriter::~FileWriter() { fclose(file_); }

std::unique_ptr<FileWriter> FileWriter::Open(
    absl::string_view file_name, FileType file_type,
    const Y4mParameters* const y4m_parameters) {
  if (file_name.empty() ||
      (file_type == kFileTypeY4m && y4m_parameters == nullptr) ||
      (file_type != kFileTypeRaw && file_type != kFileTypeY4m)) {
    FILEWRITER_LOG_ERROR("Invalid parameters");
    return nullptr;
  }

  const std::string fopen_file_name = std::string(file_name);
  FILE* raw_file_ptr;

  if (file_name == "-") {
    raw_file_ptr = SetBinaryMode(stdout);
  } else {
    raw_file_ptr = fopen(fopen_file_name.c_str(), "wb");
  }

  if (raw_file_ptr == nullptr) {
    FILEWRITER_LOG_ERROR("Unable to open output file");
    return nullptr;
  }

  auto file = absl::WrapUnique(new (std::nothrow) FileWriter(raw_file_ptr));
  if (file == nullptr) {
    FILEWRITER_LOG_ERROR("Out of memory");
    fclose(raw_file_ptr);
    return nullptr;
  }

  if (file_type == kFileTypeY4m && !file->WriteY4mFileHeader(*y4m_parameters)) {
    FILEWRITER_LOG_ERROR("Error writing Y4M file header");
    return nullptr;
  }

  file->file_type_ = file_type;
  return file;
}

bool FileWriter::WriteFrame(const DecoderBuffer& frame_buffer) {
  if (file_type_ == kFileTypeY4m) {
    const char kY4mFrameHeader[] = "FRAME\n";
    if (fwrite(kY4mFrameHeader, 1, strlen(kY4mFrameHeader), file_) !=
        strlen(kY4mFrameHeader)) {
      FILEWRITER_LOG_ERROR("Error writing Y4M frame header");
      return false;
    }
  }

  const size_t pixel_size =
      (frame_buffer.bitdepth == 8) ? sizeof(uint8_t) : sizeof(uint16_t);
  for (int plane_index = 0; plane_index < frame_buffer.NumPlanes();
       ++plane_index) {
    const int height = frame_buffer.displayed_height[plane_index];
    const int width = frame_buffer.displayed_width[plane_index];
    const int stride = frame_buffer.stride[plane_index];
    const uint8_t* const plane_pointer = frame_buffer.plane[plane_index];
    for (int row = 0; row < height; ++row) {
      const uint8_t* const row_pointer = &plane_pointer[row * stride];
      if (fwrite(row_pointer, pixel_size, width, file_) !=
          static_cast<size_t>(width)) {
        char error_string[256];
        snprintf(error_string, sizeof(error_string),
                 "File write failed: %s (errno=%d)", strerror(errno), errno);
        FILEWRITER_LOG_ERROR(error_string);
        return false;
      }
    }
  }

  return true;
}

// Writes Y4M file header to |file_| and returns true when successful.
//
// A Y4M file begins with a plaintext file signature of 'YUV4MPEG2 '.
//
// Following the signature is any number of optional parameters preceded by a
// space. We always write:
//
// Width: 'W' followed by image width in pixels.
// Height: 'H' followed by image height in pixels.
// Frame Rate: 'F' followed frames/second in the form numerator:denominator.
// Interlacing: 'I' followed by 'p' for progressive.
// Color space: 'C' followed by a string representation of the color space.
//
// More info here: https://wiki.multimedia.cx/index.php/YUV4MPEG2
bool FileWriter::WriteY4mFileHeader(const Y4mParameters& y4m_parameters) {
  std::string y4m_header = absl::StrFormat(
      "YUV4MPEG2 W%zu H%zu F%zu:%zu Ip C%s\n", y4m_parameters.width,
      y4m_parameters.height, y4m_parameters.frame_rate_numerator,
      y4m_parameters.frame_rate_denominator,
      GetY4mColorSpaceString(y4m_parameters));
  return fwrite(y4m_header.c_str(), 1, y4m_header.length(), file_) ==
         y4m_header.length();
}

}  // namespace libgav1
