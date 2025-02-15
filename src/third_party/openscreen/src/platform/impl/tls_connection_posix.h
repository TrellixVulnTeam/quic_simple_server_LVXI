// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_IMPL_TLS_CONNECTION_POSIX_H_
#define PLATFORM_IMPL_TLS_CONNECTION_POSIX_H_

#include <openssl/ssl.h>

#include <memory>

#include "platform/api/tls_connection.h"
#include "platform/impl/platform_client_posix.h"
#include "platform/impl/stream_socket_posix.h"
#include "platform/impl/tls_write_buffer.h"
#include "platform/impl/weak_ptr.h"

namespace openscreen {

class TaskRunner;
class TlsConnectionFactoryPosix;

class TlsConnectionPosix : public TlsConnection {
 public:
  ~TlsConnectionPosix() override;

  // Sends any available bytes from this connection's buffer_.
  virtual void SendAvailableBytes();

  // Read out a block/message, if one is available, and notify this instance's
  // TlsConnection::Client.
  virtual void TryReceiveMessage();

  // TlsConnection overrides.
  void SetClient(Client* client) override;
  bool Send(const void* data, size_t len) override;
  IPEndpoint GetLocalEndpoint() const override;
  IPEndpoint GetRemoteEndpoint() const override;

 protected:
  friend class TlsConnectionFactoryPosix;

  TlsConnectionPosix(IPEndpoint local_address,
                     TaskRunner* task_runner,
                     PlatformClientPosix* platform_client =
                         PlatformClientPosix::GetInstance());
  TlsConnectionPosix(IPAddress::Version version,
                     TaskRunner* task_runner,
                     PlatformClientPosix* platform_client =
                         PlatformClientPosix::GetInstance());
  TlsConnectionPosix(std::unique_ptr<StreamSocket> socket,
                     TaskRunner* task_runner,
                     PlatformClientPosix* platform_client =
                         PlatformClientPosix::GetInstance());

 private:
  // Called on any thread, to post a task to notify the Client that an |error|
  // has occurred.
  void DispatchError(Error error);

  TaskRunner* const task_runner_;
  PlatformClientPosix* const platform_client_;

  Client* client_ = nullptr;

  std::unique_ptr<StreamSocket> socket_;
  bssl::UniquePtr<SSL> ssl_;

  TlsWriteBuffer buffer_;

  WeakPtrFactory<TlsConnectionPosix> weak_factory_{this};

  OSP_DISALLOW_COPY_AND_ASSIGN(TlsConnectionPosix);
};

}  // namespace openscreen

#endif  // PLATFORM_IMPL_TLS_CONNECTION_POSIX_H_
