// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_ENGINE_NON_BLOCKING_SYNC_COMMON_H_
#define COMPONENTS_SYNC_ENGINE_NON_BLOCKING_SYNC_COMMON_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/time/time.h"
#include "components/sync/base/client_tag_hash.h"
#include "components/sync/model/entity_data.h"
#include "components/sync/protocol/sync.pb.h"

namespace syncer {

static const int64_t kUncommittedVersion = -1;

struct CommitRequestData {
  CommitRequestData();
  ~CommitRequestData();

  // Fields sent to the sync server.
  std::unique_ptr<EntityData> entity;
  int64_t base_version = 0;

  // Fields not sent to the sync server. However, they are kept to be sent back
  // to the processor in the response.

  // Strictly incrementing number for in-progress commits.
  // More information about its meaning can be found in comments in the files
  // that make use of this struct.
  int64_t sequence_number = 0;
  std::string specifics_hash;
  base::Time unsynced_time;

 private:
  DISALLOW_COPY_AND_ASSIGN(CommitRequestData);
};

// Represents a successfully committed item.
struct CommitResponseData {
  CommitResponseData();
  CommitResponseData(const CommitResponseData& other);
  CommitResponseData(CommitResponseData&&);
  CommitResponseData& operator=(const CommitResponseData&);
  CommitResponseData& operator=(CommitResponseData&&);
  ~CommitResponseData();

  std::string id;
  // The sync id that was sent in the request. Non-empty only if different from
  // |id|. It could be different because the server can change the sync id
  // (e.g. for newly created bookmarks),
  std::string id_in_request;
  ClientTagHash client_tag_hash;
  int64_t sequence_number = 0;
  int64_t response_version = 0;
  std::string specifics_hash;
  base::Time unsynced_time;
};

// Represents an item, which wasn't committed due to an error.
struct FailedCommitResponseData {
  FailedCommitResponseData();
  FailedCommitResponseData(const FailedCommitResponseData& other);
  FailedCommitResponseData(FailedCommitResponseData&&);
  FailedCommitResponseData& operator=(const FailedCommitResponseData&);
  FailedCommitResponseData& operator=(FailedCommitResponseData&&);
  ~FailedCommitResponseData();

  ClientTagHash client_tag_hash;
  sync_pb::CommitResponse::ResponseType response_type =
      sync_pb::CommitResponse::TRANSIENT_ERROR;

  sync_pb::CommitResponse::EntryResponse::DatatypeSpecificError
      datatype_specific_error;
};

struct UpdateResponseData {
  UpdateResponseData();
  UpdateResponseData(UpdateResponseData&&) = default;
  UpdateResponseData& operator=(UpdateResponseData&&) = default;
  ~UpdateResponseData();

  UpdateResponseData(const UpdateResponseData&) = delete;
  UpdateResponseData& operator=(const UpdateResponseData&) = delete;

  EntityData entity;

  int64_t response_version = 0;
  std::string encryption_key_name;
};

using CommitRequestDataList = std::vector<std::unique_ptr<CommitRequestData>>;
using CommitResponseDataList = std::vector<CommitResponseData>;
using FailedCommitResponseDataList = std::vector<FailedCommitResponseData>;
using UpdateResponseDataList = std::vector<UpdateResponseData>;

// Returns the estimate of dynamically allocated memory in bytes.
size_t EstimateMemoryUsage(const CommitRequestData& value);
size_t EstimateMemoryUsage(const UpdateResponseData& value);

}  // namespace syncer

#endif  // COMPONENTS_SYNC_ENGINE_NON_BLOCKING_SYNC_COMMON_H_
