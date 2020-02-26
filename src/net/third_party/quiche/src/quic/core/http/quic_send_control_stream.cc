// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"
#include <cstdint>
#include <memory>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QuicSendControlStream::QuicSendControlStream(
    QuicStreamId id,
    QuicSession* session,
    uint64_t qpack_maximum_dynamic_table_capacity,
    uint64_t qpack_maximum_blocked_streams,
    uint64_t max_inbound_header_list_size)
    : QuicStream(id, session, /*is_static = */ true, WRITE_UNIDIRECTIONAL),
      settings_sent_(false),
      qpack_maximum_dynamic_table_capacity_(
          qpack_maximum_dynamic_table_capacity),
      qpack_maximum_blocked_streams_(qpack_maximum_blocked_streams),
      max_inbound_header_list_size_(max_inbound_header_list_size) {}

void QuicSendControlStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  // TODO(renjietang) Change the error code to H/3 specific
  // HTTP_CLOSED_CRITICAL_STREAM.
  session()->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Attempt to reset send control stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSendControlStream::MaybeSendSettingsFrame() {
  if (settings_sent_) {
    return;
  }

  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  // Send the stream type on so the peer knows about this stream.
  char data[sizeof(kControlStream)];
  QuicDataWriter writer(QUICHE_ARRAYSIZE(data), data);
  writer.WriteVarInt62(kControlStream);
  WriteOrBufferData(quiche::QuicheStringPiece(writer.data(), writer.length()),
                    false, nullptr);

  SettingsFrame settings;
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] =
      qpack_maximum_dynamic_table_capacity_;
  settings.values[SETTINGS_QPACK_BLOCKED_STREAMS] =
      qpack_maximum_blocked_streams_;
  settings.values[SETTINGS_MAX_HEADER_LIST_SIZE] =
      max_inbound_header_list_size_;
  // https://tools.ietf.org/html/draft-ietf-quic-http-25#section-7.2.4.1
  // specifies that setting identifiers of 0x1f * N + 0x21 are reserved and
  // greasing should be attempted.
  if (GetQuicFlag(FLAGS_quic_disable_http3_settings_grease_randomness)) {
    settings.values[0x40] = 20;
  } else {
    uint32_t result;
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
    uint64_t setting_id = 0x1fULL * static_cast<uint64_t>(result) + 0x21ULL;
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
    settings.values[setting_id] = result;
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializeSettingsFrame(settings, &buffer);
  QUIC_DVLOG(1) << "Control stream " << id() << " is writing settings frame "
                << settings;
  QuicSpdySession* spdy_session = static_cast<QuicSpdySession*>(session());
  if (spdy_session->debug_visitor() != nullptr) {
    spdy_session->debug_visitor()->OnSettingsFrameSent(settings);
  }
  WriteOrBufferData(quiche::QuicheStringPiece(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
  settings_sent_ = true;
}

void QuicSendControlStream::WritePriorityUpdate(
    const PriorityUpdateFrame& priority_update) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendSettingsFrame();
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializePriorityUpdateFrame(priority_update, &buffer);
  QUIC_DVLOG(1) << "Control Stream " << id() << " is writing "
                << priority_update;
  WriteOrBufferData(quiche::QuicheStringPiece(buffer.get(), frame_length),
                    false, nullptr);
}

void QuicSendControlStream::SendMaxPushIdFrame(PushId max_push_id) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());

  MaybeSendSettingsFrame();
  MaxPushIdFrame frame;
  frame.push_id = max_push_id;
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializeMaxPushIdFrame(frame, &buffer);
  WriteOrBufferData(quiche::QuicheStringPiece(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
}

void QuicSendControlStream::SendGoAway(QuicStreamId stream_id) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());

  MaybeSendSettingsFrame();
  GoAwayFrame frame;
  // If the peer hasn't created any stream yet. Use stream id 0 to indicate no
  // request is accepted.
  if (stream_id ==
      QuicUtils::GetInvalidStreamId(session()->transport_version())) {
    stream_id = 0;
  }
  frame.stream_id = stream_id;
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializeGoAwayFrame(frame, &buffer);
  WriteOrBufferData(quiche::QuicheStringPiece(buffer.get(), frame_length),
                    false, nullptr);
}

}  // namespace quic
