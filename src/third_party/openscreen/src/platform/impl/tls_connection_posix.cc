// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/impl/tls_connection_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <openssl/ssl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <utility>

#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "platform/api/task_runner.h"
#include "platform/base/error.h"
#include "platform/impl/stream_socket.h"
#include "util/crypto/openssl_util.h"
#include "util/logging.h"

namespace openscreen {

// TODO(jophba, rwkeane): implement write blocking/unblocking
TlsConnectionPosix::TlsConnectionPosix(IPEndpoint local_address,
                                       TaskRunner* task_runner,
                                       PlatformClientPosix* platform_client)
    : task_runner_(task_runner),
      platform_client_(platform_client),
      socket_(std::make_unique<StreamSocketPosix>(local_address)) {
  OSP_DCHECK(task_runner_);
  if (platform_client_) {
    platform_client_->tls_data_router()->RegisterConnection(this);
  }
}

TlsConnectionPosix::TlsConnectionPosix(IPAddress::Version version,
                                       TaskRunner* task_runner,
                                       PlatformClientPosix* platform_client)
    : task_runner_(task_runner),
      platform_client_(platform_client),
      socket_(std::make_unique<StreamSocketPosix>(version)) {
  OSP_DCHECK(task_runner_);
  if (platform_client_) {
    platform_client_->tls_data_router()->RegisterConnection(this);
  }
}

TlsConnectionPosix::TlsConnectionPosix(std::unique_ptr<StreamSocket> socket,
                                       TaskRunner* task_runner,
                                       PlatformClientPosix* platform_client)
    : task_runner_(task_runner),
      platform_client_(platform_client),
      socket_(std::move(socket)) {
  OSP_DCHECK(task_runner_);
  if (platform_client_) {
    platform_client_->tls_data_router()->RegisterConnection(this);
  }
}

TlsConnectionPosix::~TlsConnectionPosix() {
  if (platform_client_) {
    platform_client_->tls_data_router()->DeregisterConnection(this);
  }
}

void TlsConnectionPosix::TryReceiveMessage() {
  const int bytes_available = SSL_pending(ssl_.get());
  if (bytes_available > 0) {
    // NOTE: the pending size of the data block available is not a guarantee
    // that it will receive only bytes_available or even
    // any data, since not all pending bytes are application data.
    std::vector<uint8_t> block(bytes_available);

    const int bytes_read = SSL_read(ssl_.get(), block.data(), bytes_available);

    // Read operator was not successful, either due to a closed connection,
    // an error occurred, or we have to take an action.
    if (bytes_read <= 0) {
      const Error error = GetSSLError(ssl_.get(), bytes_read);
      if (!error.ok() && (error != Error::Code::kAgain)) {
        DispatchError(error);
      }
      return;
    }

    block.resize(bytes_read);

    task_runner_->PostTask([weak_this = weak_factory_.GetWeakPtr(),
                            moved_block = std::move(block)]() mutable {
      if (auto* self = weak_this.get()) {
        if (auto* client = self->client_) {
          client->OnRead(self, std::move(moved_block));
        }
      }
    });
  }
}

void TlsConnectionPosix::SetClient(Client* client) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  client_ = client;
}

bool TlsConnectionPosix::Send(const void* data, size_t len) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  return buffer_.Push(data, len);
}

IPEndpoint TlsConnectionPosix::GetLocalEndpoint() const {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  absl::optional<IPEndpoint> endpoint = socket_->local_address();
  OSP_DCHECK(endpoint.has_value());
  return endpoint.value();
}

IPEndpoint TlsConnectionPosix::GetRemoteEndpoint() const {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  absl::optional<IPEndpoint> endpoint = socket_->remote_address();
  OSP_DCHECK(endpoint.has_value());
  return endpoint.value();
}

void TlsConnectionPosix::SendAvailableBytes() {
  absl::Span<const uint8_t> sendable_bytes = buffer_.GetReadableRegion();
  if (sendable_bytes.empty()) {
    return;
  }

  const int result =
      SSL_write(ssl_.get(), sendable_bytes.data(), sendable_bytes.size());
  if (result <= 0) {
    const Error result_error = GetSSLError(ssl_.get(), result);
    if (!result_error.ok() && (result_error.code() != Error::Code::kAgain)) {
      DispatchError(result_error);
    }
  } else {
    buffer_.Consume(static_cast<size_t>(result));
  }
}

void TlsConnectionPosix::DispatchError(Error error) {
  task_runner_->PostTask([weak_this = weak_factory_.GetWeakPtr(),
                          moved_error = std::move(error)]() mutable {
    if (auto* self = weak_this.get()) {
      if (auto* client = self->client_) {
        client->OnError(self, std::move(moved_error));
      }
    }
  });
}

}  // namespace openscreen
