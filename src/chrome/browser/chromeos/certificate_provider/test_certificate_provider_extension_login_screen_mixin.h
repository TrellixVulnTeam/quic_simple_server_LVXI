// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CERTIFICATE_PROVIDER_TEST_CERTIFICATE_PROVIDER_EXTENSION_LOGIN_SCREEN_MIXIN_H_
#define CHROME_BROWSER_CHROMEOS_CERTIFICATE_PROVIDER_TEST_CERTIFICATE_PROVIDER_EXTENSION_LOGIN_SCREEN_MIXIN_H_

#include <memory>

#include "chrome/test/base/mixin_based_in_process_browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace chromeos {
class DeviceStateMixin;
}

class TestCertificateProviderExtension;

// Mixin for installing and running TestCertificateProviderExtension on the
// Login Screen via the device policy.
//
// Note: Please make sure that the |kDisableBackgroundNetworking| switch is NOT
// set when using this mixin.
class TestCertificateProviderExtensionLoginScreenMixin final
    : public InProcessBrowserTestMixin {
 public:
  TestCertificateProviderExtensionLoginScreenMixin(
      InProcessBrowserTestMixinHost* host,
      chromeos::DeviceStateMixin* device_state_mixin);
  TestCertificateProviderExtensionLoginScreenMixin(
      const TestCertificateProviderExtensionLoginScreenMixin&) = delete;
  TestCertificateProviderExtensionLoginScreenMixin& operator=(
      const TestCertificateProviderExtensionLoginScreenMixin&) = delete;
  ~TestCertificateProviderExtensionLoginScreenMixin() override;

  TestCertificateProviderExtension* test_certificate_provider_extension() {
    return test_certificate_provider_extension_.get();
  }
  const TestCertificateProviderExtension* test_certificate_provider_extension()
      const {
    return test_certificate_provider_extension_.get();
  }

  // InProcessBrowserTestMixin:
  void SetUpOnMainThread() override;
  void TearDownOnMainThread() override;

 private:
  chromeos::DeviceStateMixin* const device_state_mixin_;
  net::EmbeddedTestServer embedded_test_server_;
  std::unique_ptr<TestCertificateProviderExtension>
      test_certificate_provider_extension_;
};

#endif  // CHROME_BROWSER_CHROMEOS_CERTIFICATE_PROVIDER_TEST_CERTIFICATE_PROVIDER_EXTENSION_LOGIN_SCREEN_MIXIN_H_
