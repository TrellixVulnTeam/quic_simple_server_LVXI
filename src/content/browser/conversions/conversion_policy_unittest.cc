// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/conversions/conversion_policy.h"

#include <vector>

#include "base/time/time.h"
#include "content/browser/conversions/conversion_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

constexpr base::TimeDelta kDefaultExpiry = base::TimeDelta::FromDays(30);

ConversionReport GetReport(base::Time impression_time,
                           base::Time conversion_time,
                           base::TimeDelta expiry = kDefaultExpiry) {
  return ConversionReport(
      ImpressionBuilder(impression_time).SetExpiry(expiry).Build(),
      /*conversion_data=*/"123", conversion_time,
      /*conversion_id=*/base::nullopt);
}

}  // namespace

class ConversionPolicyTest : public testing::Test {
 public:
  ConversionPolicyTest() = default;
};

TEST_F(ConversionPolicyTest, ImmediateConversion_FirstWindowUsed) {
  base::Time impression_time = base::Time::Now();
  auto report = GetReport(impression_time, /*conversion_time=*/impression_time);
  EXPECT_EQ(impression_time + base::TimeDelta::FromDays(2),
            ConversionPolicy().GetReportTimeForConversion(report));
}

TEST_F(ConversionPolicyTest, ConversionImmediatelyBeforeWindow_NextWindowUsed) {
  base::Time impression_time = base::Time::Now();
  base::Time conversion_time = impression_time + base::TimeDelta::FromDays(2) -
                               base::TimeDelta::FromMinutes(1);
  auto report = GetReport(impression_time, conversion_time);
  EXPECT_EQ(impression_time + base::TimeDelta::FromDays(7),
            ConversionPolicy().GetReportTimeForConversion(report));
}

TEST_F(ConversionPolicyTest, ConversionBeforeWindowDelay_WindowUsed) {
  base::Time impression_time = base::Time::Now();

  // The deadline for a window is 1 hour before the window. Use a time just
  // before the deadline.
  base::Time conversion_time = impression_time + base::TimeDelta::FromDays(2) -
                               base::TimeDelta::FromMinutes(61);
  auto report = GetReport(impression_time, conversion_time);
  EXPECT_EQ(impression_time + base::TimeDelta::FromDays(2),
            ConversionPolicy().GetReportTimeForConversion(report));
}

TEST_F(ConversionPolicyTest,
       ImpressionExpiryBeforeTwoDayWindow_TwoDayWindowUsed) {
  base::Time impression_time = base::Time::Now();
  base::Time conversion_time = impression_time + base::TimeDelta::FromHours(1);

  // Set the impression to expire before the two day window.
  auto report = GetReport(impression_time, conversion_time,
                          /*expiry=*/base::TimeDelta::FromHours(2));
  EXPECT_EQ(impression_time + base::TimeDelta::FromDays(2),
            ConversionPolicy().GetReportTimeForConversion(report));
}

TEST_F(ConversionPolicyTest,
       ImpressionExpiryBeforeSevenDayWindow_ExpiryWindowUsed) {
  base::Time impression_time = base::Time::Now();
  base::Time conversion_time = impression_time + base::TimeDelta::FromDays(3);

  // Set the impression to expire before the two day window.
  auto report = GetReport(impression_time, conversion_time,
                          /*expiry=*/base::TimeDelta::FromDays(4));

  // The expiry window is reported one hour after expiry time.
  EXPECT_EQ(impression_time + base::TimeDelta::FromDays(4) +
                base::TimeDelta::FromHours(1),
            ConversionPolicy().GetReportTimeForConversion(report));
}

TEST_F(ConversionPolicyTest,
       ImpressionExpiryAfterSevenDayWindow_ExpiryWindowUsed) {
  base::Time impression_time = base::Time::Now();
  base::Time conversion_time = impression_time + base::TimeDelta::FromDays(7);

  // Set the impression to expire before the two day window.
  auto report = GetReport(impression_time, conversion_time,
                          /*expiry=*/base::TimeDelta::FromDays(9));

  // The expiry window is reported one hour after expiry time.
  EXPECT_EQ(impression_time + base::TimeDelta::FromDays(9) +
                base::TimeDelta::FromHours(1),
            ConversionPolicy().GetReportTimeForConversion(report));
}

TEST_F(ConversionPolicyTest,
       SingleReportForConversion_AttributionCreditAssigned) {
  base::Time now = base::Time::Now();
  std::vector<ConversionReport> reports = {
      GetReport(/*impression_time=*/now, /*conversion_time=*/now)};
  ConversionPolicy().AssignAttributionCredits(&reports);
  EXPECT_EQ(1u, reports.size());
  EXPECT_EQ(100, reports[0].attribution_credit);
}

TEST_F(ConversionPolicyTest, TwoReportsForConversion_LastReceivesCredit) {
  base::Time now = base::Time::Now();
  std::vector<ConversionReport> reports = {
      GetReport(/*impression_time=*/now, /*conversion_time=*/now),
      GetReport(/*impression_time=*/now + base::TimeDelta::FromHours(100),
                /*conversion_time=*/now)};
  ConversionPolicy().AssignAttributionCredits(&reports);
  EXPECT_EQ(2u, reports.size());
  EXPECT_EQ(0, reports[0].attribution_credit);
  EXPECT_EQ(100, reports[1].attribution_credit);

  // Ensure the reports were not rearranged.
  EXPECT_EQ(now + base::TimeDelta::FromHours(100),
            reports[1].impression.impression_time());
}

}  // namespace content
