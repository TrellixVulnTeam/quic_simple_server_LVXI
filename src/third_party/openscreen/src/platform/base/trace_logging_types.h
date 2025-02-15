// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_BASE_TRACE_LOGGING_TYPES_H_
#define PLATFORM_BASE_TRACE_LOGGING_TYPES_H_

#include <stdint.h>

#include <limits>

namespace openscreen {

// Define TraceId type here since other TraceLogging files import it.
using TraceId = uint64_t;

// kEmptyTraceId is the Trace ID when tracing at a global level, not inside any
// tracing block - ie this will be the parent ID for a top level tracing block.
constexpr TraceId kEmptyTraceId = 0x0;

// kUnsetTraceId is the Trace ID passed in to the tracing library when no user-
// specified value is desired.
constexpr TraceId kUnsetTraceId = std::numeric_limits<TraceId>::max();

// A class to represent the current TraceId Hirearchy and for the user to
// pass around as needed.
struct TraceIdHierarchy {
  TraceId current;
  TraceId parent;
  TraceId root;

  static constexpr TraceIdHierarchy Empty() {
    return {kEmptyTraceId, kEmptyTraceId, kEmptyTraceId};
  }

  bool HasCurrent() { return current != kUnsetTraceId; }
  bool HasParent() { return parent != kUnsetTraceId; }
  bool HasRoot() { return root != kUnsetTraceId; }
};

inline bool operator==(const TraceIdHierarchy& lhs,
                       const TraceIdHierarchy& rhs) {
  return lhs.current == rhs.current && lhs.parent == rhs.parent &&
         lhs.root == rhs.root;
}

inline bool operator!=(const TraceIdHierarchy& lhs,
                       const TraceIdHierarchy& rhs) {
  return !(lhs == rhs);
}

// BitFlags to represent the supported tracing categories.
// NOTE: These are currently placeholder values and later changes should feel
// free to edit them.
// TODO(rwkeane): Rename SSL to either Ssl or kSsl
struct TraceCategory {
  enum Value : uint64_t {
    Any = std::numeric_limits<uint64_t>::max(),
    mDNS = 0x01 << 0,
    Quic = 0x01 << 1,
    SSL = 0x01 << 2,
    Presentation = 0x01 << 3,
  };
};

}  // namespace openscreen

#endif  // PLATFORM_BASE_TRACE_LOGGING_TYPES_H_
