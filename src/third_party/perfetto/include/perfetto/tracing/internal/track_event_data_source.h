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

#ifndef INCLUDE_PERFETTO_TRACING_INTERNAL_TRACK_EVENT_DATA_SOURCE_H_
#define INCLUDE_PERFETTO_TRACING_INTERNAL_TRACK_EVENT_DATA_SOURCE_H_

#include "perfetto/base/compiler.h"
#include "perfetto/protozero/message_handle.h"
#include "perfetto/tracing/data_source.h"
#include "perfetto/tracing/event_context.h"
#include "perfetto/tracing/internal/track_event_internal.h"
#include "perfetto/tracing/track.h"
#include "perfetto/tracing/track_event_category_registry.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

#include <type_traits>
#include <unordered_map>

namespace perfetto {
namespace internal {
namespace {

// A template helper for determining whether a type can be used as a track event
// lambda, i.e., it has the signature "void(EventContext)". This is achieved by
// checking that we can pass an EventContext value (the inner declval) into a T
// instance (the outer declval). If this is a valid expression, the result
// evaluates to sizeof(0), i.e., true.
// TODO(skyostil): Replace this with std::is_convertible<std::function<...>>
// once we have C++14.
template <typename T>
static constexpr bool IsValidTraceLambdaImpl(
    typename std::enable_if<static_cast<bool>(
        sizeof(std::declval<T>()(std::declval<EventContext>()), 0))>::type* =
        nullptr) {
  return true;
}

template <typename T>
static constexpr bool IsValidTraceLambdaImpl(...) {
  return false;
}

template <typename T>
static constexpr bool IsValidTraceLambda() {
  return IsValidTraceLambdaImpl<T>(nullptr);
}

}  // namespace

struct TrackEventDataSourceTraits : public perfetto::DefaultDataSourceTraits {
  using IncrementalStateType = TrackEventIncrementalState;

  // Use a one shared TLS slot so that all track event data sources write into
  // the same sequence and share interning dictionaries.
  static DataSourceThreadLocalState* GetDataSourceTLS(DataSourceStaticState*,
                                                      TracingTLS* root_tls) {
    return &root_tls->track_event_tls;
  }
};

// A helper that ensures movable debug annotations are passed by value to
// minimize binary size at the call site, while allowing non-movable and
// non-copiable arguments to be passed by reference.
// TODO(skyostil): Remove this with C++17.
template <typename T>
struct DebugAnnotationArg {
  using type = typename std::
      conditional<std::is_move_constructible<T>::value, T, T&&>::type;
};

// A generic track event data source which is instantiated once per track event
// category namespace.
template <typename DataSourceType, const TrackEventCategoryRegistry* Registry>
class TrackEventDataSource
    : public DataSource<DataSourceType, TrackEventDataSourceTraits> {
  using Base = DataSource<DataSourceType, TrackEventDataSourceTraits>;

 public:
  // DataSource implementation.
  void OnSetup(const DataSourceBase::SetupArgs& args) override {
    TrackEventInternal::EnableTracing(*Registry, *args.config,
                                      args.internal_instance_index);
  }

  void OnStart(const DataSourceBase::StartArgs&) override {}

  void OnStop(const DataSourceBase::StopArgs& args) override {
    TrackEventInternal::DisableTracing(*Registry, args.internal_instance_index);
  }

  static void Flush() {
    Base::template Trace([](typename Base::TraceContext ctx) { ctx.Flush(); });
  }

  // This is the inlined entrypoint for all track event trace points. It tries
  // to be as lightweight as possible in terms of instructions and aims to
  // compile down to an unlikely conditional jump to the actual trace writing
  // function.
  template <size_t CategoryIndex, typename Callback>
  static void CallIfCategoryEnabled(Callback callback) PERFETTO_ALWAYS_INLINE {
    Base::template CallIfEnabled<CategoryTracePointTraits<CategoryIndex>>(
        [&callback](uint32_t instances) { callback(instances); });
  }

  // Once we've determined tracing to be enabled for this category, actually
  // write a trace event onto this thread's default track. Outlined to avoid
  // bloating code (mostly stack depth) at the actual trace point.
  //
  // To minimize call overhead at each trace point, we provide the following
  // trace point argument variants:
  //
  // - None
  // - Lambda
  // - One debug annotation
  // - Two debug annotations
  // - Track
  // - Track + Lambda
  // - Track + one debug annotation
  // - Track + two debug annotations

  // Trace point which takes no arguments.
  template <size_t CategoryIndex>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type)
      PERFETTO_NO_INLINE {
    TraceForCategoryImpl<CategoryIndex>(instances, event_name, type);
  }

  // Trace point which takes a lambda function argument.
  template <size_t CategoryIndex,
            typename ArgumentFunction = void (*)(EventContext),
            typename ArgumentFunctionCheck = typename std::enable_if<
                IsValidTraceLambda<ArgumentFunction>()>::type>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type,
                               ArgumentFunction arg_function)
      PERFETTO_NO_INLINE {
    TraceForCategoryImpl<CategoryIndex>(instances, event_name, type, Track(),
                                        std::move(arg_function));
  }

  // This variant of the inner trace point takes a Track argument which can be
  // used to emit events on a non-default track.
  template <size_t CategoryIndex,
            typename TrackType,
            typename TrackTypeCheck = typename std::enable_if<
                std::is_convertible<TrackType, Track>::value>::type>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type,
                               const TrackType& track) PERFETTO_NO_INLINE {
    TraceForCategoryImpl<CategoryIndex>(instances, event_name, type, track);
  }

  // Trace point with a track and a lambda function.
  template <size_t CategoryIndex,
            typename TrackType,
            typename ArgumentFunction = void (*)(EventContext),
            typename ArgumentFunctionCheck = typename std::enable_if<
                IsValidTraceLambda<ArgumentFunction>()>::type,
            typename TrackTypeCheck = typename std::enable_if<
                std::is_convertible<TrackType, Track>::value>::type>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type,
                               const TrackType& track,
                               ArgumentFunction arg_function)
      PERFETTO_NO_INLINE {
    TraceForCategoryImpl<CategoryIndex>(instances, event_name, type, track,
                                        std::move(arg_function));
  }

  // Trace point with one debug annotation.
  //
  // This type of trace point is implemented with an inner helper function which
  // ensures |arg_value| is only passed by reference when required (i.e., with a
  // custom DebugAnnotation type). This avoids the binary and runtime overhead
  // of unnecessarily passing all types debug annotations by reference.
  //
  // Note that for this to work well, the _outer_ function (this function) has
  // to be inlined at the call site while the _inner_ function
  // (TraceForCategoryWithDebugAnnotations) is still outlined to minimize
  // overall binary size.
  template <size_t CategoryIndex, typename ArgType>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type,
                               const char* arg_name,
                               ArgType&& arg_value) PERFETTO_ALWAYS_INLINE {
    TraceForCategoryWithDebugAnnotations<CategoryIndex, Track, ArgType>(
        instances, event_name, type, Track(), arg_name,
        std::forward<ArgType>(arg_value));
  }

  // A one argument trace point which takes an explicit track.
  template <size_t CategoryIndex, typename TrackType, typename ArgType>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type,
                               const TrackType& track,
                               const char* arg_name,
                               ArgType&& arg_value) PERFETTO_ALWAYS_INLINE {
    PERFETTO_DCHECK(track);
    TraceForCategoryWithDebugAnnotations<CategoryIndex, TrackType, ArgType>(
        instances, event_name, type, track, arg_name,
        std::forward<ArgType>(arg_value));
  }

  template <size_t CategoryIndex, typename TrackType, typename ArgType>
  static void TraceForCategoryWithDebugAnnotations(
      uint32_t instances,
      const char* event_name,
      perfetto::protos::pbzero::TrackEvent::Type type,
      const TrackType& track,
      const char* arg_name,
      typename internal::DebugAnnotationArg<ArgType>::type arg_value)
      PERFETTO_NO_INLINE {
    Base::template TraceWithInstances<CategoryTracePointTraits<CategoryIndex>>(
        instances, [&](typename Base::TraceContext ctx) {
          {
            // TODO(skyostil): Intern categories at compile time.
            auto event_ctx = TrackEventInternal::WriteEvent(
                ctx.tls_inst_->trace_writer.get(), ctx.GetIncrementalState(),
                Registry->GetCategory(CategoryIndex)->name, event_name, type);
            if (track)
              event_ctx.event()->set_track_uuid(track.uuid);
            TrackEventInternal::AddDebugAnnotation(&event_ctx, arg_name,
                                                   arg_value);
          }
          if (track) {
            TrackEventInternal::WriteTrackDescriptorIfNeeded(
                track, ctx.tls_inst_->trace_writer.get(),
                ctx.GetIncrementalState());
          }
        });
  }

  // Trace point with two debug annotations. Note that we only support up to two
  // direct debug annotations. For more complicated arguments, you should
  // define your own argument type in track_event.proto and use a lambda to fill
  // it in your trace point.
  template <size_t CategoryIndex, typename ArgType, typename ArgType2>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type,
                               const char* arg_name,
                               ArgType&& arg_value,
                               const char* arg_name2,
                               ArgType2&& arg_value2) PERFETTO_ALWAYS_INLINE {
    TraceForCategoryWithDebugAnnotations<CategoryIndex, Track, ArgType,
                                         ArgType2>(
        instances, event_name, type, Track(), arg_name,
        std::forward<ArgType>(arg_value), arg_name2,
        std::forward<ArgType2>(arg_value2));
  }

  // A two argument trace point which takes an explicit track.
  template <size_t CategoryIndex,
            typename TrackType,
            typename ArgType,
            typename ArgType2>
  static void TraceForCategory(uint32_t instances,
                               const char* event_name,
                               perfetto::protos::pbzero::TrackEvent::Type type,
                               const TrackType& track,
                               const char* arg_name,
                               ArgType&& arg_value,
                               const char* arg_name2,
                               ArgType2&& arg_value2) PERFETTO_ALWAYS_INLINE {
    PERFETTO_DCHECK(track);
    TraceForCategoryWithDebugAnnotations<CategoryIndex, TrackType, ArgType,
                                         ArgType2>(
        instances, event_name, type, track, arg_name,
        std::forward<ArgType>(arg_value), arg_name2,
        std::forward<ArgType2>(arg_value2));
  }

  template <size_t CategoryIndex,
            typename TrackType,
            typename ArgType,
            typename ArgType2>
  static void TraceForCategoryWithDebugAnnotations(
      uint32_t instances,
      const char* event_name,
      perfetto::protos::pbzero::TrackEvent::Type type,
      TrackType track,
      const char* arg_name,
      typename internal::DebugAnnotationArg<ArgType>::type arg_value,
      const char* arg_name2,
      typename internal::DebugAnnotationArg<ArgType2>::type arg_value2)
      PERFETTO_NO_INLINE {
    Base::template TraceWithInstances<CategoryTracePointTraits<CategoryIndex>>(
        instances, [&](typename Base::TraceContext ctx) {
          // TODO(skyostil): Intern categories at compile time.
          {
            auto event_ctx = TrackEventInternal::WriteEvent(
                ctx.tls_inst_->trace_writer.get(), ctx.GetIncrementalState(),
                Registry->GetCategory(CategoryIndex)->name, event_name, type);
            if (track)
              event_ctx.event()->set_track_uuid(track.uuid);
            TrackEventInternal::AddDebugAnnotation(&event_ctx, arg_name,
                                                   arg_value);
            TrackEventInternal::AddDebugAnnotation(&event_ctx, arg_name2,
                                                   arg_value2);
          }
          if (track) {
            TrackEventInternal::WriteTrackDescriptorIfNeeded(
                track, ctx.tls_inst_->trace_writer.get(),
                ctx.GetIncrementalState());
          }
        });
  }

  // Initialize the track event library. Should be called before tracing is
  // enabled.
  static bool Register() {
    // Registration is performed out-of-line so users don't need to depend on
    // DataSourceDescriptor C++ bindings.
    return TrackEventInternal::Initialize(
        [](const DataSourceDescriptor& dsd) { return Base::Register(dsd); });
  }

  // Record metadata about different types of timeline tracks. See Track.
  static void SetTrackDescriptor(
      const Track& track,
      std::function<void(protos::pbzero::TrackDescriptor*)> callback) {
    SetTrackDescriptorImpl(track, std::move(callback));
  }

  static void SetProcessDescriptor(
      std::function<void(protos::pbzero::TrackDescriptor*)> callback,
      const ProcessTrack& track = ProcessTrack::Current()) {
    SetTrackDescriptorImpl(std::move(track), std::move(callback));
  }

  static void SetThreadDescriptor(
      std::function<void(protos::pbzero::TrackDescriptor*)> callback,
      const ThreadTrack& track = ThreadTrack::Current()) {
    SetTrackDescriptorImpl(std::move(track), std::move(callback));
  }

  static void EraseTrackDescriptor(const Track& track) {
    TrackRegistry::Get()->EraseTrack(track);
  }

 private:
  // Each category has its own enabled/disabled state, stored in the category
  // registry.
  template <size_t CategoryIndex>
  struct CategoryTracePointTraits {
    static constexpr std::atomic<uint8_t>* GetActiveInstances() {
      return Registry->GetCategoryState(CategoryIndex);
    }
  };

  // TODO(skyostil): Make |CategoryIndex| a regular parameter to reuse trace
  // point code across different categories.
  template <size_t CategoryIndex,
            typename TrackType = Track,
            typename ArgumentFunction = void (*)(EventContext),
            typename ArgumentFunctionCheck = typename std::enable_if<
                IsValidTraceLambda<ArgumentFunction>()>::type,
            typename TrackTypeCheck = typename std::enable_if<
                std::is_convertible<TrackType, Track>::value>::type>
  static void TraceForCategoryImpl(
      uint32_t instances,
      const char* event_name,
      perfetto::protos::pbzero::TrackEvent::Type type,
      const TrackType& track = Track(),
      ArgumentFunction arg_function = [](EventContext) {
      }) PERFETTO_ALWAYS_INLINE {
    Base::template TraceWithInstances<CategoryTracePointTraits<CategoryIndex>>(
        instances, [&](typename Base::TraceContext ctx) {
          {
            // TODO(skyostil): Intern categories at compile time.
            auto event_ctx = TrackEventInternal::WriteEvent(
                ctx.tls_inst_->trace_writer.get(), ctx.GetIncrementalState(),
                Registry->GetCategory(CategoryIndex)->name, event_name, type);
            if (track)
              event_ctx.event()->set_track_uuid(track.uuid);
            arg_function(std::move(event_ctx));
          }
          if (track) {
            TrackEventInternal::WriteTrackDescriptorIfNeeded(
                track, ctx.tls_inst_->trace_writer.get(),
                ctx.GetIncrementalState());
          }
        });
  }

  // Records a track descriptor into the track descriptor registry and, if we
  // are tracing, also mirrors the descriptor into the trace.
  template <typename TrackType>
  static void SetTrackDescriptorImpl(
      const TrackType& track,
      std::function<void(protos::pbzero::TrackDescriptor*)> callback) {
    TrackRegistry::Get()->UpdateTrack(
        track, [&](protos::pbzero::TrackDescriptor* desc) { callback(desc); });
    Base::template Trace([&](typename Base::TraceContext ctx) {
      TrackEventInternal::WriteTrackDescriptor(
          track, ctx.tls_inst_->trace_writer.get());
    });
  }
};

}  // namespace internal
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_INTERNAL_TRACK_EVENT_DATA_SOURCE_H_
