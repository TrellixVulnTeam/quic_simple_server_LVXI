/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_GLOBAL_ARGS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_GLOBAL_ARGS_TRACKER_H_

#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto {
namespace trace_processor {

// Interns args into the storage from all ArgsTrackers across trace processor.
// Note: most users will want to use ArgsTracker to push args to the strorage
// and not this class. This class is really intended for ArgsTracker to use for
// that purpose.
class GlobalArgsTracker {
 public:
  struct Arg {
    StringId flat_key = kNullStringId;
    StringId key = kNullStringId;
    Variadic value = Variadic::Integer(0);

    Column* column;
    uint32_t row;
  };

  struct ArgHasher {
    uint64_t operator()(const Arg& arg) const noexcept {
      base::Hash hash;
      hash.Update(arg.key);
      // We don't hash arg.flat_key because it's a subsequence of arg.key.
      switch (arg.value.type) {
        case Variadic::Type::kInt:
          hash.Update(arg.value.int_value);
          break;
        case Variadic::Type::kUint:
          hash.Update(arg.value.uint_value);
          break;
        case Variadic::Type::kString:
          hash.Update(arg.value.string_value);
          break;
        case Variadic::Type::kReal:
          hash.Update(arg.value.real_value);
          break;
        case Variadic::Type::kPointer:
          hash.Update(arg.value.pointer_value);
          break;
        case Variadic::Type::kBool:
          hash.Update(arg.value.bool_value);
          break;
        case Variadic::Type::kJson:
          hash.Update(arg.value.json_value);
          break;
      }
      return hash.digest();
    }
  };

  GlobalArgsTracker(TraceProcessorContext* context);

  ArgSetId AddArgSet(const std::vector<Arg>& args,
                     uint32_t begin,
                     uint32_t end) {
    base::Hash hash;
    for (uint32_t i = begin; i < end; i++) {
      hash.Update(ArgHasher()(args[i]));
    }

    auto* arg_table = context_->storage->mutable_arg_table();

    ArgSetHash digest = hash.digest();
    auto it = arg_row_for_hash_.find(digest);
    if (it != arg_row_for_hash_.end())
      return arg_table->arg_set_id()[it->second];

    // The +1 ensures that nothing has an id == kInvalidArgSetId == 0.
    ArgSetId id = static_cast<uint32_t>(arg_row_for_hash_.size()) + 1;
    arg_row_for_hash_.emplace(digest, arg_table->row_count());
    for (uint32_t i = begin; i < end; i++) {
      const auto& arg = args[i];

      tables::ArgTable::Row row;
      row.arg_set_id = id;
      row.flat_key = arg.flat_key;
      row.key = arg.key;
      switch (arg.value.type) {
        case Variadic::Type::kInt:
          row.int_value = arg.value.int_value;
          break;
        case Variadic::Type::kUint:
          row.int_value = static_cast<int64_t>(arg.value.uint_value);
          break;
        case Variadic::Type::kString:
          row.string_value = arg.value.string_value;
          break;
        case Variadic::Type::kReal:
          row.real_value = arg.value.real_value;
          break;
        case Variadic::Type::kPointer:
          row.int_value = static_cast<int64_t>(arg.value.pointer_value);
          break;
        case Variadic::Type::kBool:
          row.int_value = arg.value.bool_value;
          break;
        case Variadic::Type::kJson:
          row.string_value = arg.value.json_value;
          break;
      }
      row.value_type = context_->storage->GetIdForVariadicType(arg.value.type);
      arg_table->Insert(row);
    }
    return id;
  }

 private:
  using ArgSetHash = uint64_t;

  std::unordered_map<ArgSetHash, uint32_t> arg_row_for_hash_;

  TraceProcessorContext* context_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_GLOBAL_ARGS_TRACKER_H_
