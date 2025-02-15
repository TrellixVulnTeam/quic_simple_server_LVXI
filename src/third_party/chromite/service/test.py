# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test service.

Handles test related functionality.
"""

from __future__ import print_function

import os
import re
import shutil

from chromite.cbuildbot import commands
from chromite.lib import constants
from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import failures_lib
from chromite.lib import image_lib
from chromite.lib import moblab_vm
from chromite.lib import osutils
from chromite.lib import portage_util


class Error(Exception):
  """The module's base error class."""


class BuildTargetUnitTestResult(object):
  """Result value object."""

  def __init__(self, return_code, failed_cpvs):
    """Init method.

    Args:
      return_code (int): The return code from the command execution.
      failed_cpvs (list[portage_util.CPV]|None): List of packages whose tests
        failed.
    """
    self.return_code = return_code
    self.failed_cpvs = failed_cpvs or []

  @property
  def success(self):
    return self.return_code == 0 and len(self.failed_cpvs) == 0


def BuildTargetUnitTest(build_target, chroot, blacklist=None, was_built=True):
  """Run the ebuild unit tests for the target.

  Args:
    build_target (build_target_util.BuildTarget): The build target.
    chroot (chroot_lib.Chroot): The chroot where the tests are running.
    blacklist (list[str]|None): Tests to skip.
    was_built (bool): Whether packages were built.

  Returns:
    BuildTargetUnitTestResult
  """
  # TODO(saklein) Refactor commands.RunUnitTests to use this/the API.
  # TODO(crbug.com/960805) Move cros_run_unit_tests logic here.
  cmd = ['cros_run_unit_tests', '--board', build_target.name]

  if blacklist:
    cmd.extend(['--blacklist_packages', ' '.join(blacklist)])

  if not was_built:
    cmd.append('--assume-empty-sysroot')

  # Set up the failed package status file.
  extra_env = chroot.env
  with chroot.tempdir() as tempdir:
    extra_env[constants.CROS_METRICS_DIR_ENVVAR] = tempdir

    result = cros_build_lib.run(cmd, enter_chroot=True,
                                extra_env=extra_env,
                                chroot_args=chroot.get_enter_args(),
                                check=False)

    failed_pkgs = portage_util.ParseDieHookStatusFile(tempdir)

  return BuildTargetUnitTestResult(result.returncode, failed_pkgs)


def BuildTargetUnitTestTarball(chroot, sysroot, result_path):
  """Build the unittest tarball.

  Args:
    chroot (chroot_lib.Chroot): Chroot where the tests were run.
    sysroot (sysroot_lib.Sysroot): The sysroot where the tests were run.
    result_path (str): The directory where the archive should be created.
  """
  tarball = 'unit_tests.tar'
  tarball_path = os.path.join(result_path, tarball)

  cwd = os.path.join(chroot.path, sysroot.path.lstrip(os.sep),
                     constants.UNITTEST_PKG_PATH)

  result = cros_build_lib.CreateTarball(tarball_path, cwd, chroot=chroot.path,
                                        compression=cros_build_lib.COMP_NONE,
                                        check=False)

  return tarball_path if result.returncode == 0 else None


def DebugInfoTest(sysroot_path):
  """Run the debug info tests.

  Args:
    sysroot_path (str): The sysroot being tested.

  Returns:
    bool: True iff all tests passed, False otherwise.
  """
  cmd = ['debug_info_test', os.path.join(sysroot_path, 'usr/lib/debug')]
  result = cros_build_lib.run(cmd, enter_chroot=True, check=False)

  return result.returncode == 0


def CreateMoblabVm(workspace_dir, chroot_dir, image_dir):
  """Create the moblab VMs.

  Assumes that image_dir is in exactly the state it was after building
  a test image and then converting it to a VM image.

  Args:
    workspace_dir (str): Workspace for the moblab VM.
    chroot_dir (str): Directory containing the chroot for the moblab VM.
    image_dir (str): Directory containing the VM image.

  Returns:
    MoblabVm: The resulting VM.
  """
  vms = moblab_vm.MoblabVm(workspace_dir, chroot_dir=chroot_dir)
  vms.Create(image_dir, dut_image_dir=image_dir, create_vm_images=False)
  return vms


def PrepareMoblabVmImageCache(vms, builder, payload_dirs):
  """Preload the given payloads into the moblab VM image cache.

  Args:
    vms (MoblabVm): The Moblab VM.
    builder (str): The builder path, used to name the cache dir.
    payload_dirs (list[str]): List of payload directories to load.

  Returns:
    str: Absolute path to the image cache path.
  """
  with vms.MountedMoblabDiskContext() as disk_dir:
    image_cache_root = os.path.join(disk_dir, 'static/prefetched')
    # If by any chance this path exists, the permission bits are surely
    # nonsense, since 'moblab' user doesn't exist on the host system.
    osutils.RmDir(image_cache_root, ignore_missing=True, sudo=True)

    image_cache_dir = os.path.join(image_cache_root, builder)
    osutils.SafeMakedirsNonRoot(image_cache_dir)
    for payload_dir in payload_dirs:
      osutils.CopyDirContents(payload_dir, image_cache_dir, allow_nonempty=True)

  image_cache_rel_dir = image_cache_dir[len(disk_dir):].strip('/')
  return os.path.join('/', 'mnt/moblab', image_cache_rel_dir)


def RunMoblabVmTest(chroot, vms, builder, image_cache_dir, results_dir):
  """Run Moblab VM tests.

  Args:
    chroot (chroot_lib.Chroot): The chroot in which to run tests.
    builder (str): The builder path, used to find artifacts on GS.
    vms (MoblabVm): The Moblab VMs to test.
    image_cache_dir (str): Path to artifacts cache.
    results_dir (str): Path to output test results.
  """
  with vms.RunVmsContext():
    # TODO(evanhernandez): Move many of these arguments to test config.
    test_args = [
        # moblab in VM takes longer to bring up all upstart services on first
        # boot than on physical machines.
        'services_init_timeout_m=10',
        'target_build="%s"' % builder,
        'test_timeout_hint_m=90',
        'clear_devserver_cache=False',
        'image_storage_server="%s"' % (image_cache_dir.rstrip('/') + '/'),
    ]
    cros_build_lib.run(
        [
            'test_that',
            '--no-quickmerge',
            '--results_dir', results_dir,
            '-b', 'moblab-generic-vm',
            'localhost:%s' % vms.moblab_ssh_port,
            'moblab_DummyServerNoSspSuite',
            '--args', ' '.join(test_args),
        ],
        enter_chroot=True,
        chroot_args=chroot.get_enter_args(),
    )


def SimpleChromeWorkflowTest(sysroot_path, build_target_name, chrome_root,
                             goma):
  """Execute SimpleChrome workflow tests

  Args:
    sysroot_path (str): The sysroot path for testing Chrome.
    build_target_name (str): Board build target
    chrome_root (str): Path to Chrome source root.
    goma (goma_util.Goma): Goma object (or None).
  """
  board_dir = 'out_%s' % build_target_name

  out_board_dir = os.path.join(chrome_root, board_dir, 'Release')
  use_goma = goma != None
  extra_args = []

  with osutils.TempDir(prefix='chrome-sdk-cache') as tempdir:
    sdk_cmd = _InitSimpleChromeSDK(tempdir, build_target_name, sysroot_path,
                                   chrome_root, use_goma)

    if goma:
      extra_args.extend(['--nostart-goma', '--gomadir', goma.linux_goma_dir])

    _BuildChrome(sdk_cmd, chrome_root, out_board_dir, goma)
    _TestDeployChrome(sdk_cmd, out_board_dir)
    _VMTestChrome(build_target_name, sdk_cmd)


def _InitSimpleChromeSDK(tempdir, build_target_name, sysroot_path, chrome_root,
                         use_goma):
  """Create ChromeSDK object for executing 'cros chrome-sdk' commands.

  Args:
    tempdir (string): Tempdir for command execution.
    build_target_name (string): Board build target.
    sysroot_path (string): Sysroot for Chrome to use.
    chrome_root (string): Path to Chrome.
    use_goma (bool): Whether to use goma.

  Returns:
    A ChromeSDK object.
  """
  extra_args = ['--cwd', chrome_root, '--sdk-path', sysroot_path]
  cache_dir = os.path.join(tempdir, 'cache')

  sdk_cmd = commands.ChromeSDK(
      constants.SOURCE_ROOT, build_target_name, chrome_src=chrome_root,
      goma=use_goma, extra_args=extra_args, cache_dir=cache_dir)
  return sdk_cmd


def _VerifySDKEnvironment(out_board_dir):
  """Make sure the SDK environment is set up properly.

  Args:
    out_board_dir (str): Output SDK dir for board.
  """
  if not os.path.exists(out_board_dir):
    raise AssertionError('%s not created!' % out_board_dir)
  logging.info('ARGS.GN=\n%s',
               osutils.ReadFile(os.path.join(out_board_dir, 'args.gn')))


def _BuildChrome(sdk_cmd, chrome_root, out_board_dir, goma):
  """Build Chrome with SimpleChrome environment.

  Args:
    sdk_cmd (ChromeSDK object): sdk_cmd to run cros chrome-sdk commands.
    chrome_root (string): Path to Chrome.
    out_board_dir (string): Path to board directory.
    goma (goma_util.Goma): Goma object
  """
  # Validate fetching of the SDK and setting everything up.
  sdk_cmd.Run(['true'])

  sdk_cmd.Run(['gclient', 'runhooks'])

  # Generate args.gn and ninja files.
  gn_cmd = os.path.join(chrome_root, 'buildtools', 'linux64', 'gn')
  gn_gen_cmd = '%s gen "%s" --args="$GN_ARGS"' % (gn_cmd, out_board_dir)
  sdk_cmd.Run(['bash', '-c', gn_gen_cmd])

  _VerifySDKEnvironment(out_board_dir)

  if goma:
    # If goma is enabled, start goma compiler_proxy here, and record
    # several information just before building Chrome is started.
    goma.Start()
    extra_env = goma.GetExtraEnv()
    ninja_env_path = os.path.join(goma.goma_log_dir, 'ninja_env')
    sdk_cmd.Run(['env', '--null'],
                run_args={'extra_env': extra_env,
                          'stdout': ninja_env_path})
    osutils.WriteFile(os.path.join(goma.goma_log_dir, 'ninja_cwd'),
                      sdk_cmd.cwd)
    osutils.WriteFile(os.path.join(goma.goma_log_dir, 'ninja_command'),
                      cros_build_lib.CmdToStr(sdk_cmd.GetNinjaCommand()))
  else:
    extra_env = None

  result = None
  try:
    # Build chromium.
    result = sdk_cmd.Ninja(run_args={'extra_env': extra_env})
  finally:
    # In teardown, if goma is enabled, stop the goma compiler proxy,
    # and record/copy some information to log directory, which will be
    # uploaded to the goma's server in a later stage.
    if goma:
      goma.Stop()
      ninja_log_path = os.path.join(chrome_root,
                                    sdk_cmd.GetNinjaLogPath())
      if os.path.exists(ninja_log_path):
        shutil.copy2(ninja_log_path,
                     os.path.join(goma.goma_log_dir, 'ninja_log'))
      if result:
        osutils.WriteFile(os.path.join(goma.goma_log_dir, 'ninja_exit'),
                          str(result.returncode))


def _TestDeployChrome(sdk_cmd, out_board_dir):
  """Test SDK deployment.

  Args:
    sdk_cmd (ChromeSDK object): sdk_cmd to run cros chrome-sdk commands.
    out_board_dir (string): Path to board directory.
  """
  with osutils.TempDir(prefix='chrome-sdk-stage') as tempdir:
    # Use the TOT deploy_chrome.
    script_path = os.path.join(
        constants.SOURCE_ROOT, constants.CHROMITE_BIN_SUBDIR, 'deploy_chrome')
    sdk_cmd.Run([script_path, '--build-dir', out_board_dir,
                 '--staging-only', '--staging-dir', tempdir])
    # Verify chrome is deployed.
    chromepath = os.path.join(tempdir, 'chrome')
    if not os.path.exists(chromepath):
      raise AssertionError(
          'deploy_chrome did not run successfully! Searched %s' % (chromepath))


def _VMTestChrome(board, sdk_cmd):
  """Run cros_run_test."""
  image_dir_symlink = image_lib.GetLatestImageLink(board)
  image_path = os.path.join(image_dir_symlink,
                            constants.VM_IMAGE_BIN)

  # Run VM test for boards where we've built a VM.
  if image_path and os.path.exists(image_path):
    sdk_cmd.VMTest(image_path)


def ValidateMoblabVmTest(results_dir):
  """Determine if the VM test passed or not.

  Args:
    results_dir (str): Path to directory containing test_that results.

  Raises:
    failures_lib.TestFailure: If dummy_PassServer did not run or failed.
  """
  log_file = os.path.join(results_dir, 'debug', 'test_that.INFO')
  if not os.path.isfile(log_file):
    raise failures_lib.TestFailure('Found no test_that logs at %s' % log_file)

  log_file_contents = osutils.ReadFile(log_file)
  if not re.match(r'dummy_PassServer\s*\[\s*PASSED\s*]', log_file_contents):
    raise failures_lib.TestFailure('Moblab run_suite succeeded, but did '
                                   'not successfully run dummy_PassServer.')
