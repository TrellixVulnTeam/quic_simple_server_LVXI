// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_ROUTER_PROVIDERS_CAST_CAST_ACTIVITY_RECORD_H_
#define CHROME_BROWSER_MEDIA_ROUTER_PROVIDERS_CAST_CAST_ACTIVITY_RECORD_H_

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "base/optional.h"
#include "chrome/browser/media/router/providers/cast/activity_record.h"
#include "chrome/browser/media/router/providers/cast/cast_media_controller.h"
#include "chrome/common/media_router/mojom/media_router.mojom.h"
#include "chrome/common/media_router/providers/cast/cast_media_source.h"
#include "components/cast_channel/cast_message_handler.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "third_party/openscreen/src/cast/common/channel/proto/cast_channel.pb.h"

namespace url {
class Origin;
}

namespace media_router {

class CastActivityRecord;
class CastInternalMessage;
class CastSession;
class CastSessionClient;
class CastSessionClientFactoryForTest;
class CastSessionTracker;
class MediaRoute;

class CastActivityRecordFactoryForTest {
 public:
  virtual std::unique_ptr<CastActivityRecord> MakeCastActivityRecord(
      const MediaRoute& route,
      const std::string& app_id) = 0;
};

class CastActivityRecord : public ActivityRecord {
 public:
  using ClientMap =
      base::flat_map<std::string, std::unique_ptr<CastSessionClient>>;

  // Creates a new record owned by |owner|.
  CastActivityRecord(const MediaRoute& route,
                     const std::string& app_id,
                     cast_channel::CastMessageHandler* message_handler,
                     CastSessionTracker* session_tracker);
  ~CastActivityRecord() override;

  // ActivityRecord implementation
  void SendStopSessionMessageToClients(const std::string& hash_token) override;
  void SetOrUpdateSession(const CastSession& session,
                          const MediaSinkInternal& sink,
                          const std::string& hash_token) override;
  void SendMessageToClient(
      const std::string& client_id,
      blink::mojom::PresentationConnectionMessagePtr message) override;
  void SendMediaStatusToClients(const base::Value& media_status,
                                base::Optional<int> request_id) override;
  void ClosePresentationConnections(
      blink::mojom::PresentationConnectionCloseReason close_reason) override;
  void TerminatePresentationConnections() override;
  void OnAppMessage(const cast::channel::CastMessage& message) override;
  void OnInternalMessage(const cast_channel::InternalMessage& message) override;
  void CreateMediaController(
      mojo::PendingReceiver<mojom::MediaController> media_controller,
      mojo::PendingRemote<mojom::MediaStatusObserver> observer) override;

  // Sends media command |cast_message|, which came from the SDK client, to the
  // receiver hosting this session. Returns the locally-assigned request ID of
  // the message sent to the receiver.
  virtual base::Optional<int> SendMediaRequestToReceiver(
      const CastInternalMessage& cast_message);

  // Sends app message |cast_message|, which came from the SDK client, to the
  // receiver hosting this session. Returns true if the message is sent
  // successfully.
  virtual cast_channel::Result SendAppMessageToReceiver(
      const CastInternalMessage& cast_message);

  // Sends a SET_VOLUME request to the receiver and calls |callback| when a
  // response indicating whether the request succeeded is received.
  virtual void SendSetVolumeRequestToReceiver(
      const CastInternalMessage& cast_message,
      cast_channel::ResultCallback callback);

  // Called when the client given by |client_id| requests to leave the session.
  // This will also cause all clients within the session with matching origin
  // and/or tab ID to leave (i.e., their presentation connections will be
  // closed).
  virtual void HandleLeaveSession(const std::string& client_id);

  // Adds a new client |client_id| to this session and returns the handles of
  // the two pipes to be held by Blink It is invalid to call this method if the
  // client already exists.
  virtual mojom::RoutePresentationConnectionPtr AddClient(
      const CastMediaSource& source,
      const url::Origin& origin,
      int tab_id);

  virtual void RemoveClient(const std::string& client_id);

  bool CanJoinSession(const CastMediaSource& cast_source, bool incognito) const;
  bool HasJoinableClient(AutoJoinPolicy policy,
                         const url::Origin& origin,
                         int tab_id) const;

  static void SetClientFactoryForTest(
      CastSessionClientFactoryForTest* factory) {
    client_factory_for_test_ = factory;
  }

 private:
  friend class CastSessionClientImpl;
  friend class CastActivityManager;
  friend class CastActivityRecordTest;

  static CastSessionClientFactoryForTest* client_factory_for_test_;

  int cast_channel_id() const { return sink_.cast_channel_id(); }

  CastSessionClient* GetClient(const std::string& client_id) {
    auto it = connected_clients_.find(client_id);
    return it == connected_clients_.end() ? nullptr : it->second.get();
  }

  ClientMap connected_clients_;
  std::unique_ptr<CastMediaController> media_controller_;
};

}  // namespace media_router

#endif  // CHROME_BROWSER_MEDIA_ROUTER_PROVIDERS_CAST_CAST_ACTIVITY_RECORD_H_
