// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discovery/mdns/mdns_querier.h"

#include "discovery/mdns/mdns_random.h"
#include "discovery/mdns/mdns_receiver.h"
#include "discovery/mdns/mdns_sender.h"
#include "discovery/mdns/mdns_trackers.h"

namespace openscreen {
namespace discovery {
namespace {

bool IsNegativeResponseFor(const MdnsRecord& record, DnsType type) {
  if (record.dns_type() != DnsType::kNSEC) {
    return false;
  }

  const NsecRecordRdata& nsec = absl::get<NsecRecordRdata>(record.rdata());

  // RFC 6762 section 6.1, the NSEC bit must NOT be set in the received NSEC
  // record to indicate this is an mDNS NSEC record rather than a traditional
  // DNS NSEC record.
  if (std::find(nsec.types().begin(), nsec.types().end(), DnsType::kNSEC) !=
      nsec.types().end()) {
    return false;
  }

  return std::find(nsec.types().begin(), nsec.types().end(), type) !=
         nsec.types().end();
}

}  // namespace

MdnsQuerier::MdnsQuerier(MdnsSender* sender,
                         MdnsReceiver* receiver,
                         TaskRunner* task_runner,
                         ClockNowFunctionPtr now_function,
                         MdnsRandom* random_delay)
    : sender_(sender),
      receiver_(receiver),
      task_runner_(task_runner),
      now_function_(now_function),
      random_delay_(random_delay) {
  OSP_DCHECK(sender_);
  OSP_DCHECK(receiver_);
  OSP_DCHECK(task_runner_);
  OSP_DCHECK(now_function_);
  OSP_DCHECK(random_delay_);

  receiver_->AddResponseCallback(this);
}

MdnsQuerier::~MdnsQuerier() {
  receiver_->RemoveResponseCallback(this);
}

// NOTE: The code below is range loops instead of std:find_if, for better
// readability, brevity and homogeneity.  Using std::find_if results in a few
// more lines of code, readability suffers from extra lambdas.

void MdnsQuerier::StartQuery(const DomainName& name,
                             DnsType dns_type,
                             DnsClass dns_class,
                             MdnsRecordChangedCallback* callback) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(callback);
  OSP_DCHECK(dns_type != DnsType::kNSEC);

  // Add a new callback if haven't seen it before
  auto callbacks_it = callbacks_.equal_range(name);
  for (auto entry = callbacks_it.first; entry != callbacks_it.second; ++entry) {
    const CallbackInfo& callback_info = entry->second;
    if (dns_type == callback_info.dns_type &&
        dns_class == callback_info.dns_class &&
        callback == callback_info.callback) {
      // Already have this callback
      return;
    }
  }
  callbacks_.emplace(name, CallbackInfo{callback, dns_type, dns_class});

  // Notify the new callback with previously cached records.
  // NOTE: In the future, could allow callers to fetch cached records after
  // adding a callback, for example to prime the UI.
  auto records_it = records_.equal_range(name);
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    const MdnsRecord& record = entry->second->record();
    if ((dns_type == DnsType::kANY || dns_type == record.dns_type()) &&
        (dns_class == DnsClass::kANY || dns_class == record.dns_class())) {
      callback->OnRecordChanged(record, RecordChangedEvent::kCreated);
    }
  }

  // Add a new question if haven't seen it before
  auto questions_it = questions_.equal_range(name);
  for (auto entry = questions_it.first; entry != questions_it.second; ++entry) {
    const MdnsQuestion& tracked_question = entry->second->question();
    if (dns_type == tracked_question.dns_type() &&
        dns_class == tracked_question.dns_class()) {
      // Already have this question
      return;
    }
  }
  AddQuestion(
      MdnsQuestion(name, dns_type, dns_class, ResponseType::kMulticast));
}

void MdnsQuerier::StopQuery(const DomainName& name,
                            DnsType dns_type,
                            DnsClass dns_class,
                            MdnsRecordChangedCallback* callback) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(callback);
  OSP_DCHECK(dns_type != DnsType::kNSEC);

  // Find and remove the callback.
  int callbacks_for_key = 0;
  auto callbacks_it = callbacks_.equal_range(name);
  for (auto entry = callbacks_it.first; entry != callbacks_it.second;) {
    const CallbackInfo& callback_info = entry->second;
    if (dns_type == callback_info.dns_type &&
        dns_class == callback_info.dns_class) {
      if (callback == callback_info.callback) {
        entry = callbacks_.erase(entry);
      } else {
        ++callbacks_for_key;
        ++entry;
      }
    }
  }

  // Exit if there are still callbacks registered for DomainName + DnsType +
  // DnsClass
  if (callbacks_for_key > 0) {
    return;
  }

  // Find and delete a question that does not have any associated callbacks
  auto questions_it = questions_.equal_range(name);
  for (auto entry = questions_it.first; entry != questions_it.second; ++entry) {
    const MdnsQuestion& tracked_question = entry->second->question();
    if (dns_type == tracked_question.dns_type() &&
        dns_class == tracked_question.dns_class()) {
      questions_.erase(entry);
      return;
    }
  }

  // TODO(crbug.com/openscreen/83): Find and delete all records that no longer
  // answer any questions, if a question was deleted.  It's possible the same
  // query will be added back before the records expire, so this behavior could
  // be configurable by the caller.
}

void MdnsQuerier::ReinitializeQueries(const DomainName& name) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  // Get the ongoing queries and their callbacks.
  std::vector<CallbackInfo> callbacks;
  auto its = callbacks_.equal_range(name);
  for (auto it = its.first; it != its.second; it++) {
    callbacks.push_back(std::move(it->second));
  }
  callbacks_.erase(name);

  // Remove all known questions and answers.
  questions_.erase(name);
  records_.erase(name);

  // Restart the queries.
  for (const auto& cb : callbacks) {
    StartQuery(name, cb.dns_type, cb.dns_class, cb.callback);
  }
}

void MdnsQuerier::OnMessageReceived(const MdnsMessage& message) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(message.type() == MessageType::Response);

  // Drop the message if its answers don't correspond to any existing question.
  bool is_relevant_answer = false;
  for (const MdnsRecord& answer : message.answers()) {
    const auto range = questions_.equal_range(answer.name());
    const auto it =
        std::find_if(range.first, range.second, [&answer](const auto& pair) {
          return (pair.second->question().dns_type() == DnsType::kANY ||
                  IsNegativeResponseFor(answer,
                                        pair.second->question().dns_type()) ||
                  pair.second->question().dns_type() == answer.dns_type()) &&
                 (pair.second->question().dns_class() == DnsClass::kANY ||
                  pair.second->question().dns_class() == answer.dns_class());
        });
    if (it != range.second) {
      is_relevant_answer = true;
      break;
    }
  }
  if (!is_relevant_answer) {
    return;
  }

  // TODO(crbug.com/openscreen/83): Check authority records.
  // TODO(crbug.com/openscreen/84): Cap size of cache, to avoid memory blowups
  // when publishers misbehave.
  ProcessRecords(message.answers());
  ProcessRecords(message.additional_records());
}

void MdnsQuerier::OnRecordExpired(const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  ProcessCallbacks(record, RecordChangedEvent::kExpired);

  auto records_it = records_.equal_range(record.name());
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    MdnsRecordTracker* tracker = entry->second.get();
    const MdnsRecord& tracked_record = tracker->record();
    if (record.dns_type() == tracked_record.dns_type() &&
        record.dns_class() == tracked_record.dns_class() &&
        record.rdata() == tracked_record.rdata()) {
      records_.erase(entry);
      break;
    }
  }
}

void MdnsQuerier::ProcessRecords(const std::vector<MdnsRecord>& records) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  for (const MdnsRecord& record : records) {
    if (record.dns_type() == DnsType::kNSEC) {
      // TODO(rwkeane): Handle NSEC negative response records.
      continue;
    }

    switch (record.record_type()) {
      case RecordType::kShared: {
        ProcessSharedRecord(record);
        break;
      }
      case RecordType::kUnique: {
        ProcessUniqueRecord(record);
        break;
      }
    }
  }
}

void MdnsQuerier::ProcessSharedRecord(const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(record.record_type() == RecordType::kShared);

  auto records_it = records_.equal_range(record.name());
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    MdnsRecordTracker* tracker = entry->second.get();
    const MdnsRecord& tracked_record = tracker->record();
    if (record.dns_type() == tracked_record.dns_type() &&
        record.dns_class() == tracked_record.dns_class() &&
        record.rdata() == tracked_record.rdata()) {
      // Already have this shared record, update the existing one.
      // This is a TTL only update since we've already checked that RDATA
      // matches. No notification is necessary on a TTL only update.
      // TODO(crbug.com/openscreen/87): Handle errors returned by Update().
      tracker->Update(record);
      return;
    }
  }
  // Have never before seen this shared record, insert a new one.
  AddRecord(record);
  ProcessCallbacks(record, RecordChangedEvent::kCreated);
}

void MdnsQuerier::ProcessUniqueRecord(const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(record.record_type() == RecordType::kUnique);

  int records_for_key = 0;
  auto records_it = records_.equal_range(record.name());
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    const MdnsRecord& tracked_record = entry->second->record();
    if (record.dns_type() == tracked_record.dns_type() &&
        record.dns_class() == tracked_record.dns_class()) {
      ++records_for_key;
    }
  }

  if (records_for_key == 0) {
    // Have not seen any records with this key before.
    AddRecord(record);
    ProcessCallbacks(record, RecordChangedEvent::kCreated);
  } else if (records_for_key == 1) {
    // There's only one record with this key.
    MdnsRecordTracker* tracker = records_it.first->second.get();
    // TODO(crbug.com/openscreen/87): Handle errors returned by Update().
    ErrorOr<MdnsRecordTracker::UpdateType> result = tracker->Update(record);
    if (result.is_value()) {
      switch (result.value()) {
        case MdnsRecordTracker::UpdateType::kGoodbye:
          tracker->ExpireSoon();
          break;
        case MdnsRecordTracker::UpdateType::kTTLOnly:
          // TTL has been updated.  No action required.
          break;
        case MdnsRecordTracker::UpdateType::kRdata:
          // If RDATA on the record is different, notify that the record has
          // been updated.
          ProcessCallbacks(record, RecordChangedEvent::kUpdated);
          break;
      }
    }
  } else {
    // Multiple records with the same key. Expire all record with non-matching
    // RDATA. Update the record with the matching RDATA if it exists, otherwise
    // insert a new record.
    bool is_new_record = true;
    for (auto entry = records_it.first; entry != records_it.second; ++entry) {
      MdnsRecordTracker* tracker = entry->second.get();
      const MdnsRecord& tracked_record = tracker->record();
      if (record.dns_type() == tracked_record.dns_type() &&
          record.dns_class() == tracked_record.dns_class()) {
        if (record.rdata() == tracked_record.rdata()) {
          is_new_record = false;
          // TODO(crbug.com/openscreen/87): Handle errors returned by Update().
          ErrorOr<MdnsRecordTracker::UpdateType> result =
              tracker->Update(record);
          if (result.is_value()) {
            switch (result.value()) {
              case MdnsRecordTracker::UpdateType::kGoodbye:
                tracker->ExpireSoon();
                break;
              case MdnsRecordTracker::UpdateType::kTTLOnly:
                // No notification is necessary on a TTL only update.
                break;
              case MdnsRecordTracker::UpdateType::kRdata:
                // Not possible - we already checked that the RDATA matches.
                OSP_NOTREACHED();
                break;
            }
          }
        } else {
          tracker->ExpireSoon();
        }
      }
    }

    if (is_new_record) {
      // Did not find an existing record to update.
      AddRecord(record);
      ProcessCallbacks(record, RecordChangedEvent::kCreated);
    }
  }
}

void MdnsQuerier::ProcessCallbacks(const MdnsRecord& record,
                                   RecordChangedEvent event) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  auto callbacks_it = callbacks_.equal_range(record.name());
  for (auto entry = callbacks_it.first; entry != callbacks_it.second; ++entry) {
    const CallbackInfo& callback_info = entry->second;
    if ((callback_info.dns_type == DnsType::kANY ||
         record.dns_type() == callback_info.dns_type) &&
        (callback_info.dns_class == DnsClass::kANY ||
         record.dns_class() == callback_info.dns_class)) {
      callback_info.callback->OnRecordChanged(record, event);
    }
  }
}

void MdnsQuerier::AddQuestion(const MdnsQuestion& question) {
  const MdnsQuestionTracker::KnownAnswerQuery query(
      [this](const DomainName& name, DnsType type, DnsClass clazz) {
        return GetKnownAnswers(name, type, clazz);
      });
  questions_.emplace(question.name(),
                     std::make_unique<MdnsQuestionTracker>(
                         std::move(question), query, sender_, task_runner_,
                         now_function_, random_delay_));
}

void MdnsQuerier::AddRecord(const MdnsRecord& record) {
  auto expiration_callback = [this](const MdnsRecord& record) {
    MdnsQuerier::OnRecordExpired(record);
  };
  records_.emplace(record.name(),
                   std::make_unique<MdnsRecordTracker>(
                       std::move(record), sender_, task_runner_, now_function_,
                       random_delay_, expiration_callback));
}

std::vector<MdnsRecord::ConstRef> MdnsQuerier::GetKnownAnswers(
    const DomainName& name,
    DnsType type,
    DnsClass clazz) {
  std::vector<MdnsRecord::ConstRef> records;
  auto its = records_.equal_range(name);
  for (auto it = its.first; it != its.second; it++) {
    const MdnsRecord& record = it->second->record();
    if ((type == DnsType::kANY || type == record.dns_type()) &&
        (clazz == DnsClass::kANY || clazz == record.dns_class()) &&
        !it->second->IsNearingExpiry()) {
      records.emplace_back(record);
    }
  }

  return records;
}

}  // namespace discovery
}  // namespace openscreen
