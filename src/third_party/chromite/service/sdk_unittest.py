# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""SDK service tests."""

from __future__ import print_function

import os

from chromite.lib import chroot_lib
from chromite.lib import cros_build_lib
from chromite.lib import cros_test_lib
from chromite.service import sdk


class ChrootPathsTest(cros_test_lib.TestCase):
  """ChrootPaths tests."""

  def testGetArgsList(self):
    """Test the GetArgsList method."""
    # No values passed.
    empty = sdk.ChrootPaths()
    self.assertListEqual([], empty.GetArgList())

    # Cache dir provided.
    cache_dir = '/cache/dir'
    obj = sdk.ChrootPaths(cache_dir=cache_dir)
    self.assertListEqual(['--cache-dir', cache_dir], obj.GetArgList())

    # Chroot path provided.
    chroot_path = '/chroot/path'
    obj = sdk.ChrootPaths(chroot_path=chroot_path)
    self.assertListEqual(['--chroot', chroot_path], obj.GetArgList())


class CreateArgumentsTest(cros_test_lib.MockTestCase):
  """CreateArguments tests."""

  def _GetArgsList(self, **kwargs):
    """Helper to simplify getting the argument list."""
    instance = sdk.CreateArguments(**kwargs)
    return instance.GetArgList()

  def testGetArgList(self):
    """Test the GetArgsList method."""
    # Check the variations of replace.
    self.assertIn('--replace', self._GetArgsList(replace=True))
    self.assertIn('--create', self._GetArgsList(replace=False))

    # Check the other flags get added when the correct argument passed.
    self.assertListEqual(
        ['--create'],
        self._GetArgsList(replace=False, bootstrap=False, use_image=True))

    self.assertListEqual(
        ['--create', '--bootstrap', '--nouse-image'],
        self._GetArgsList(replace=False, bootstrap=True, use_image=False))

    # Make sure the path arguments get added.
    paths = sdk.ChrootPaths()
    path_args = ['--path-arg', '/path']
    self.PatchObject(paths, 'GetArgList', return_value=path_args)
    result = self._GetArgsList(paths=paths)
    for arg in path_args:
      self.assertIn(arg, result)


class UpdateArgumentsTest(cros_test_lib.TestCase):
  """UpdateArguments tests."""

  def _GetArgList(self, **kwargs):
    """Helper to simplify getting the argument list."""
    instance = sdk.UpdateArguments(**kwargs)
    return instance.GetArgList()

  def testBuildSource(self):
    """Test the build_source argument."""
    self.assertIn('--nousepkg', self._GetArgList(build_source=True))

  def testNoBuildSource(self):
    """Test using binpkgs."""
    self.assertNotIn('--nousepkg', self._GetArgList(build_source=False))

  def testToolchainTargets(self):
    """Test the toolchain boards argument."""
    expected = ['--toolchain_boards', 'board1,board2']
    result = self._GetArgList(toolchain_targets=['board1', 'board2'])
    for arg in expected:
      self.assertIn(arg, result)

  def testToolchainTargetsIgnoredForSource(self):
    """Test the toolchain boards argument."""
    expected = ['--nousepkg']
    result = self._GetArgList(toolchain_targets=['board1', 'board2'],
                              build_source=True)
    self.assertNotIn('--toolchain_boards', result)
    for arg in expected:
      self.assertIn(arg, result)

  def testNoToolchainTargets(self):
    """Test no toolchain boards argument."""
    self.assertIn('--skip_toolchain_update',
                  self._GetArgList(toolchain_targets=None))


class CreateTest(cros_test_lib.RunCommandTempDirTestCase):
  """Create function tests."""

  def testCreate(self):
    """Test the create function builds the command correctly."""
    arguments = sdk.CreateArguments(replace=True)
    arguments.paths.chroot_path = os.path.join(self.tempdir, 'chroot')
    expected_args = ['--arg', '--other', '--with-value', 'value']
    expected_version = 1

    self.PatchObject(arguments, 'GetArgList', return_value=expected_args)
    self.PatchObject(sdk, 'GetChrootVersion', return_value=expected_version)
    self.PatchObject(cros_build_lib, 'IsInsideChroot', return_value=False)

    version = sdk.Create(arguments)

    self.assertCommandContains(expected_args)
    self.assertEqual(expected_version, version)

  def testCreateInsideFails(self):
    """Test Create raises an error when called inside the chroot."""
    # Make sure it fails inside the chroot.
    self.PatchObject(cros_build_lib, 'IsInsideChroot', return_value=True)
    arguments = sdk.CreateArguments()
    with self.assertRaises(cros_build_lib.DieSystemExit):
      sdk.Create(arguments)


class DeleteTest(cros_test_lib.RunCommandTestCase):
  """Delete function tests."""

  def testDeleteNoChroot(self):
    """Test no chroot provided."""
    sdk.Delete()
    # cros clean sysroots command.
    self.assertCommandContains(['clean', '--sysroots'])
    # cros_sdk --delete.
    self.assertCommandContains(['--delete'])
    # Double whammy: no chroot specified for cros_sdk --delete, and no
    # cros clean --chroot.
    self.assertCommandContains(['--chroot'], expected=False)

  def testDeleteWithChroot(self):
    """Test with chroot provided."""
    path = '/some/path'
    sdk.Delete(chroot=chroot_lib.Chroot(path))
    self.assertCommandContains(['--delete', '--chroot', path])


class UpdateTest(cros_test_lib.RunCommandTestCase):
  """Update function tests."""

  def setUp(self):
    # Needs to be run inside the chroot right now.
    self.PatchObject(cros_build_lib, 'IsInsideChroot', return_value=True)

  def testUpdate(self):
    """Test the update method."""
    arguments = sdk.UpdateArguments()
    expected_args = ['--arg', '--other', '--with-value', 'value']
    expected_version = 1
    self.PatchObject(arguments, 'GetArgList', return_value=expected_args)
    self.PatchObject(sdk, 'GetChrootVersion', return_value=expected_version)

    version = sdk.Update(arguments)

    self.assertCommandContains(expected_args)
    self.assertEqual(expected_version, version)
