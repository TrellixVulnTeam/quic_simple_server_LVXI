// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SHARING_SHARING_CONSTANTS_H_
#define CHROME_BROWSER_SHARING_SHARING_CONSTANTS_H_

#include "base/time/time.h"
#include "net/base/backoff_entry.h"

// App ID linked to FCM messages for Sharing.
extern const char kSharingFCMAppID[];

// Sender ID for Sharing.
extern const char kSharingSenderID[];

// Amount of time before FCM registration should happen again.
extern const base::TimeDelta kRegistrationExpiration;

// Time until we treat a WebRTC connection as timed out and force close it.
extern const base::TimeDelta kSharingWebRtcTimeout;

// Backoff policy for registration retry.
extern const net::BackoffEntry::Policy kRetryBackoffPolicy;

// Maximum number of devices to be shown in dialog and context menu.
extern const int kMaxDevicesShown;

// Command id for first device shown in submenu.
extern const int kSubMenuFirstDeviceCommandId;

// Command id for last device shown in submenu.
extern const int kSubMenuLastDeviceCommandId;

// The feature name prefix used in metrics name.
enum class SharingFeatureName {
  kUnknown,
  kClickToCall,
  kSharedClipboard,
  kMaxValue = kSharedClipboard,
};

// The device platform that the user is sharing from/with.
enum class SharingDevicePlatform {
  kUnknown,
  kAndroid,
  kChromeOS,
  kIOS,
  kLinux,
  kMac,
  kWindows,
};

#endif  // CHROME_BROWSER_SHARING_SHARING_CONSTANTS_H_
