// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/resize_observer/resize_observer.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_resize_observer_callback.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/layout/adjust_for_absolute_zoom.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observation.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_controller.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_entry.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_options.h"

namespace blink {

constexpr const char* kBoxOptionBorderBox = "border-box";
constexpr const char* kBoxOptionContentBox = "content-box";

ResizeObserver* ResizeObserver::Create(Document& document,
                                       V8ResizeObserverCallback* callback) {
  return MakeGarbageCollected<ResizeObserver>(callback, document);
}

ResizeObserver* ResizeObserver::Create(Document& document, Delegate* delegate) {
  return MakeGarbageCollected<ResizeObserver>(delegate, document);
}

ResizeObserver::ResizeObserver(V8ResizeObserverCallback* callback,
                               Document& document)
    : ContextClient(&document),
      callback_(callback),
      skipped_observations_(false),
      element_size_changed_(false) {
  DCHECK(callback_);
  controller_ = &document.EnsureResizeObserverController();
  controller_->AddObserver(*this);
}

ResizeObserver::ResizeObserver(Delegate* delegate, Document& document)
    : ContextClient(&document),
      delegate_(delegate),
      skipped_observations_(false),
      element_size_changed_(false) {
  DCHECK(delegate_);
  controller_ = &document.EnsureResizeObserverController();
  controller_->AddObserver(*this);
}

ResizeObserverBoxOptions ResizeObserver::ParseBoxOptions(
    const String& box_options) {
  if (box_options == kBoxOptionBorderBox)
    return ResizeObserverBoxOptions::BorderBox;
  if (box_options == kBoxOptionContentBox)
    return ResizeObserverBoxOptions::ContentBox;
  return ResizeObserverBoxOptions::ContentBox;
}

void ResizeObserver::observeInternal(Element* target,
                                     ResizeObserverBoxOptions box_option) {
  auto& observer_map = target->EnsureResizeObserverData();

  if (observer_map.Contains(this)) {
    auto observation = observer_map.find(this);
    if ((*observation).value->observedBox() == box_option)
      return;

    // Unobserve target if box_option has changed and target already existed. If
    // there is an existing observation of a different box, this new observation
    // takes precedence. See:
    // https://drafts.csswg.org/resize-observer/#processing-model
    observations_.erase((*observation).value);
    auto index = active_observations_.Find((*observation).value);
    if (index != kNotFound) {
      active_observations_.EraseAt(index);
    }
    observer_map.erase(observation);
  }

  auto* observation =
      MakeGarbageCollected<ResizeObservation>(target, this, box_option);
  observations_.insert(observation);
  observer_map.Set(this, observation);

  if (LocalFrameView* frame_view = target->GetDocument().View())
    frame_view->ScheduleAnimation();
}

void ResizeObserver::observe(Element* target,
                             const ResizeObserverOptions* options) {
  ResizeObserverBoxOptions box_option = ParseBoxOptions(options->box());
  observeInternal(target, box_option);
}

void ResizeObserver::observe(Element* target) {
  observeInternal(target, ResizeObserverBoxOptions::ContentBox);
}

void ResizeObserver::unobserve(Element* target) {
  auto* observer_map = target ? target->ResizeObserverData() : nullptr;
  if (!observer_map)
    return;
  auto observation = observer_map->find(this);
  if (observation != observer_map->end()) {
    observations_.erase((*observation).value);
    auto index = active_observations_.Find((*observation).value);
    if (index != kNotFound) {
      active_observations_.EraseAt(index);
    }
    observer_map->erase(observation);
  }
}

void ResizeObserver::disconnect() {
  ObservationList observations;
  observations_.Swap(observations);

  for (auto& observation : observations) {
    Element* target = (*observation).Target();
    if (target)
      target->EnsureResizeObserverData().erase(this);
  }
  ClearObservations();
}

size_t ResizeObserver::GatherObservations(size_t deeper_than) {
  DCHECK(active_observations_.IsEmpty());

  size_t min_observed_depth = ResizeObserverController::kDepthBottom;
  if (!element_size_changed_)
    return min_observed_depth;
  for (auto& observation : observations_) {
    if (!observation->ObservationSizeOutOfSync())
      continue;
    auto depth = observation->TargetDepth();
    if (depth > deeper_than) {
      active_observations_.push_back(*observation);
      min_observed_depth = std::min(min_observed_depth, depth);
    } else {
      skipped_observations_ = true;
    }
  }
  return min_observed_depth;
}

void ResizeObserver::DeliverObservations() {
  // We can only clear this flag after all observations have been
  // broadcast.
  element_size_changed_ = skipped_observations_;
  if (active_observations_.IsEmpty())
    return;

  HeapVector<Member<ResizeObserverEntry>> entries;

  for (auto& observation : active_observations_) {
    // In case that the observer and the target belong to different execution
    // contexts and the target's execution context is already gone, then skip
    // such a target.
    ExecutionContext* execution_context =
        observation->Target()->GetExecutionContext();
    if (!execution_context || execution_context->IsContextDestroyed())
      continue;

    observation->SetObservationSize(observation->ComputeTargetSize());
    auto* entry =
        MakeGarbageCollected<ResizeObserverEntry>(observation->Target());
    entries.push_back(entry);
  }

  if (entries.size() == 0) {
    // No entry to report.
    // Note that, if |active_observations_| is not empty but |entries| is empty,
    // it means that it's possible that no target element is making |callback_|
    // alive. In this case, we must not touch |callback_|.
    ClearObservations();
    return;
  }

  DCHECK(callback_ || delegate_);
  if (callback_)
    callback_->InvokeAndReportException(this, entries, this);
  if (delegate_)
    delegate_->OnResize(entries);
  ClearObservations();
}

void ResizeObserver::ClearObservations() {
  active_observations_.clear();
  skipped_observations_ = false;
}

void ResizeObserver::ElementSizeChanged() {
  element_size_changed_ = true;
  if (controller_)
    controller_->ObserverChanged();
}

bool ResizeObserver::HasPendingActivity() const {
  return !observations_.IsEmpty();
}

void ResizeObserver::Trace(blink::Visitor* visitor) {
  visitor->Trace(callback_);
  visitor->Trace(delegate_);
  visitor->Trace(observations_);
  visitor->Trace(active_observations_);
  visitor->Trace(controller_);
  ScriptWrappable::Trace(visitor);
  ContextClient::Trace(visitor);
}

}  // namespace blink
