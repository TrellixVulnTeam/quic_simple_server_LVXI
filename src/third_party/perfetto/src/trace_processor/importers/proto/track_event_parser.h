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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_PARSER_H_

#include "perfetto/protozero/field.h"
#include "src/trace_processor/args_tracker.h"
#include "src/trace_processor/slice_tracker.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {

namespace protos {
namespace pbzero {
class TrackEvent_LegacyEvent_Decoder;
}  // namespace pbzero
}  // namespace protos

namespace trace_processor {

class PacketSequenceStateGeneration;
class TraceProcessorContext;

class TrackEventParser {
 public:
  explicit TrackEventParser(TraceProcessorContext* context);

  void ParseTrackDescriptor(protozero::ConstBytes);
  UniquePid ParseProcessDescriptor(protozero::ConstBytes);
  UniqueTid ParseThreadDescriptor(protozero::ConstBytes);

  void ParseTrackEvent(int64_t ts,
                       int64_t tts,
                       int64_t ticount,
                       PacketSequenceStateGeneration*,
                       protozero::ConstBytes);

 private:
  void ParseChromeProcessDescriptor(UniquePid upid, protozero::ConstBytes);
  void ParseChromeThreadDescriptor(UniqueTid utid, protozero::ConstBytes);
  void ParseLegacyEventAsRawEvent(
      int64_t ts,
      int64_t tts,
      int64_t ticount,
      base::Optional<UniqueTid> utid,
      StringId category_id,
      StringId name_id,
      const protos::pbzero::TrackEvent_LegacyEvent_Decoder& legacy_event,
      SliceTracker::SetArgsCallback slice_args_callback);
  void ParseDebugAnnotationArgs(protozero::ConstBytes debug_annotation,
                                PacketSequenceStateGeneration*,
                                ArgsTracker::BoundInserter* inserter);
  void ParseNestedValueArgs(protozero::ConstBytes nested_value,
                            base::StringView flat_key,
                            base::StringView key,
                            ArgsTracker::BoundInserter* inserter);
  void ParseTaskExecutionArgs(protozero::ConstBytes task_execution,
                              PacketSequenceStateGeneration*,
                              ArgsTracker::BoundInserter* inserter);
  void ParseLogMessage(protozero::ConstBytes,
                       PacketSequenceStateGeneration*,
                       int64_t,
                       base::Optional<UniqueTid>,
                       ArgsTracker::BoundInserter* inserter);
  void ParseCcScheduler(protozero::ConstBytes cc_scheduler,
                        PacketSequenceStateGeneration*,
                        ArgsTracker::BoundInserter* inserter);
  void ParseChromeUserEvent(protozero::ConstBytes chrome_user_event,
                            ArgsTracker::BoundInserter* inserter);
  void ParseChromeLegacyIpc(protozero::ConstBytes chrome_legacy_ipc,
                            ArgsTracker::BoundInserter* inserter);
  void ParseChromeKeyedService(protozero::ConstBytes chrome_keyed_service,
                               ArgsTracker::BoundInserter* inserter);
  void ParseChromeHistogramSample(protozero::ConstBytes chrome_keyed_service,
                                  ArgsTracker::BoundInserter* inserter);

  TraceProcessorContext* context_;

  const StringId task_file_name_args_key_id_;
  const StringId task_function_name_args_key_id_;
  const StringId task_line_number_args_key_id_;
  const StringId log_message_body_key_id_;
  const StringId raw_legacy_event_id_;
  const StringId legacy_event_passthrough_utid_id_;
  const StringId legacy_event_category_key_id_;
  const StringId legacy_event_name_key_id_;
  const StringId legacy_event_phase_key_id_;
  const StringId legacy_event_duration_ns_key_id_;
  const StringId legacy_event_thread_timestamp_ns_key_id_;
  const StringId legacy_event_thread_duration_ns_key_id_;
  const StringId legacy_event_thread_instruction_count_key_id_;
  const StringId legacy_event_thread_instruction_delta_key_id_;
  const StringId legacy_event_use_async_tts_key_id_;
  const StringId legacy_event_unscoped_id_key_id_;
  const StringId legacy_event_global_id_key_id_;
  const StringId legacy_event_local_id_key_id_;
  const StringId legacy_event_id_scope_key_id_;
  const StringId legacy_event_bind_id_key_id_;
  const StringId legacy_event_bind_to_enclosing_key_id_;
  const StringId legacy_event_flow_direction_key_id_;
  const StringId flow_direction_value_in_id_;
  const StringId flow_direction_value_out_id_;
  const StringId flow_direction_value_inout_id_;
  const StringId chrome_user_event_action_args_key_id_;
  const StringId chrome_legacy_ipc_class_args_key_id_;
  const StringId chrome_legacy_ipc_line_args_key_id_;
  const StringId chrome_keyed_service_name_args_key_id_;
  const StringId chrome_histogram_sample_name_hash_args_key_id_;
  const StringId chrome_histogram_sample_name_args_key_id_;
  const StringId chrome_histogram_sample_sample_args_key_id_;

  std::array<StringId, 38> chrome_legacy_ipc_class_ids_;
  std::array<StringId, 9> chrome_process_name_ids_;
  std::array<StringId, 14> chrome_thread_name_ids_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_PARSER_H_
