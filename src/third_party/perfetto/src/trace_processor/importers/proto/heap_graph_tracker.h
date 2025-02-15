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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_HEAP_GRAPH_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_HEAP_GRAPH_TRACKER_H_

#include <map>
#include <vector>

#include "perfetto/ext/base/optional.h"

#include "protos/perfetto/trace/profiling/heap_graph.pbzero.h"
#include "src/trace_processor/importers/proto/heap_graph_walker.h"
#include "src/trace_processor/trace_processor_context.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

class TraceProcessorContext;

size_t NumberOfArrays(base::StringView type);
base::StringView NormalizeTypeName(base::StringView type);

class HeapGraphTracker : public HeapGraphWalker::Delegate, public Destructible {
 public:
  struct SourceObject {
    // All ids in this are in the trace iid space, not in the trace processor
    // id space.
    struct Reference {
      uint64_t field_name_id = 0;
      uint64_t owned_object_id = 0;
    };
    uint64_t object_id = 0;
    uint64_t self_size = 0;
    uint64_t type_id = 0;
    std::vector<Reference> references;
  };

  struct SourceRoot {
    StringPool::Id root_type;
    std::vector<uint64_t> object_ids;
  };

  explicit HeapGraphTracker(TraceProcessorContext* context);

  static HeapGraphTracker* GetOrCreate(TraceProcessorContext* context) {
    if (!context->heap_graph_tracker) {
      context->heap_graph_tracker.reset(new HeapGraphTracker(context));
    }
    return static_cast<HeapGraphTracker*>(context->heap_graph_tracker.get());
  }

  void AddRoot(uint32_t seq_id, UniquePid upid, int64_t ts, SourceRoot root);
  void AddObject(uint32_t seq_id, UniquePid upid, int64_t ts, SourceObject obj);
  void AddInternedTypeName(uint32_t seq_id,
                           uint64_t intern_id,
                           StringPool::Id strid);
  void AddInternedFieldName(uint32_t seq_id,
                            uint64_t intern_id,
                            StringPool::Id strid);
  void FinalizeProfile(uint32_t seq);
  void SetPacketIndex(uint32_t seq_id, uint64_t index);

  ~HeapGraphTracker() override = default;
  // HeapGraphTracker::Delegate
  void MarkReachable(int64_t row) override;
  void SetRetained(int64_t row,
                   int64_t retained,
                   int64_t unique_retained) override;

  const std::vector<int64_t>* RowsForType(StringPool::Id type_name) const {
    auto it = class_to_rows_.find(type_name);
    if (it == class_to_rows_.end())
      return nullptr;
    return &it->second;
  }

  const std::vector<int64_t>* RowsForField(StringPool::Id field_name) const {
    auto it = field_to_rows_.find(field_name);
    if (it == field_to_rows_.end())
      return nullptr;
    return &it->second;
  }

  std::unique_ptr<tables::ExperimentalFlamegraphNodesTable> BuildFlamegraph(
      const int64_t current_ts,
      const UniquePid current_upid);

 private:
  struct SequenceState {
    SequenceState(HeapGraphTracker* tracker) : walker(tracker) {}

    UniquePid current_upid = 0;
    int64_t current_ts = 0;
    std::vector<SourceObject> current_objects;
    std::vector<SourceRoot> current_roots;
    std::map<uint64_t, StringPool::Id> interned_type_names;
    std::map<uint64_t, StringPool::Id> interned_field_names;
    std::map<uint64_t, uint32_t> object_id_to_row;
    base::Optional<uint64_t> prev_index;
    HeapGraphWalker walker;
  };

  SequenceState& GetOrCreateSequence(uint32_t seq_id);
  bool SetPidAndTimestamp(SequenceState* seq, UniquePid upid, int64_t ts);


  TraceProcessorContext* const context_;
  std::map<uint32_t, SequenceState> sequence_state_;
  std::map<std::pair<UniquePid, int64_t /* ts */>, HeapGraphWalker> walkers_;

  std::map<StringPool::Id, std::vector<int64_t>> class_to_rows_;
  std::map<StringPool::Id, std::vector<int64_t>> field_to_rows_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_HEAP_GRAPH_TRACKER_H_
