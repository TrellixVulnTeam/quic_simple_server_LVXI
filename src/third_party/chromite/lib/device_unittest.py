# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unittests for device.py"""

from __future__ import print_function

import subprocess

import mock

from chromite.lib import cros_build_lib
from chromite.lib import cros_test_lib
from chromite.lib import device
from chromite.lib import remote_access
from chromite.lib import vm


# pylint: disable=protected-access


class DeviceTester(cros_test_lib.RunCommandTestCase):
  """Test device.Device."""

  def setUp(self):
    """Common set up method for all tests."""
    opts = device.Device.GetParser().parse_args(['--device', '190.0.2.130'])
    self._device = device.Device(opts)

  def CreateDevice(self, device_name, is_vm):
    """Creates a device.

    Args:
      device_name: Name of the device.
      is_vm: If True, then created device should be a VM.
    """
    self._device.device = device_name
    created_device = device.Device.Create(
        vm.VM.GetParser().parse_args(['--device', device_name]))
    self.assertEqual(isinstance(created_device, vm.VM), is_vm)
    self.assertEqual(self._device.is_vm, is_vm)

  def testWaitForBoot(self):
    self._device.WaitForBoot()
    # Verify that ssh command is called with all of the right configurations.
    self.assertCommandContains(
        ['ssh', '-p', '22', 'root@190.0.2.130', '--', 'true'])

  @mock.patch('chromite.lib.cros_build_lib.run',
              side_effect=remote_access.SSHConnectionError())
  def testWaitForBootTimeOut(self, boot_mock):
    """Verify an exception is raised when the device takes to long to boot."""
    self.assertRaises(device.DeviceError, self._device.WaitForBoot, sleep=0)
    boot_mock.assert_called()

  @mock.patch('chromite.lib.device.Device.remote_run',
              return_value=cros_build_lib.CommandResult(returncode=1))
  def testWaitForBootReturnCode(self, boot_mock):
    """Verify an exception is raised when the returncode is not 0."""
    self.assertRaises(device.DeviceError, self._device.WaitForBoot)
    boot_mock.assert_called()

  def testRunCmd(self):
    """Verify that a command is run via run."""
    self._device.run(['/usr/local/autotest/bin/vm_sanity'])
    self.assertCommandContains(['sudo'], expected=False)
    self.assertCommandCalled(['/usr/local/autotest/bin/vm_sanity'])

  def testRunCmdDryRun(self):
    """Verify that a command is run while dry running for debugging."""
    self._device.dry_run = True
    # Look for dry run command in output.
    with cros_test_lib.LoggingCapturer() as logs:
      self._device.run(['echo', 'Hello'])
    self.assertTrue(logs.LogsContain('[DRY RUN] echo Hello'))

  def testRunCmdSudo(self):
    """Verify that a command is run via sudo."""
    self._device.use_sudo = True
    self._device.run(['mount', '-o', 'remount,rw', '/'])
    self.assertCommandCalled(['sudo', '--', 'mount', '-o', 'remount,rw', '/'])

  def testRemoteCmd(self):
    """Verify remote command runs correctly with default arguments."""
    self._device.remote_run(['/usr/local/autotest/bin/vm_sanity'])
    self.assertCommandContains(['/usr/local/autotest/bin/vm_sanity'])
    self.assertCommandContains(capture_output=True, stderr=subprocess.STDOUT,
                               log_output=True)

  def testRemoteCmdStream(self):
    """Verify remote command for streaming output."""
    self._device.remote_run('/usr/local/autotest/bin/vm_sanity',
                            stream_output=True)
    self.assertCommandContains(capture_output=False)
    self.assertCommandContains(stderr=subprocess.STDOUT,
                               log_output=True, expected=False)

  def testCreate(self):
    """Verify Device/VM creation."""
    # Verify a VM is created by default.
    self.CreateDevice(None, True)
    # Verify a VM is created when IP is localhost.
    self.CreateDevice('localhost', True)
    # Verify a Device is created when an IP is specified.
    self.CreateDevice('190.0.2.130', False)

  def testDryRunError(self):
    """Verify _dry_run can only be called when dry_run is True."""
    self._device.dry_run = False
    self.assertRaises(AssertionError, self._device._dry_run,
                      cmd=['echo', 'Hello'])
