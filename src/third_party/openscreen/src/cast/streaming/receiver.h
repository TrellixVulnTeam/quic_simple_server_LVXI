// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAST_STREAMING_RECEIVER_H_
#define CAST_STREAMING_RECEIVER_H_

#include <stdint.h>

#include <array>
#include <chrono>  // NOLINT
#include <memory>
#include <vector>

#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "cast/streaming/clock_drift_smoother.h"
#include "cast/streaming/compound_rtcp_builder.h"
#include "cast/streaming/environment.h"
#include "cast/streaming/frame_collector.h"
#include "cast/streaming/frame_id.h"
#include "cast/streaming/packet_receive_stats_tracker.h"
#include "cast/streaming/rtcp_common.h"
#include "cast/streaming/rtcp_session.h"
#include "cast/streaming/rtp_packet_parser.h"
#include "cast/streaming/sender_report_parser.h"
#include "cast/streaming/session_config.h"
#include "cast/streaming/ssrc.h"
#include "platform/api/time.h"
#include "util/alarm.h"

namespace openscreen {
namespace cast {

struct EncodedFrame;
class ReceiverPacketRouter;

// The Cast Streaming Receiver, a peer corresponding to some Cast Streaming
// Sender at the other end of a network link.
//
// Cast Streaming is a transport protocol which divides up the frames for one
// media stream (e.g., audio or video) into multiple RTP packets containing an
// encrypted payload. The Receiver is the peer responsible for collecting the
// RTP packets, decrypting the payload, and re-assembling a frame that can be
// passed to a decoder and played out.
//
// A Sender ↔ Receiver pair is used to transport each media stream. Typically,
// there are two pairs in a normal system, one for the audio stream and one for
// video stream. A local player is responsible for synchronizing the playout of
// the frames of each stream to achieve lip-sync. See the discussion in
// encoded_frame.h for how the |reference_time| and |rtp_timestamp| of the
// EncodedFrames are used to achieve this.
//
// See the Receiver Demo app for a reference implementation that both shows and
// explains how Receivers are properly configured and started, integrated with a
// decoder, and the resulting decoded media is played out. Also, here is a
// general usage example:
//
//   class MyPlayer : public openscreen::cast::Receiver::Consumer {
//    public:
//     explicit MyPlayer(Receiver* receiver) : receiver_(receiver) {
//       recevier_->SetPlayerProcessingTime(std::chrono::milliseconds(10));
//       receiver_->SetConsumer(this);
//     }
//
//     ~MyPlayer() override {
//       receiver_->SetConsumer(nullptr);
//     }
//
//    private:
//     // Receiver::Consumer implementation.
//     void OnFramesReady(int next_frame_buffer_size) override {
//       std::vector<uint8_t> buffer;
//       buffer.resize(next_frame_buffer_size);
//       openscreen::cast::EncodedFrame encoded_frame =
//           receiver_->ConsumeNextFrame(absl::Span<uint8_t>(buffer));
//
//       display_.RenderFrame(decoder_.DecodeFrame(encoded_frame.data));
//
//       // Note: An implementation could call receiver_->AdvanceToNextFrame()
//       // and receiver_->ConsumeNextFrame() in a loop here, to consume all the
//       // remaining frames that are ready.
//     }
//
//     Receiver* const receiver_;
//     MyDecoder decoder_;
//     MyDisplay display_;
//   };
//
// Internally, a queue of complete and partially-received frames is maintained.
// The queue is a circular queue of FrameCollectors that each maintain the
// individual receive state of each in-flight frame. There are three conceptual
// "pointers" that indicate what assumptions and operations are made on certain
// ranges of frames in the queue:
//
//   1. Latest Frame Expected: The FrameId of the latest frame whose existence
//      is known to this Receiver. This is the highest FrameId seen in any
//      successfully-parsed RTP packet.
//   2. Checkpoint Frame: Indicates that all of the RTP packets for all frames
//      up to and including the one having this FrameId have been successfully
//      received and processed.
//   3. Last Frame Consumed: The FrameId of last frame consumed (see
//      ConsumeNextFrame()). Once a frame is consumed, all internal resources
//      related to the frame can be freed and/or re-used for later frames.
class Receiver {
 public:
  class Consumer {
   public:
    virtual ~Consumer();

    // Called whenever one or more frames have become ready for consumption. The
    // |next_frame_buffer_size| argument is identical to the result of calling
    // AdvanceToNextFrame(), and so the Consumer only needs to prepare a buffer
    // and call ConsumeNextFrame(). It may then call AdvanceToNextFrame() to
    // check whether there are any more frames ready, but this is not mandatory.
    // See usage example in class-level comments.
    virtual void OnFramesReady(int next_frame_buffer_size) = 0;
  };

  // Constructs a Receiver that attaches to the given |environment| and
  // |packet_router|. The config contains the settings that were
  // agreed-upon by both sides from the OFFER/ANSWER exchange (i.e., the part of
  // the overall end-to-end connection process that occurs before Cast Streaming
  // is started).
  Receiver(Environment* environment,
           ReceiverPacketRouter* packet_router,
           const SessionConfig& config);
  ~Receiver();

  Ssrc ssrc() const { return rtcp_session_.receiver_ssrc(); }
  int rtp_timebase() const { return rtp_timebase_; }

  // Set the Consumer receiving notifications when new frames are ready for
  // consumption. Frames received before this method is called will remain in
  // the queue indefinitely.
  void SetConsumer(Consumer* consumer);

  // Sets how much time the consumer will need to decode/buffer/render/etc., and
  // otherwise fully process a frame for on-time playback. This information is
  // used by the Receiver to decide whether to skip past frames that have
  // arrived too late. This method can be called repeatedly to make adjustments
  // based on changing environmental conditions.
  //
  // Default setting: kDefaultPlayerProcessingTime
  void SetPlayerProcessingTime(Clock::duration needed_time);

  // Propagates a "picture loss indicator" notification to the Sender,
  // requesting a key frame so that decode/playout can recover. It is safe to
  // call this redundantly. The Receiver will clear the picture loss condition
  // automatically, once a key frame is received (i.e., before
  // ConsumeNextFrame() is called to access it).
  void RequestKeyFrame();

  // Advances to the next frame ready for consumption. This may skip-over
  // incomplete frames that will not play out on-time; but only if there are
  // completed frames further down the queue that have no dependency
  // relationship with them (e.g., key frames).
  //
  // This method returns kNoFramesReady if there is not currently a frame ready
  // for consumption. The caller should wait for a Consumer::OnFramesReady()
  // notification before trying again. Otherwise, the number of bytes of encoded
  // data is returned, and the caller should use this to ensure the buffer it
  // passes to ConsumeNextFrame() is large enough.
  int AdvanceToNextFrame();

  // Returns the next frame, both metadata and payload data. The Consumer calls
  // this method after being notified via OnFramesReady(), and it can also call
  // this whenever AdvanceToNextFrame() indicates another frame is ready.
  // |buffer| must point to a sufficiently-sized buffer that will be populated
  // with the frame's payload data. Upon return |frame->data| will be set to the
  // portion of the buffer that was populated.
  EncodedFrame ConsumeNextFrame(absl::Span<uint8_t> buffer);

  // The default "player processing time" amount. See SetPlayerProcessingTime().
  static constexpr std::chrono::milliseconds kDefaultPlayerProcessingTime{5};

  // Returned by AdvanceToNextFrame() when there are no frames currently ready
  // for consumption.
  static constexpr int kNoFramesReady = -1;

 protected:
  friend class ReceiverPacketRouter;

  // Called by ReceiverPacketRouter to provide this Receiver with what looks
  // like a RTP/RTCP packet meant for it specifically (among other Receivers).
  void OnReceivedRtpPacket(Clock::time_point arrival_time,
                           std::vector<uint8_t> packet);
  void OnReceivedRtcpPacket(Clock::time_point arrival_time,
                            std::vector<uint8_t> packet);

 private:
  // An entry in the circular queue (see |pending_frames_|).
  struct PendingFrame {
    FrameCollector collector;

    // The Receiver's [local] Clock time when this frame was originally captured
    // at the Sender. This is computed and assigned when the RTP packet with ID
    // 0 is processed. Add the target playout delay to this to get the target
    // playout time.
    absl::optional<Clock::time_point> estimated_capture_time;

    PendingFrame();
    ~PendingFrame();

    // Reset this entry to its initial state, freeing resources.
    void Reset();
  };

  // Get/Set the checkpoint FrameId. This indicates that all of the packets for
  // all frames up to and including this FrameId have been successfully received
  // (or otherwise do not need to be re-transmitted).
  FrameId checkpoint_frame() const { return rtcp_builder_.checkpoint_frame(); }
  void set_checkpoint_frame(FrameId frame_id) {
    rtcp_builder_.SetCheckpointFrame(frame_id);
  }

  // Send an RTCP packet to the Sender immediately, to acknowledge the complete
  // reception of one or more additional frames, to reply to a Sender Report, or
  // to request re-transmits. Calling this also schedules additional RTCP
  // packets to be sent periodically for the life of this Receiver.
  void SendRtcp();

  // Helpers to map the given |frame_id| to the element in the |pending_frames_|
  // circular queue. There are both const and non-const versions, but neither
  // mutate any state (i.e., they are just look-ups).
  const PendingFrame& GetQueueEntry(FrameId frame_id) const;
  PendingFrame& GetQueueEntry(FrameId frame_id);

  // Record that the target playout delay has changed starting with the given
  // FrameId.
  void RecordNewTargetPlayoutDelay(FrameId as_of_frame,
                                   std::chrono::milliseconds delay);

  // Examine the known target playout delay changes to determine what setting is
  // in-effect for the given frame.
  std::chrono::milliseconds ResolveTargetPlayoutDelay(FrameId frame_id) const;

  // Called to move the checkpoint forward. This scans the queue, starting from
  // |new_checkpoint|, to find the latest in a contiguous sequence of completed
  // frames. Then, it records that frame as the new checkpoint, and immediately
  // sends a feedback RTCP packet to the Sender.
  void AdvanceCheckpoint(FrameId new_checkpoint);

  // Helper to force-drop all frames before |first_kept_frame|, even if they
  // were never consumed. This will also auto-cancel frames that were never
  // completely received, artificially moving the checkpoint forward, and
  // notifying the Sender of that. The caller of this method is responsible for
  // making sure that frame data dependencies will not be broken by dropping the
  // frames.
  void DropAllFramesBefore(FrameId first_kept_frame);

  // Sets the |consumption_alarm_| to check whether any frames are ready,
  // including possibly skipping over late frames in order to make not-yet-late
  // frames become ready. The default argument value means "without delay."
  void ScheduleFrameReadyCheck(Clock::time_point when = {});

  const ClockNowFunctionPtr now_;
  ReceiverPacketRouter* const packet_router_;
  RtcpSession rtcp_session_;
  SenderReportParser rtcp_parser_;
  CompoundRtcpBuilder rtcp_builder_;
  PacketReceiveStatsTracker stats_tracker_;  // Tracks transmission stats.
  RtpPacketParser rtp_parser_;
  const int rtp_timebase_;    // RTP timestamp ticks per second.
  const FrameCrypto crypto_;  // Decrypts assembled frames.

  // Buffer for serializing/sending RTCP packets.
  const int rtcp_buffer_capacity_;
  const std::unique_ptr<uint8_t[]> rtcp_buffer_;

  // Schedules tasks to ensure RTCP reports are sent within a bounded interval.
  // Not scheduled until after this Receiver has processed the first packet from
  // the Sender.
  Alarm rtcp_alarm_;
  Clock::time_point last_rtcp_send_time_ = Clock::time_point::min();

  // The last Sender Report received and when the packet containing it had
  // arrived. This contains lip-sync timestamps used as part of the calculation
  // of playout times for the received frames, as well as ping-pong data bounced
  // back to the Sender in the Receiver Reports. It is nullopt until the first
  // parseable Sender Report is received.
  absl::optional<SenderReportParser::SenderReportWithId> last_sender_report_;
  Clock::time_point last_sender_report_arrival_time_;

  // Tracks the offset between the Receiver's [local] clock and the Sender's
  // clock. This is invalid until the first Sender Report has been successfully
  // processed (i.e., |last_sender_report_| is not nullopt).
  ClockDriftSmoother smoothed_clock_offset_;

  // The ID of the latest frame whose existence is known to this Receiver. This
  // value must always be greater than or equal to |checkpoint_frame()|.
  FrameId latest_frame_expected_ = FrameId::first() - 1;

  // The ID of the last frame consumed. This value must always be less than or
  // equal to |checkpoint_frame()|, since it's impossible to consume incomplete
  // frames!
  FrameId last_frame_consumed_ = FrameId::first() - 1;

  // The ID of the latest key frame known to be in-flight. This is used by
  // RequestKeyFrame() to ensure the PLI condition doesn't get set again until
  // after the consumer has seen a key frame that would clear the condition.
  FrameId last_key_frame_received_;

  // The frame queue (circular), which tracks which frames are in-flight, stores
  // data for partially-received frames, and holds onto completed frames until
  // the consumer consumes them.
  //
  // Use GetQueueEntry() to access a slot. The currently-active slots are those
  // for the frames after |last_frame_consumed_| and up-to/including
  // |latest_frame_expected_|.
  std::array<PendingFrame, kMaxUnackedFrames> pending_frames_{};

  // Tracks the recent changes to the target playout delay, which is controlled
  // by the Sender. The FrameId indicates the first frame where a new delay
  // setting takes effect. This vector is never empty, is kept sorted, and is
  // pruned to remain as small as possible.
  //
  // The target playout delay is the amount of time between a frame's
  // capture/recording on the Sender and when it should be played-out at the
  // Receiver.
  std::vector<std::pair<FrameId, std::chrono::milliseconds>>
      playout_delay_changes_;

  // The consumer to notify when there are one or more frames completed and
  // ready to be consumed.
  Consumer* consumer_ = nullptr;

  // The additional time needed to decode/play-out each frame after being
  // consumed from this Receiver.
  Clock::duration player_processing_time_ = kDefaultPlayerProcessingTime;

  // Scheduled to check whether there are frames ready and, if there are, to
  // notify the Consumer via OnFramesReady().
  Alarm consumption_alarm_;

  // The interval between sending ACK/NACK feedback RTCP messages while
  // incomplete frames exist in the queue.
  static constexpr std::chrono::milliseconds kNackFeedbackInterval{30};
};

}  // namespace cast
}  // namespace openscreen

#endif  // CAST_STREAMING_RECEIVER_H_
