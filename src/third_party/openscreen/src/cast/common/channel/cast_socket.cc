// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cast/common/channel/cast_socket.h"

#include <atomic>

#include "cast/common/channel/message_framer.h"
#include "util/logging.h"

namespace openscreen {
namespace cast {

using ::cast::channel::CastMessage;
using message_serialization::DeserializeResult;

uint32_t GetNextSocketId() {
  static std::atomic<uint32_t> id(1);
  return id++;
}

CastSocket::CastSocket(std::unique_ptr<TlsConnection> connection,
                       Client* client,
                       uint32_t socket_id)
    : connection_(std::move(connection)),
      client_(client),
      socket_id_(socket_id) {
  OSP_DCHECK(client);
  connection_->SetClient(this);
}

CastSocket::~CastSocket() {
  connection_->SetClient(nullptr);
}

Error CastSocket::SendMessage(const CastMessage& message) {
  if (state_ == State::kError) {
    return Error::Code::kSocketClosedFailure;
  }

  const ErrorOr<std::vector<uint8_t>> out =
      message_serialization::Serialize(message);
  if (!out) {
    return out.error();
  }

  if (!connection_->Send(out.value().data(), out.value().size())) {
    return Error::Code::kAgain;
  }
  return Error::Code::kNone;
}

void CastSocket::SetClient(Client* client) {
  OSP_DCHECK(client);
  client_ = client;
}

std::array<uint8_t, 2> CastSocket::GetSanitizedIpAddress() {
  IPEndpoint remote = connection_->GetRemoteEndpoint();
  std::array<uint8_t, 2> result;
  uint8_t bytes[16];
  if (remote.address.IsV4()) {
    remote.address.CopyToV4(bytes);
    result[0] = bytes[2];
    result[1] = bytes[3];
  } else {
    remote.address.CopyToV6(bytes);
    result[0] = bytes[14];
    result[1] = bytes[15];
  }
  return result;
}

void CastSocket::OnError(TlsConnection* connection, Error error) {
  state_ = State::kError;
  client_->OnError(this, error);
}

void CastSocket::OnRead(TlsConnection* connection, std::vector<uint8_t> block) {
  read_buffer_.insert(read_buffer_.end(), block.begin(), block.end());
  ErrorOr<DeserializeResult> message_or_error =
      message_serialization::TryDeserialize(
          absl::Span<uint8_t>(&read_buffer_[0], read_buffer_.size()));
  if (!message_or_error) {
    return;
  }
  read_buffer_.erase(read_buffer_.begin(),
                     read_buffer_.begin() + message_or_error.value().length);
  client_->OnMessage(this, std::move(message_or_error.value().message));
}

}  // namespace cast
}  // namespace openscreen
