# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Toolchain and related functionality."""

from __future__ import print_function

import base64
import collections
import datetime
import glob
import json
import os
import re
import shutil

from chromite.cbuildbot import afdo
from chromite.lib import alerts
from chromite.lib import constants
from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import git
from chromite.lib import gob_util
from chromite.lib import gs
from chromite.lib import osutils
from chromite.lib import path_util
from chromite.lib import portage_util
from chromite.lib import timeout_util

class PrepareForBuildReturn(object):
  """Return values for PrepareForBuild call."""
  UNSPECIFIED = 0
  # Build is necessary to generate artifacts.
  NEEDED = 1
  # Defer to other artifacts.  Used primarily for aggregation of artifact
  # results.
  UNKNOWN = 2
  # Artifacts are already generated.  The build is pointless.
  POINTLESS = 3


# URLs
# FIXME(tcwang): Remove access to GS buckets from this lib.
# There are plans in the future to remove all network
# operations from chromite, including access to GS buckets.
# Need to use build API and recipes to communicate to GS buckets in
# the future.
ORDERFILE_GS_URL_UNVETTED = \
    'gs://chromeos-prebuilt/afdo-job/orderfiles/unvetted'
ORDERFILE_GS_URL_VETTED = \
    'gs://chromeos-prebuilt/afdo-job/orderfiles/vetted'
BENCHMARK_AFDO_GS_URL = \
    'gs://chromeos-prebuilt/afdo-job/llvm/'
CWP_AFDO_GS_URL = \
    'gs://chromeos-prebuilt/afdo-job/cwp/chrome/'
KERNEL_PROFILE_URL = \
    'gs://chromeos-prebuilt/afdo-job/cwp/kernel/'
AFDO_GS_URL_VETTED = \
    'gs://chromeos-prebuilt/afdo-job/vetted/'
KERNEL_AFDO_GS_URL_VETTED = \
    os.path.join(AFDO_GS_URL_VETTED, 'kernel')
BENCHMARK_AFDO_GS_URL_VETTED = \
    os.path.join(AFDO_GS_URL_VETTED, 'benchmarks')
CWP_AFDO_GS_URL_VETTED = \
    os.path.join(AFDO_GS_URL_VETTED, 'cwp')
RELEASE_AFDO_GS_URL_VETTED = \
    os.path.join(AFDO_GS_URL_VETTED, 'release')

# Constants
AFDO_SUFFIX = '.afdo'
BZ2_COMPRESSION_SUFFIX = '.bz2'
XZ_COMPRESSION_SUFFIX = '.xz'
KERNEL_AFDO_COMPRESSION_SUFFIX = '.gcov.xz'
TOOLCHAIN_UTILS_PATH = '/mnt/host/source/src/third_party/toolchain-utils/'
TOOLCHAIN_UTILS_REPO = \
    'https://chromium.googlesource.com/chromiumos/third_party/toolchain-utils'
AFDO_PROFILE_PATH_IN_CHROMIUM = 'src/chromeos/profiles/%s.afdo.newest.txt'
MERGED_AFDO_NAME = 'chromeos-chrome-amd64-%s'

# How old can the Kernel AFDO data be? (in days).
KERNEL_ALLOWED_STALE_DAYS = 42
# How old can the Kernel AFDO data be before sheriff got noticed? (in days).
KERNEL_WARN_STALE_DAYS = 14

# For merging release Chrome profiles.
RELEASE_CWP_MERGE_WEIGHT = 75
RELEASE_BENCHMARK_MERGE_WEIGHT = 100 - RELEASE_CWP_MERGE_WEIGHT

# Set of boards that can generate the AFDO profile (can generate 'perf'
# data with LBR events). Currently, it needs to be a device that has
# at least 4GB of memory.
#
# This must be consistent with the definitions in autotest.
AFDO_DATA_GENERATORS_LLVM = ('chell', 'samus')
CHROME_AFDO_VERIFIER_BOARDS = {'samus': 'silvermont',
                               'snappy': 'airmont',
                               'eve': 'broadwell'}
KERNEL_AFDO_VERIFIER_BOARDS = {'lulu': '3.14', 'chell': '3.18', 'eve': '4.4'}

AFDO_ALERT_RECIPIENTS = [
    'chromeos-toolchain-oncall1@google.com'
]

# Full path to the chromiumos-overlay directory.
_CHROMIUMOS_OVERLAY = os.path.join(
    constants.CHROMITE_DIR, '..', constants.CHROMIUMOS_OVERLAY_DIR)

# RegExps
# NOTE: These regexp are copied from cbuildbot/afdo.py. Keep two copies
# until deployed and then remove the one in cbuildbot/afdo.py.
CHROOT_ROOT = os.path.join('%(build_root)s', constants.DEFAULT_CHROOT_DIR)
CHROOT_TMP_DIR = os.path.join('%(root)s', 'tmp')
AFDO_VARIABLE_REGEX = r'AFDO_FILE\["%s"\]'
AFDO_ARTIFACT_EBUILD_REGEX = r'^(?P<bef>%s=)(?P<name>("[^"]*"|.*))(?P<aft>.*)'
AFDO_ARTIFACT_EBUILD_REPL = r'\g<bef>"%s"\g<aft>'

BENCHMARK_PROFILE_NAME_REGEX = r"""
       ^chromeos-chrome-amd64-
       (\d+)\.                    # Major
       (\d+)\.                    # Minor
       (\d+)\.                    # Build
       (\d+)                      # Patch
       (?:_rc)?-r(\d+)            # Revision
       (-merged)?\.
       afdo(?:\.bz2)?$            # We don't care about the presence of .bz2,
                                  # so we use the ignore-group '?:' operator.
"""

BenchmarkProfileVersion = collections.namedtuple(
    'BenchmarkProfileVersion',
    ['major', 'minor', 'build', 'patch', 'revision', 'is_merged'])

CWP_PROFILE_NAME_REGEX = r"""
      ^R(\d+)-                      # Major
       (\d+)\.                      # Build
       (\d+)-                       # Patch
       (\d+)                        # Clock; breaks ties sometimes.
       (?:\.afdo|\.gcov)?           # Optional: CWP for Chrome has `afdo`,
                                    # and kernel has `gcov`. Also this regex
                                    # is also used to match names in ebuild and
                                    # sometimes names in ebuild don't have
                                    # suffix.
       (?:\.xz)?$                   # We don't care about the presence of xz
    """

CWPProfileVersion = collections.namedtuple('CWPProfileVersion',
                                           ['major', 'build', 'patch', 'clock'])

MERGED_PROFILE_NAME_REGEX = r"""
      ^chromeos-chrome
      -(?:orderfile|amd64)                       # prefix for either orderfile
                                                 # or release profile.
      # CWP parts
      -(?:field|silvermont|airmont|broadwell)    # Valid names
      -(\d+)                                     # Major
      -(\d+)                                     # Build
      \.(\d+)                                    # Patch
      -(\d+)                                     # Clock; breaks ties sometimes.
      # Benchmark parts
      -benchmark
      -(\d+)                                     # Major
      \.(\d+)                                    # Minor
      \.(\d+)                                    # Build
      \.(\d+)                                    # Patch
      -r(\d+)                                    # Revision
      (?:\.orderfile|-redacted\.afdo)            # suffix for either orderfile
                                                 # or release profile.
      (?:\.xz)?$
"""

CHROME_ARCH_VERSION = '%(package)s-%(arch)s-%(version)s'
CHROME_PERF_AFDO_FILE = '%s.perf.data' % CHROME_ARCH_VERSION
CHROME_BENCHMARK_AFDO_FILE = '%s%s' % (CHROME_ARCH_VERSION, AFDO_SUFFIX)


class Error(Exception):
  """Base module error class."""


class PrepareForBuildHandlerError(Error):
  """Error for PrepareForBuildHandler class."""


class GenerateChromeOrderfileError(Error):
  """Error for GenerateChromeOrderfile class."""


class ProfilesNameHelperError(Error):
  """Error for helper functions related to profile naming."""


class GenerateBenchmarkAFDOProfilesError(Error):
  """Error for GenerateBenchmarkAFDOProfiles class."""


class UpdateEbuildWithAFDOArtifactsError(Error):
  """Error for UpdateEbuildWithAFDOArtifacts class."""


class PublishVettedAFDOArtifactsError(Error):
  """Error for publishing vetted afdo artifacts."""


def _ParseBenchmarkProfileName(profile_name):
  """Parse the name of a benchmark profile for Chrome.

  Examples:
    with input: profile_name='chromeos-chrome-amd64-77.0.3849.0_rc-r1.afdo'
    the function returns:
    BenchmarkProfileVersion(
      major=77, minor=0, build=3849, patch=0, revision=1, is_merged=False)

  Args:
    profile_name: The name of a benchmark profile.

  Returns:
    Named tuple of BenchmarkProfileVersion. With the input above, returns
  """
  pattern = re.compile(BENCHMARK_PROFILE_NAME_REGEX, re.VERBOSE)
  match = pattern.match(profile_name)
  if not match:
    raise ProfilesNameHelperError(
        'Unparseable benchmark profile name: %s' % profile_name)

  groups = match.groups()
  version_groups = groups[:-1]
  is_merged = groups[-1]
  return BenchmarkProfileVersion(
      *[int(x) for x in version_groups], is_merged=bool(is_merged))


def _ParseCWPProfileName(profile_name):
  """Parse the name of a CWP profile for Chrome.

  Examples:
    With input profile_name='R77-3809.38-1562580965.afdo',
    the function returns:
    CWPProfileVersion(major=77, build=3809, patch=38, clock=1562580965)

  Args:
    profile_name: The name of a CWP profile.

  Returns:
    Named tuple of CWPProfileVersion.
  """
  pattern = re.compile(CWP_PROFILE_NAME_REGEX, re.VERBOSE)
  match = pattern.match(profile_name)
  if not match:
    raise ProfilesNameHelperError(
        'Unparseable CWP profile name: %s' % profile_name)
  return CWPProfileVersion(*[int(x) for x in match.groups()])


def _ParseMergedProfileName(artifact_name):
  """Parse the name of an orderfile or a release profile for Chrome.

  Examples:
    With input: profile_name='chromeos-chrome-orderfile\
    -field-77-3809.38-1562580965
    -benchmark-77.0.3849.0_rc-r1.orderfile.xz'
    the function returns:
    (BenchmarkProfileVersion(
     major=77, minor=0, build=3849, patch=0, revision=1, is_merged=False),
     CWPProfileVersion(major=77, build=3809, patch=38, clock=1562580965))

  Args:
    artifact_name: The name of an orderfile, or a release AFDO profile.

  Returns:
    A tuple of (BenchmarkProfileVersion, CWPProfileVersion)
  """
  pattern = re.compile(MERGED_PROFILE_NAME_REGEX, re.VERBOSE)
  match = pattern.match(artifact_name)
  if not match:
    raise ProfilesNameHelperError(
        'Unparseable merged AFDO name: %s' % artifact_name)
  groups = match.groups()
  cwp_groups = groups[:4]
  benchmark_groups = groups[4:]
  return (BenchmarkProfileVersion(
      *[int(x) for x in benchmark_groups], is_merged=False),
          CWPProfileVersion(*[int(x) for x in cwp_groups]))


def _FindEbuildPath(package, buildroot=None, board=None):
  """Find the path to ebuild (in chroot or outside).

  Args:
    package: Name of the package. chromeos-chrome or chromeos-kernel-X_XX.
    buildroot: Optional. The path to build root, when used outside chroot.
    When omitted, the return path is relative inside chroot.
    board: Optional. A string of the name of the board.

  Returns:
    The full path to the versioned ebuild file. The path is either
    relative inside chroot, or outside chroot, depending on the buildroot arg.
  """
  valid_package_names = [
      'chromeos-kernel-' + x.replace('.', '_')
      for x in KERNEL_AFDO_VERIFIER_BOARDS.values()
  ] + ['chromeos-chrome']
  if package not in valid_package_names:
    raise ValueError('Invalid package name %s to look up ebuild.' % package)

  if board:
    equery_prog = 'equery-%s' % board
  else:
    equery_prog = 'equery'
  equery_cmd = [equery_prog, 'w', package]
  ebuild_file = cros_build_lib.run(
      equery_cmd, enter_chroot=True, stdout=True).output.rstrip()
  if not buildroot:
    return ebuild_file

  basepath = '/mnt/host/source'
  if not ebuild_file.startswith(basepath):
    raise ValueError('Unexpected ebuild path: %s' % ebuild_file)
  ebuild_path = os.path.relpath(ebuild_file, basepath)
  return os.path.join(buildroot, ebuild_path)


def _GetArtifactVersionInChromium(arch, chrome_root):
  """Find the version (name) of AFDO artifact from chromium source.

  Args:
    arch: There are three AFDO profiles in chromium:
    silvermont, broadwell, and airmont.
    chrome_root: The path to Chrome root.

  Returns:
    The name of the AFDO artifact found in the chroot.
    None if not found.

  Raises:
    ValueError: when "arch" is not a supported.
    RuntimeError: when the file containing AFDO profile name can't be found.
  """
  if arch not in list(CHROME_AFDO_VERIFIER_BOARDS.values()):
    raise ValueError('Invalid architecture %s to use in AFDO profile' % arch)

  if not os.path.exists(chrome_root):
    raise RuntimeError('chrome_root %s does not exist.' % chrome_root)

  profile_file = os.path.join(chrome_root,
                              AFDO_PROFILE_PATH_IN_CHROMIUM % arch)
  if not os.path.exists(profile_file):
    logging.info('Files in chrome_root profile: %r', os.listdir(
        os.path.join(chrome_root,
                     AFDO_PROFILE_PATH_IN_CHROMIUM % arch,
                     '..')))
    raise RuntimeError(
        'File %s containing profile name does not exist' % (profile_file,))

  return osutils.ReadFile(profile_file)


def _GetArtifactVersionInEbuild(package, variable, buildroot=None):
  """Find the version (name) of AFDO artifact from files in ebuild.

  Args:
    package: The name of the package to search in ebuild.
    variable: Use this variable directly as regex to search.
    buildroot: Optional. When omitted, search ebuild file inside
    chroot. Otherwise, use the path to search ebuild file outside.

  Returns:
    The name of the AFDO artifact found in the ebuild.
    None if not found.
  """
  ebuild_file = _FindEbuildPath(package, buildroot=buildroot)
  pattern = re.compile(AFDO_ARTIFACT_EBUILD_REGEX % variable)
  with open(ebuild_file) as f:
    for line in f:
      matched = pattern.match(line)
      if matched:
        ret = matched.group('name')
        if ret.startswith('"') and ret.endswith('"'):
          return ret[1:-1]
        return ret

  logging.info('%s is not found in the ebuild: %s', variable, ebuild_file)
  return None


def _GetCombinedAFDOName(cwp_versions, cwp_arch, benchmark_versions):
  """Construct a name mixing CWP and benchmark AFDO names.

  Examples:
    If benchmark AFDO is BenchmarkProfileVersion(
      major=77, minor=0, build=3849, patch=0, revision=1, is_merged=False)
    and CWP AFDO is CWPProfileVersion(
      major=77, build=3809, patch=38, clock=1562580965),
    and cwp_arch is 'silvermont',
    the returned name is:
    silvermont-77-3809.38-1562580965-benchmark-77.0.3849.0-r1

  Args:
    cwp_versions: CWP profile as a namedtuple CWPProfileVersion.
    cwp_arch: Architecture used to differentiate CWP profiles.
    benchmark_versions: Benchmark profile as a namedtuple
    BenchmarkProfileVersion.

  Returns:
    A name using the combination of CWP + benchmark AFDO names.
  """
  cwp_piece = '%s-%d-%d.%d-%d' % (
      cwp_arch, cwp_versions.major, cwp_versions.build,
      cwp_versions.patch, cwp_versions.clock)
  benchmark_piece = 'benchmark-%d.%d.%d.%d-r%d' % (
      benchmark_versions.major, benchmark_versions.minor,
      benchmark_versions.build, benchmark_versions.patch,
      benchmark_versions.revision)
  return '%s-%s' % (cwp_piece, benchmark_piece)


def _GetOrderfileName(chrome_root):
  """Construct an orderfile name for the current Chrome OS checkout.

  Args:
    chrome_root: The path to chrome_root.

  Returns:
    An orderfile name using CWP + benchmark AFDO name.
  """
  benchmark_afdo_version, cwp_afdo_version = _ParseMergedProfileName(
      _GetArtifactVersionInChromium(arch='silvermont',
                                    chrome_root=chrome_root))
  return 'chromeos-chrome-orderfile-%s' % (
      _GetCombinedAFDOName(cwp_afdo_version, 'field', benchmark_afdo_version))


def _GetBenchmarkAFDOName(buildroot, board):
  """Constructs a benchmark AFDO name for the current build root.

  Args:
    buildroot: The path to build root.
    board: The name of the board.

  Returns:
    A string similar to: chromeos-chrome-amd64-77.0.3849.0_rc-r1.afdo
  """
  cpv = portage_util.PortageqBestVisible(constants.CHROME_CP, cwd=buildroot)
  arch = portage_util.PortageqEnvvar('ARCH', board=board, allow_undefined=True)
  afdo_spec = {'package': cpv.package, 'arch': arch, 'version': cpv.version}
  afdo_file = CHROME_BENCHMARK_AFDO_FILE % afdo_spec
  return afdo_file


def _CompressAFDOFiles(targets, input_dir, output_dir, suffix):
  """Compress files using AFDO compression type.

  Args:
    targets: List of files to compress. Only the basename is needed.
    input_dir: Paths to the targets (outside chroot). If None, use
    the targets as full path.
    output_dir: Paths to save the compressed file (outside chroot).
    suffix: Compression suffix.

  Returns:
    List of full paths of the generated tarballs.

  Raises:
    RuntimeError if the file to compress does not exist.
  """
  ret = []
  for t in targets:
    name = os.path.basename(t)
    compressed = name + suffix
    if input_dir:
      input_path = os.path.join(input_dir, name)
    else:
      input_path = t
    if not os.path.exists(input_path):
      raise RuntimeError('file %s to compress does not exist' % input_path)
    output_path = os.path.join(output_dir, compressed)
    cros_build_lib.CompressFile(input_path, output_path)
    ret.append(output_path)
  return ret


def _UploadAFDOArtifactToGSBucket(gs_url, local_path, rename=''):
  """Upload AFDO artifact to a centralized GS Bucket.

  Args:
    gs_url: The location to upload the artifact.
    local_path: Full local path to the file.
    rename: Optional. The name used in the bucket. If omitted, will use
    the same name as in local path.

  Raises:
    RuntimeError if the file to upload doesn't exist.
  """
  gs_context = gs.GSContext()

  if not os.path.exists(local_path):
    raise RuntimeError('file %s to upload does not exist' % local_path)

  if rename:
    remote_name = rename
  else:
    remote_name = os.path.basename(local_path)
  url = os.path.join(gs_url, remote_name)

  if gs_context.Exists(url):
    logging.info('URL %s already exists, skipping uploading...', url)
    return

  gs_context.Copy(local_path, url, acl='public-read')


def _MergeAFDOProfiles(chroot_profile_list,
                       chroot_output_profile,
                       use_compbinary=False):
  """Merges the given profile list.

  This function is copied over from afdo.py.

  Args:
    chroot_profile_list: a list of (profile_path, profile_weight).
      Profile_weight is an int that tells us how to weight the profile compared
      to everything else.
    chroot_output_profile: where to store the result profile.
    use_compbinary: whether to use the new compressed binary AFDO profile
      format.
  """
  if not chroot_profile_list:
    raise ValueError('Need profiles to merge')

  # A regular llvm-profdata command looks like:
  # llvm-profdata merge [-sample] -output=/path/to/output input1 [input2
  #                                                               [input3 ...]]
  #
  # Alternatively, we can specify inputs by `-weighted-input=A,file`, where A
  # is a multiplier of the sample counts in the profile.
  merge_command = [
      'llvm-profdata',
      'merge',
      '-sample',
      '-output=' + chroot_output_profile,
  ]

  merge_command += [
      '-weighted-input=%d,%s' % (weight, name)
      for name, weight in chroot_profile_list
  ]

  if use_compbinary:
    merge_command.append('-compbinary')

  cros_build_lib.run(merge_command, enter_chroot=True, print_cmd=True)


def _RedactAFDOProfile(input_path, output_path):
  """Redact ICF'ed symbols from AFDO profiles.

  ICF can cause inflation on AFDO sampling results, so we want to remove
  them from AFDO profiles used for Chrome.
  See http://crbug.com/916024 for more details.

  Args:
    input_path: Full path to input AFDO profile.
    output_path: Full path to output AFDO profile.
  """

  profdata_command_base = ['llvm-profdata', 'merge', '-sample']
  # Convert the compbinary profiles to text profiles.
  input_to_text_temp = input_path + '.text.temp'
  convert_to_text_command = profdata_command_base + [
      '-text', input_path, '-output', input_to_text_temp
  ]
  cros_build_lib.run(convert_to_text_command, enter_chroot=True, print_cmd=True)

  # Call the redaction script.
  redacted_temp = input_path + '.redacted.temp'
  with open(input_to_text_temp) as f:
    cros_build_lib.run(['redact_textual_afdo_profile'],
                       input=f,
                       stdout=redacted_temp,
                       enter_chroot=True,
                       print_cmd=True)

  # Convert the profiles back to compbinary profiles.
  # Using `compbinary` profiles saves us hundreds of MB of RAM per
  # compilation, since it allows profiles to be lazily loaded.
  convert_to_combinary_command = profdata_command_base + [
      '-compbinary',
      redacted_temp,
      '-output',
      output_path,
  ]
  cros_build_lib.run(
      convert_to_combinary_command, enter_chroot=True, print_cmd=True)


class GenerateChromeOrderfile(object):
  """Class to handle generation of orderfile for Chrome.

  This class takes orderfile containing symbols ordered by Call-Chain
  Clustering (C3), produced when linking Chrome, and uses a toolchain
  script to perform post processing to generate an orderfile that can
  be used for linking Chrome and creates tarball. The output of this
  script is a tarball of the orderfile and a tarball of the NM output
  of the built Chrome binary.

  The whole class runs outside chroot, so use paths relative outside
  chroot, except the functions noted otherwise.
  """

  PROCESS_SCRIPT = os.path.join(TOOLCHAIN_UTILS_PATH,
                                'orderfile/post_process_orderfile.py')
  CHROME_BINARY_PATH = ('/var/cache/chromeos-chrome/chrome-src-internal/'
                        'src/out_${BOARD}/Release/chrome')
  INPUT_ORDERFILE_PATH = ('/build/${BOARD}/opt/google/chrome/'
                          'chrome.orderfile.txt')

  def __init__(self, board, output_dir, chrome_root, chroot_path, chroot_args):
    """Construct an object for generating orderfile for Chrome.

    Args:
      board: Name of the board.
      output_dir: Directory (outside chroot) to save the output artifacts.
      chrome_root: Path to the Chrome source.
      chroot_path: Path to the chroot.
      chroot_args: The arguments used to enter the chroot.
    """
    self.output_dir = output_dir
    self.orderfile_name = _GetOrderfileName(chrome_root)
    self.chrome_binary = self.CHROME_BINARY_PATH.replace('${BOARD}', board)
    self.input_orderfile = self.INPUT_ORDERFILE_PATH.replace('${BOARD}', board)
    self.chroot_path = chroot_path
    self.working_dir = os.path.join(self.chroot_path, 'tmp')
    self.working_dir_inchroot = '/tmp'
    self.chroot_args = chroot_args
    self.tarballs = []

  def _CheckArguments(self):
    """Make sure the arguments received are correct."""
    if not os.path.isdir(self.output_dir):
      raise GenerateChromeOrderfileError(
          'Non-existent directory %s specified for --out-dir' %
          (self.output_dir,))

    chrome_binary_path_outside = os.path.join(self.chroot_path,
                                              self.chrome_binary[1:])
    if not os.path.exists(chrome_binary_path_outside):
      raise GenerateChromeOrderfileError(
          'Chrome binary does not exist at %s in chroot' %
          (chrome_binary_path_outside,))

    chrome_orderfile_path_outside = os.path.join(self.chroot_path,
                                                 self.input_orderfile[1:])
    if not os.path.exists(chrome_orderfile_path_outside):
      raise GenerateChromeOrderfileError(
          'No orderfile generated in the builder.')

  def _GenerateChromeNM(self):
    """Generate symbols by running nm command on Chrome binary.

    This command runs inside chroot.
    """
    cmd = ['llvm-nm', '-n', self.chrome_binary]
    result_inchroot = os.path.join(self.working_dir_inchroot,
                                   self.orderfile_name + '.nm')
    result_out_chroot = os.path.join(self.working_dir,
                                     self.orderfile_name + '.nm')

    try:
      cros_build_lib.run(
          cmd,
          stdout=result_out_chroot,
          enter_chroot=True,
          chroot_args=self.chroot_args)
    except cros_build_lib.RunCommandError:
      raise GenerateChromeOrderfileError(
          'Unable to run %s to get nm on Chrome binary' % (cmd))

    # Return path inside chroot
    return result_inchroot

  def _PostProcessOrderfile(self, chrome_nm):
    """Use toolchain script to do post-process on the orderfile.

    This command runs inside chroot.
    """
    result = os.path.join(self.working_dir_inchroot,
                          self.orderfile_name + '.orderfile')
    cmd = [
        self.PROCESS_SCRIPT, '--chrome', chrome_nm, '--input',
        self.input_orderfile, '--output', result
    ]

    try:
      cros_build_lib.run(
          cmd, enter_chroot=True, chroot_args=self.chroot_args)
    except cros_build_lib.RunCommandError:
      raise GenerateChromeOrderfileError(
          'Unable to run %s to process orderfile.' % (cmd))

    # Return path inside chroot
    return result

  def Bundle(self):
    """Generate post-processed Chrome orderfile and create tarball."""
    self._CheckArguments()
    chrome_nm = self._GenerateChromeNM()
    orderfile = self._PostProcessOrderfile(chrome_nm)
    self.tarballs = _CompressAFDOFiles(
        [chrome_nm, orderfile], self.working_dir, self.output_dir,
        XZ_COMPRESSION_SUFFIX)

  def Perform(self):
    """Generate post-processed Chrome orderfile and create tarball."""
    self.Bundle()
    for t in self.tarballs:
      _UploadAFDOArtifactToGSBucket(ORDERFILE_GS_URL_UNVETTED, t)


class UpdateEbuildWithAFDOArtifacts(object):
  """Class to update ebuild with unvetted AFDO artifacts.

  This class is used to verify an unvetted AFDO artifact, by patching
  the ebuild of the package with a dictionary of rules.

  This class runs inside chroot, so all the paths should be relative
  inside chroot.
  """

  def __init__(self, board, package, update_rules):
    """Construct an object for updating ebuild with AFDO artifacts.

    Args:
      board: Name of the board.
      package: Name of the package to test with.
      update_rules: A dict containing pairs of (key, value) used to
      patch ebuild. "key" will be used to search ebuild as an variable,
      and update that variable with "value".
    """
    self.board = board
    self.package = package
    self.update_rules = update_rules

  def _PatchEbuild(self, ebuild_file):
    """Patch the specified ebuild to use the artifact.

    Args:
      ebuild_file: path to the ebuild file.

    Raises:
      UpdateEbuildWithAFDOArtifactsError: If marker is not found.
    """
    original_ebuild = path_util.FromChrootPath(ebuild_file)
    modified_ebuild = '%s.new' % original_ebuild

    patterns = []
    replacements = []
    for name, value in self.update_rules.items():
      patterns.append(re.compile(AFDO_ARTIFACT_EBUILD_REGEX % name))
      replacements.append(AFDO_ARTIFACT_EBUILD_REPL % value)

    found = set()
    with open(original_ebuild) as original, \
         open(modified_ebuild, 'w') as modified:
      for line in original:
        matched = False
        for p, r in zip(patterns, replacements):
          matched = p.match(line)
          if matched:
            found.add(r)
            modified.write(p.sub(r, line))
            # Each line can only match one pattern
            break
        if not matched:
          modified.write(line)
    not_updated = set(replacements) - found
    if not_updated:
      logging.info('Unable to update %s in the ebuild', not_updated)
      raise UpdateEbuildWithAFDOArtifactsError(
          'Ebuild file does not have appropriate marker for AFDO/orderfile.')

    os.rename(modified_ebuild, original_ebuild)
    for name, value in self.update_rules.items():
      logging.info('Patched %s with (%s, %s)', original_ebuild, name, value)

  def _UpdateManifest(self, ebuild_file):
    """Regenerate the Manifest file. (To update orderfile)

    Args:
      ebuild_file: path to the ebuild file
    """
    ebuild_prog = 'ebuild-%s' % self.board
    cmd = [ebuild_prog, ebuild_file, 'manifest', '--force']
    cros_build_lib.run(cmd, enter_chroot=True)

  def Perform(self):
    """Main function to update ebuild with the rules."""
    ebuild = _FindEbuildPath(self.package, board=self.board)
    self._PatchEbuild(ebuild)
    # Patch the chrome 9999 ebuild too, as the manifest will use
    # 9999 ebuild.
    ebuild_9999 = os.path.join(
        os.path.dirname(ebuild), self.package + '-9999.ebuild')
    self._PatchEbuild(ebuild_9999)
    # Use 9999 ebuild to update manifest.
    self._UpdateManifest(ebuild_9999)


def _RankValidBenchmarkProfiles(name):
  """Calculate a value used to rank valid benchmark profiles.

  Args:
    name: A name or a full path of a possible benchmark profile.

  Returns:
    A BenchmarkProfileNamedTuple used for ranking if the name
    is a valid benchmark profile. Otherwise, returns None.
  """
  try:
    version = _ParseBenchmarkProfileName(os.path.basename(name))
    # Filter out merged benchmark profiles.
    if version.is_merged:
      return None
    return version
  except ProfilesNameHelperError:
    return None


def _RankValidCWPProfiles(name):
  """Calculate a value used to rank valid CWP profiles.

  Args:
    name: A name or a full path of a possible CWP profile.

  Returns:
    The "clock" part of the CWP profile, used for ranking if the
    name is a valid CWP profile. Otherwise, returns None.
  """
  try:
    return _ParseCWPProfileName(os.path.basename(name)).clock
  except ProfilesNameHelperError:
    return None


def _RankValidOrderfiles(name):
  """Calculcate a value used to rank valid orderfiles.

  Args:
    name: A name or a full path of a possible orderfile.

  Returns:
    A tuple of (BenchmarkProfileNamedTuple, "clock" part of CWP profile),
    or returns None if the name is not a valid orderfile name.

  Raises:
    ValueError, if the parsed orderfile is not valid.
  """
  try:
    benchmark_part, cwp_part = _ParseMergedProfileName(os.path.basename(name))
    if benchmark_part.is_merged:
      raise ValueError(
          '-merged should not appear in orderfile or release AFDO name.')
    return benchmark_part, cwp_part.clock
  except ProfilesNameHelperError:
    return None


def _FindLatestAFDOArtifact(gs_url, ranking_function):
  """Find the latest AFDO artifact in a GS bucket.

  Always finds profiles that matches the current Chrome branch.

  Args:
    gs_url: The full path to GS bucket URL.
    ranking_function: A function used to evaluate a full url path.
    e.g. foo(x) should return a value that can use to rank the profile,
    or return None if the url x is not relevant.

  Returns:
    The name of the eligible latest AFDO artifact.

  Raises:
    RuntimeError: If no files matches the regex in the bucket.
    ValueError: if regex is not valid.
  """

  def _FilterResultsBasedOnBranch(branch):
    """Filter out results that doesn't belong to this branch."""
    # The branch should appear in the name either as:
    # R78-12371.22-1566207135 for kernel/CWP profiles
    # OR chromeos-chrome-amd64-78.0.3877.0 for benchmark profiles
    return [
        x for x in all_files
        if 'R%s' % branch in x.url or '-%s.' % branch in x.url
    ]

  gs_context = gs.GSContext()

  # Obtain all files from gs_url and filter out those not match
  # pattern. And filter out text files for legacy PFQ like
  # latest-chromeos-chrome-amd64-79.afdo
  all_files = [
      x for x in gs_context.List(gs_url, details=True) if 'latest-' not in x.url
  ]
  chrome_branch = _FindCurrentChromeBranch()
  results = _FilterResultsBasedOnBranch(chrome_branch)

  if not results:
    # If no results found, it's maybe because we just branched.
    # Try to find the latest profile from last branch.
    results = _FilterResultsBasedOnBranch(str(int(chrome_branch) - 1))
    if not results:
      raise RuntimeError(
          'No files found on %s for branch %s' % (gs_url, chrome_branch))

  ranked_results = [(ranking_function(x.url), x.url) for x in results]
  filtered_results = [x for x in ranked_results if x[0] is not None]
  if not filtered_results:
    raise RuntimeError('No valid latest artifact was found on %s'
                       '(example invalid artifact: %s).' %
                       (gs_url, results[0].url))

  latest = max(filtered_results)
  name = os.path.basename(latest[1])
  logging.info('Latest AFDO artifact in %s is %s', gs_url, name)
  return name


def _FindCurrentChromeBranch():
  """Find the current Chrome branch from Chrome ebuild.

  Returns:
    The branch version as a str.
  """
  chrome_ebuild = os.path.basename(_FindEbuildPath('chromeos-chrome'))
  pattern = re.compile(
      r'chromeos-chrome-(?P<branch>\d+)\.\d+\.\d+\.\d+(?:_rc)?-r\d+.ebuild')
  match = pattern.match(chrome_ebuild)
  if not match:
    raise ValueError('Unparseable chrome ebuild name: %s' % chrome_ebuild)

  return match.group('branch')


def _GetProfileAge(profile, artifact_type):
  """Tell the age of profile_version in days.

  Args:
    profile: Name of the profile. Different artifact_type has different
    format. For kernel_afdo, it looks like: R78-12371.11-1565602499.
    The last part is the timestamp.
    artifact_type: Only 'kernel_afdo' is supported now.

  Returns:
    Age of profile_version in days.

  Raises:
    ValueError: if the artifact_type is not supported.
  """

  if artifact_type == 'kernel_afdo':
    return (datetime.datetime.utcnow() - datetime.datetime.utcfromtimestamp(
        int(profile.split('-')[-1]))).days

  raise ValueError('Only kernel afdo is supported to check profile age.')


def _WarnSheriffAboutKernelProfileExpiration(kver, profile):
  """Send emails to toolchain sheriff to warn the soon expired profiles.

  Args:
    kver: Kernel version.
    profile: Name of the profile.
  """
  # FIXME(tcwang): Fix the subject and email format before deployment.
  subject_msg = (
      '[Test Async builder] Kernel AutoFDO profile too old for kernel %s' %
      kver)
  alert_msg = ('AutoFDO profile too old for kernel %s. Name=%s' % kver, profile)
  alerts.SendEmailLog(
      subject_msg,
      AFDO_ALERT_RECIPIENTS,
      server=alerts.SmtpServer(constants.GOLO_SMTP_SERVER),
      message=alert_msg)


_EbuildInfo = collections.namedtuple('_EbuildInfo', ['path', 'CPV'])
class _CommonPrepareBundle(object):
  """Information about Ebuild files we care about."""
  def __init__(self, artifact_name, chroot=None, sysroot_path=None,
               build_target=None, input_artifacts=None):
    self._gs_context = None
    self.artifact_name = artifact_name
    self.chroot = chroot
    self.sysroot_path = sysroot_path
    self.build_target = build_target
    # Turn the input artifacts list into a dictionary.
    self.input_artifacts = input_artifacts or []
    self._ebuild_info = {}

  @property
  def gs_context(self):
    """Get the current GS context.  May create one."""
    if not self._gs_context:
      self._gs_context = gs.GSContext()
    return self._gs_context

  @property
  def chrome_branch(self):
    """Return the branch number for chrome."""
    pkg = constants.CHROME_PN
    info = self._ebuild_info.get(pkg, self._GetEbuildInfo(pkg))
    return info.CPV.version_no_rev.split('.')[0]

  def _GetEbuildInfo(self, package, category=None):
    """Get the ebuild info for a cataegory/package in chromiumos-overlay.

    Args:
      package (str): package name (e.g. chromeos-chrome or chromeos-kernel-4_4)
      category (str): category (e.g. chromeos-base, or sys-kernel)

    Returns:
      _EbuildInfo for the stable ebuild.
    """
    if package in self._ebuild_info:
      return self._ebuild_info[package]

    if category is None:
      if package == constants.CHROME_PN:
        category = constants.CHROME_CN
      else:
        category = 'sys-kernel'

    # The stable ebuild path has at least one '.' in the version.
    paths = glob.glob(os.path.join(
        _CHROMIUMOS_OVERLAY, category, package, '*-*.*.ebuild'))
    if len(paths) == 1:
      PV = os.path.splitext(os.path.split(paths[0])[1])[0]
      info = _EbuildInfo(paths[0], portage_util.SplitCPV(
          '%s/%s' % (category, PV)))
      self._ebuild_info[constants.CHROME_PN] = info
      return info
    else:
      raise PrepareForBuildHandlerError(
          'Wrong number of %s/%s ebuilds found: %s'
          % (category, package, ', '.join(paths)))

  def _GetArtifactVersionInGob(self, arch):
    """Find the version (name) of AFDO artifact from GoB.

    Args:
      arch: There are three AFDO profiles in chromium:
           silvermont, broadwell, and airmont.

    Returns:
      The name of the AFDO artifact found on GoB, or None if not found.

    Raises:
      ValueError: when "arch" is not a supported.
      RuntimeError: when the file containing AFDO profile name can't be found.
    """
    if arch not in list(CHROME_AFDO_VERIFIER_BOARDS.values()):
      raise ValueError('Invalid architecture %s to use in AFDO profile' % arch)

    chrome_info = self._GetEbuildInfo(constants.CHROME_PN)
    rev = chrome_info.CPV.version_no_rev
    if rev.endswith('_rc'):
      rev = rev[:-3]
    profile_path = (
        'chromium/src/+/refs/tags/%s/chromeos/profiles/%s.afdo.newest.txt'
        '?format=text' % (rev, arch))

    contents = gob_util.FetchUrl(constants.EXTERNAL_GOB_HOST, profile_path)
    if not contents:
      raise RuntimeError(
          'Could not fetch https://%s/%s' % (
              constants.EXTERNAL_GOB_HOST, profile_path))

    return base64.decodestring(contents)

  def _GetArtifactVersionInEbuild(self, package, variable):
    """Find the version (name) of AFDO artifact from the ebuild.

    Args:
      package: name of the package (such as, 'chromeos-chrome')
      variable: name of the variable to find.

    Returns:
      The name of the AFDO artifact found in the ebuild, or None if not found.
    """
    info = self._GetEbuildInfo(package)
    ebuild = info.path
    pattern = re.compile(AFDO_ARTIFACT_EBUILD_REGEX % variable)
    with open(ebuild) as f:
      for line in f:
        match = pattern.match(line)
        if match:
          ret = match.group('name')
          if ret.startswith('"') and ret.endswith('"'):
            return ret[1:-1]
          return ret

    logging.info('%s is not found in the ebuild: %s', variable, ebuild)
    return None

  def _GetOrderfileName(self):
    """Get the name of the orderfile."""
    benchmark_afdo, cwp_afdo = _ParseMergedProfileName(
        self._GetArtifactVersionInGob(arch='silvermont'))
    return 'chromeos-chrome-orderfile-%s.orderfile' % (
        _GetCombinedAFDOName(cwp_afdo, 'field', benchmark_afdo))

  def _FindLatestOrderfileArtifact(self, gs_url):
    """Find the latest Ordering file artifact in a bucket.

    Args:
      gs_url: The full path to the GS bucket URL.

    Returns:
      The name of the latest eligible ordering file artifact.

    Raises:
      See _FindLatestAFDOArtifact.
    """
    return self._FindLatestAFDOArtifact(gs_url, self._RankValidOrderfiles)

  def _FindLatestAFDOArtifact(self, gs_url, rank_func):
    """Find the latest AFDO artifact in a bucket.

    Args:
      gs_url: The full path to the GS bucket URL.
      rank_func: A function to compare two URLs.  It is passed two URLs, and
          returns whether the first is more or less preferred than the second:
            negative: less preferred.
            zero: equally preferred.
            positive: more preferred.

    Returns:
      The name of the latest eligible AFDO artifact.

    Raises:
      RuntimeError: If no files matches the regex in the bucket.
      ValueError: if regex is not valid.
    """

    def _IsUrlOnBranch(url, branch):
      """Return true if the url matches this branch."""
      # Legacy PFQ results look like: latest-chromeos-chrome-amd64-79.afdo.
      # The branch should appear in the name either as:
      #   R78-12371.22-1566207135 for kernel/CWP profiles, OR
      #   chromeos-chrome-amd64-78.0.3877.0 for benchmark profiles
      return ('latest-' not in url and
              ('R%s-' % branch in url or '-%s.' % branch in url))

    # Obtain all files from gs_url and filter out those not match
    # pattern. And filter out text files for legacy PFQ like
    # latest-chromeos-chrome-amd64-79.afdo
    all_files = self.gs_context.List(gs_url, details=True)
    results = [
        x for x in all_files if _IsUrlOnBranch(x.url, self.chrome_branch)]

    if not results:
      # If no results found, it's maybe because we just branched.
      # Try to find the latest profile from last branch.
      results = [
          x
          for x in all_files
          if _IsUrlOnBranch(x.url, str(int(self.chrome_branch) - 1))]

    if not results:
      raise RuntimeError(
          'No files found on %s for branch %s' % (gs_url, self.chrome_branch))

    latest = None
    for res in results:
      rank = rank_func(res.url)
      if rank and (not latest or [rank, ''] > latest):
        latest = [rank, res.url]

    if not latest:
      raise RuntimeError('No valid latest artifact was found on %s'
                         '(example invalid artifact: %s).' %
                         (gs_url, results[0].url))

    name = os.path.basename(latest[1])
    logging.info('Latest AFDO artifact in %s is %s', gs_url, name)
    return name

  def _UpdateEbuildWithArtifacts(self, package, update_rules):
    """Update a package ebuild file with artifacts.

    Args:
      package: name of package to update (no category.)
      update_rules: dict{'variable': 'value'} to apply.
    """
    info = self._GetEbuildInfo(package)
    self._PatchEbuild(info, update_rules, uprev=True)
    CPV_9999 = '%s/%s-9999' % (info.CPV.category, info.CPV.package)
    ebuild_9999 = os.path.join(
        os.path.dirname(info.path), '%s-9999.ebuild' % package)
    info_9999 = _EbuildInfo(ebuild_9999, portage_util.SplitCPV(CPV_9999))
    self._PatchEbuild(info_9999, update_rules, uprev=False)

  def _PatchEbuild(self, info, rules, uprev):
    """Patch an ebuild file, possibly upreving it.

    Args:
      info: _EbuildInfo describing the ebuild file.
      rules: dict of key:value pairs to apply to the ebuild.
      uprev: whether to increment the revision.  Should be False for 9999
          ebuilds, and True otherwise.

    Returns:
      Updated CPV for the ebuild.
    """
    logging.info('Patching %s with %s', info.path, str(rules))
    old_name = info.path
    new_name = '%s.new' % old_name

    _Patterns = collections.namedtuple('_Patterns', ['match', 'sub'])
    patterns = set(
        _Patterns(re.compile(AFDO_ARTIFACT_EBUILD_REGEX % k),
                  AFDO_ARTIFACT_EBUILD_REPL % v)
        for k, v in rules.items())

    want = patterns.copy()
    with open(old_name) as old, open(new_name, 'w') as new:
      for line in old:
        for match, sub in patterns:
          line, count = match.subn(sub, line, count=1)
          if count:
            want.remove((match, sub))
            # Can only match one pattern.
            break
        new.write(line)
      if want:
        logging.info('Unable to update %s in the ebuild', [x.sub for x in want])
        raise UpdateEbuildWithAFDOArtifactsError(
            'Ebuild file does not have appropriate marker for AFDO/orderfile.')

    CPV = info.CPV
    if uprev:
      assert CPV.version != '9999'
      new_rev = 'r%d' % (int(CPV.rev[1:]) + 1) if CPV.rev else 'r1'
      new_CPV = '%s/%s-%s-%s' % (
          CPV.category, CPV.package, CPV.version_no_rev, new_rev)
      new_path = os.path.join(
          os.path.dirname(info.path), '%s.ebuild' % os.path.basename(new_CPV))
      os.rename(new_name, new_path)
      osutils.SafeUnlink(old_name)
      ebuild_file = new_path
      CPV = _EbuildInfo(new_path, portage_util.SplitCPV(new_CPV))
    else:
      assert CPV.version == '9999'
      os.rename(new_name, old_name)
      ebuild_file = old_name

    if self.build_target:
      ebuild_prog = 'ebuild-%s' % self.build_target
      cmd = [ebuild_prog, path_util.ToChrootPath(ebuild_file, self.chroot.path),
             'manifest', '--force']
      cros_build_lib.run(cmd, enter_chroot=True)

    return CPV

  @staticmethod
  def _RankValidOrderfiles(url):
    """Rank the given URL for comparison."""
    try:
      bench, cwp = _ParseMergedProfileName(os.path.basename(url))
      if bench.is_merged:
        raise ValueError(
            '-merged should not appear in orderfile or release AFDO name.')
      return bench, cwp
    except ProfilesNameHelperError:
      return None

  @staticmethod
  def _RankValidBenchmarkProfiles(name):
    """Calculate a value used to rank valid benchmark profiles.

    Args:
      name: A name or a full path of a possible benchmark profile.

    Returns:
      A BenchmarkProfileNamedTuple used for ranking if the name
      is a valid benchmark profile. Otherwise, returns None.
    """
    try:
      version = _ParseBenchmarkProfileName(os.path.basename(name))
      # Filter out merged benchmark profiles.
      if version.is_merged:
        return None
      return version
    except ProfilesNameHelperError:
      return None


class PrepareForBuildHandler(_CommonPrepareBundle):
  """Methods for updating ebuilds for toolchain artifacts."""

  def __init__(self, artifact_name, chroot, sysroot_path, build_target,
               input_artifacts):
    super(PrepareForBuildHandler, self).__init__(
        artifact_name, chroot, sysroot_path, build_target, input_artifacts)
    self._prepare_func = getattr(self, '_Prepare' + artifact_name)

  def Prepare(self):
    return self._prepare_func()

  def _PrepareUnverifiedOrderingFile(self):
    """Prepare to build an unverified ordering file."""
    orderfile_name = self._GetOrderfileName()

    # If the (unverified) artifact already exists in our location, then the
    # build is pointless.
    loc = self.input_artifacts.get(
        'UnverifiedOrderingFile', [ORDERFILE_GS_URL_UNVETTED])[0]
    path = os.path.join(loc, orderfile_name + XZ_COMPRESSION_SUFFIX)
    if self.gs_context.Exists(path):
      # Artifact already created.
      logging.info('Found %s.', path)
      return PrepareForBuildReturn.POINTLESS

    # No changes are needed in any ebuild, use flags take care of everything.

    # We need this build.
    logging.info('No UnverifiedOrderingFile found.')
    return PrepareForBuildReturn.NEEDED

  def _PrepareVerifiedOrderingFile(self):
    """Prepare to verify an unvetted ordering file."""
    # We will look for the input artifact in the given path, but we only check
    # for the vetted artifact in the first location given.
    locations = self.input_artifacts.get('UnverifiedOrderingFile',
                                         [ORDERFILE_GS_URL_UNVETTED])
    vetted_loc = os.path.join(os.path.dirname(locations[0]), 'vetted')
    name = None
    loc = None
    for loc in locations:
      try:
        name = self._FindLatestOrderfileArtifact(loc)
        break
      except RuntimeError as err:
        logging.warning('No unverified ordering file in %s: %s', loc, err)

    if not name:
      raise RuntimeError(str(err))

    vetted_path = os.path.join(vetted_loc, name)
    if self.gs_context.Exists(vetted_path):
      # The latest unverified ordering file has already been verified.
      logging.info('Pointless build: "%s" exists.', vetted_path)
      return PrepareForBuildReturn.POINTLESS
    # If we don't have an SDK, then we cannot update the manifest.  There is
    # little benefit in patching the ebuild before we can update the manifest.
    if self.chroot:
      self._PatchEbuild(self._GetEbuildInfo(constants.CHROME_PN),
                        {'UNVETTED_ORDERFILE': os.path.splitext(name)[0],
                         'UNVETTED_ORDERFILE_LOCATION': loc}, uprev=True)
    else:
      logging.info('No chroot: not patching ebuild.')
    return PrepareForBuildReturn.NEEDED

  def _PrepareChromeClangWarningsFile(self):
    # We always build this artifact.
    return PrepareForBuildReturn.NEEDED

  def _PrepareUnverifiedLlvmPgoFile(self):
    # TODO(crbug/1019868): implement
    return PrepareForBuildReturn.UNKNOWN

  def _PrepareUnverifiedChromeAfdoFile(self):
    # TODO(crbug/1019868): implement
    return PrepareForBuildReturn.UNKNOWN

  def _PrepareVerifiedChromeAfdoFile(self):
    # TODO(crbug/1019868): implement
    return PrepareForBuildReturn.UNKNOWN

  def _PrepareVerifiedKernelAfdoFile(self):
    # TODO(crbug/1019868): implement
    return PrepareForBuildReturn.UNKNOWN


class BundleArtifactHandler(_CommonPrepareBundle):
  """Methods for updating ebuilds for toolchain artifacts."""

  def __init__(self, artifact_name, chroot, sysroot_path, build_target,
               output_dir):
    super(BundleArtifactHandler, self).__init__(
        artifact_name, chroot, sysroot_path, build_target)
    self._bundle_func = getattr(self, '_Bundle' + artifact_name)
    self.output_dir = output_dir

  def Bundle(self):
    return self._bundle_func()

  def _BundleUnverifiedOrderingFile(self):
    """Bundle to build an unverified ordering file."""
    with self.chroot.tempdir() as tempdir:
      GenerateChromeOrderfile(
          board=self.build_target,
          output_dir=tempdir,
          chrome_root=self.chroot.chrome_root,
          chroot_path=self.chroot.path,
          chroot_args=self.chroot.get_enter_args()).Bundle()

      files = []
      for path in osutils.DirectoryIterator(tempdir):
        if os.path.isfile(path):
          rel_path = os.path.relpath(path, tempdir)
          files.append(os.path.join(self.output_dir, rel_path))
      osutils.CopyDirContents(tempdir, self.output_dir, allow_nonempty=True)

    return files

  def _BundleVerifiedOrderingFile(self):
    """Bundle vetted ordering file."""
    orderfile_name = self._GetArtifactVersionInEbuild(
        constants.CHROME_PN, 'UNVETTED_ORDERFILE') + XZ_COMPRESSION_SUFFIX
    # Strip the leading / from sysroot_path.
    orderfile_path = os.path.join(
        self.chroot.path, self.sysroot_path[1:],
        'opt/google/chrome', orderfile_name)
    verified_orderfile = os.path.join(self.output_dir, orderfile_name)
    shutil.copy2(orderfile_path, verified_orderfile)
    return [verified_orderfile]

  def _BundleChromeClangWarningsFile(self):
    """Bundle clang-tidy warnings file."""
    with self.chroot.tempdir() as tempdir:
      in_chroot_path = self.chroot.chroot_path(tempdir)
      clang_tidy_tarball = '%s.%s.clang_tidy_warnings.tar.xz' % (
          self.build_target,
          datetime.datetime.strftime(datetime.datetime.now(), '%Y%m%d'))
      cmd = [
          'cros_generate_tidy_warnings', '--out-file', clang_tidy_tarball,
          '--out-dir', in_chroot_path, '--board', self.build_target,
          '--logs-dir', os.path.join('/tmp/clang-tidy-logs', self.build_target)
      ]
      cros_build_lib.run(cmd, cwd=self.chroot.path, enter_chroot=True)
      artifact_path = os.path.join(self.output_dir, clang_tidy_tarball)
      shutil.copy2(
          os.path.join(tempdir, clang_tidy_tarball), artifact_path)
    return [artifact_path]

  def _BundleUnverifiedLlvmPgoFile(self):
    # TODO(crbug/1019868): implement
    return []

  def _BundleUnverifiedChromeAfdoFile(self):
    # TODO(crbug/1019868): implement
    return []

  def _BundleVerifiedChromeAfdoFile(self):
    # TODO(crbug/1019868): implement
    return []

  def _BundleVerifiedKernelAfdoFile(self):
    # TODO(crbug/1019868): implement
    return []


def PrepareForBuild(artifact_name, chroot, sysroot_path, build_target,
                    input_artifacts):
  """Prepare for building artifacts.

  This code is called OUTSIDE the chroot, before it is set up.

  Args:
    artifact_name: artifact name
    chroot: chroot_lib.Chroot instance for chroot.
    sysroot_path: path to sysroot, relative to chroot path, or None.
    build_target: name of build target, or None.
    input_artifacts: List(InputArtifactInfo) of available artifact locations.

  Returns:
    PrepareForBuildReturn
  """

  return PrepareForBuildHandler(
      artifact_name, chroot, sysroot_path, build_target,
      input_artifacts).Prepare()


def BundleArtifacts(name, chroot, sysroot_path, build_target, output_dir):
  """Prepare for building artifacts.

  This code is called OUTSIDE the chroot, after it is set up.

  Args:
    name: artifact name
    chroot: chroot_lib.Chroot instance for chroot.
    sysroot_path: path to sysroot, relative to chroot path.
    chrome_root: path to chrome root.
    build_target: name of build target
    output_dir: path in which to place the artifacts.

  Returns:
    list of artifacts, relative to output_dir.
  """
  return BundleArtifactHandler(
      name, chroot, sysroot_path, build_target, output_dir).Bundle()


# ###########################################################################
#
# TODO(crbug/1019868): delete once cbuildbot is gone.
# LEGACY CODE: used primarily by cbuildbot.
#
# ###########################################################################
def OrderfileUpdateChromeEbuild(board):
  """Update Chrome ebuild with latest unvetted orderfile.

  Args:
    board: Board type that was built on this machine.

  Returns:
    Status of the update.
  """

  orderfile_name = _FindLatestAFDOArtifact(ORDERFILE_GS_URL_UNVETTED,
                                           _RankValidOrderfiles)

  if not orderfile_name:
    logging.info('No eligible orderfile to verify.')
    return False

  updater = UpdateEbuildWithAFDOArtifacts(
      board=board,
      package='chromeos-chrome',
      update_rules={'UNVETTED_ORDERFILE': os.path.splitext(orderfile_name)[0]})
  updater.Perform()
  return True


def AFDOUpdateChromeEbuild(board):
  """Update Chrome ebuild with latest unvetted AFDO profiles.

  Args:
    board: Board to verify the Chrome afdo.

  Returns:
    Status of the update.
  """
  benchmark_afdo = _FindLatestAFDOArtifact(BENCHMARK_AFDO_GS_URL,
                                           _RankValidBenchmarkProfiles)
  arch = CHROME_AFDO_VERIFIER_BOARDS[board]
  url = os.path.join(CWP_AFDO_GS_URL, arch)
  cwp_afdo = _FindLatestAFDOArtifact(url, _RankValidCWPProfiles)

  if not benchmark_afdo or not cwp_afdo:
    logging.info('No eligible benchmark or cwp AFDO to verify.')
    return False

  with osutils.TempDir() as tempdir:
    result = _CreateReleaseChromeAFDO(
        os.path.splitext(cwp_afdo)[0], arch,
        os.path.splitext(benchmark_afdo)[0], tempdir)
    afdo_profile_for_verify = os.path.join('/tmp', os.path.basename(result))
    os.rename(result, afdo_profile_for_verify)

  updater = UpdateEbuildWithAFDOArtifacts(
      board=board,
      package='chromeos-chrome',
      update_rules={'UNVETTED_AFDO_FILE': afdo_profile_for_verify})
  updater.Perform()
  return True


def AFDOUpdateKernelEbuild(board):
  """Update kernel ebuild with latest unvetted AFDO profile.

  Args:
    board: Board to verify the kernel type.

  Returns:
    Status of the update.
  """

  # For kernel profiles, need to find out the current branch in order to
  # filter out profiles on other branches.
  url = os.path.join(KERNEL_PROFILE_URL, KERNEL_AFDO_VERIFIER_BOARDS[board])
  kernel_afdo = _FindLatestAFDOArtifact(url, _RankValidCWPProfiles)
  kver = KERNEL_AFDO_VERIFIER_BOARDS[board].replace('.', '_')
  # The kernel_afdo from GS bucket contains .gcov.xz suffix, but the name
  # of the afdo in kernel ebuild doesn't. Need to strip it out.
  kernel_afdo_in_ebuild = kernel_afdo.replace(KERNEL_AFDO_COMPRESSION_SUFFIX,
                                              '')

  # Check freshness
  age = _GetProfileAge(kernel_afdo_in_ebuild, 'kernel_afdo')
  if age > KERNEL_ALLOWED_STALE_DAYS:
    logging.info('Found an expired afdo for kernel %s: %s, skip.', kver,
                 kernel_afdo_in_ebuild)
    return False

  if age > KERNEL_WARN_STALE_DAYS:
    _WarnSheriffAboutKernelProfileExpiration(kver, kernel_afdo_in_ebuild)

  updater = UpdateEbuildWithAFDOArtifacts(
      board=board,
      package='chromeos-kernel-' + kver,
      update_rules={'AFDO_PROFILE_VERSION': kernel_afdo_in_ebuild})
  updater.Perform()
  return True


class GenerateBenchmarkAFDOProfile(object):
  """Class that generate benchmark AFDO profile.

  This class waits for the raw sampled profile data (perf.data) from another
  stage that runs hardware tests and uploads it to GS bucket. Once the profile
  is seen, this class downloads the raw profile, and processes it into
  LLVM-readable AFDO profiles. Then uploads the AFDO profile. This class also
  uploads an unstripped Chrome binary for debugging purpose if needed.
  """

  AFDO_GENERATE_LLVM_PROF = '/usr/bin/create_llvm_prof'
  CHROME_DEBUG_BIN = os.path.join('%(root)s', 'build/%(board)s/usr/lib/debug',
                                  'opt/google/chrome/chrome.debug')

  def __init__(self, board, output_dir, chroot_path, chroot_args):
    """Construct an object for generating benchmark profiles.

    Args:
      board: The name of the board.
      output_dir: The path to save output files.
      chroot_path: The chroot path.
      chroot_args: The arguments used to enter chroot.
    """
    self.board = board
    self.chroot_path = chroot_path
    self.chroot_args = chroot_args
    self.buildroot = os.path.join(chroot_path, '..')
    self.chrome_cpv = portage_util.PortageqBestVisible(
        constants.CHROME_CP, cwd=self.buildroot)
    self.arch = portage_util.PortageqEnvvar(
        'ARCH', board=board, allow_undefined=True)
    # Directory used to store intermediate artifacts
    self.working_dir = os.path.join(self.chroot_path, 'tmp')
    self.working_dir_inchroot = '/tmp'
    # Output directory
    self.output_dir = output_dir

  def _DecompressAFDOFile(self, to_decompress):
    """Decompress file used by AFDO process.

    Args:
      to_decompress: File to decompress.

    Returns:
      The full path to the uncompressed file.
    """
    basename = os.path.basename(to_decompress)
    dest_basename = os.path.splitext(basename)[0]
    dest = os.path.join(self.working_dir, dest_basename)
    cros_build_lib.UncompressFile(to_decompress, dest)
    return dest

  def _GetPerfAFDOName(self):
    """Construct a name for perf.data for current chrome version."""
    # The file name of the perf data is based only in the chrome version.
    # The test case that produces it does not know anything about the
    # revision number.
    # TODO(llozano): perf data filename should include the revision number.
    version_number = self.chrome_cpv.version_no_rev.split('_')[0]
    chrome_spec = {
        'package': self.chrome_cpv.package,
        'arch': self.arch,
        'version': version_number
    }
    return CHROME_PERF_AFDO_FILE % chrome_spec

  def _CheckAFDOPerfDataStatus(self):
    """Check whether AFDO perf data on GS bucket.

    Check if 'perf' data file for this architecture and release is available
    in GS.

    Returns:
      True if AFDO perf data is available. False otherwise.
    """
    gs_context = gs.GSContext()
    url = os.path.join(BENCHMARK_AFDO_GS_URL,
                       self._GetPerfAFDOName() + BZ2_COMPRESSION_SUFFIX)
    if not gs_context.Exists(url):
      logging.info('Could not find AFDO perf data at %s', url)
      return False

    logging.info('Found AFDO perf data at %s', url)
    return True

  def _WaitForAFDOPerfData(self):
    """Wait until hwtest finishes and the perf.data uploaded to GS bucket.

    Returns:
      True if hwtest finishes and perf.data is copied to local buildroot
      False if timeout.
    """
    try:
      timeout_util.WaitForReturnTrue(
          self._CheckAFDOPerfDataStatus,
          timeout=constants.AFDO_GENERATE_TIMEOUT,
          period=constants.SLEEP_TIMEOUT)
    except timeout_util.TimeoutError:
      logging.info('Could not find AFDO perf data before timeout')
      return False

    gs_context = gs.GSContext()
    url = os.path.join(BENCHMARK_AFDO_GS_URL,
                       self._GetPerfAFDOName() + BZ2_COMPRESSION_SUFFIX)
    dest_path = os.path.join(self.working_dir, url.rsplit('/', 1)[1])
    gs_context.Copy(url, dest_path)

    self._DecompressAFDOFile(dest_path)
    logging.info('Retrieved AFDO perf data to %s', dest_path)
    return True

  def _CreateAFDOFromPerfData(self):
    """Create LLVM-readable AFDO profile from raw sampled perf data.

    Returns:
      Name of the generated AFDO profile.
    """
    CHROME_UNSTRIPPED_NAME = 'chrome.unstripped'
    # create_llvm_prof demands the name of the profiled binary exactly matches
    # the name of the unstripped binary or it is named 'chrome.unstripped'.
    # So create a symbolic link with the appropriate name.
    debug_sym = os.path.join(self.working_dir, CHROME_UNSTRIPPED_NAME)
    debug_binary_inchroot = self.CHROME_DEBUG_BIN % {
        'root': '',
        'board': self.board
    }
    osutils.SafeSymlink(debug_binary_inchroot, debug_sym)

    # Call create_llvm_prof tool to generated AFDO profile from 'perf' profile
    # These following paths are converted to the path relative inside chroot.
    debug_sym_inchroot = os.path.join(self.working_dir_inchroot,
                                      CHROME_UNSTRIPPED_NAME)
    perf_afdo_inchroot_path = os.path.join(self.working_dir_inchroot,
                                           self._GetPerfAFDOName())
    afdo_name = _GetBenchmarkAFDOName(self.buildroot, self.board)
    afdo_inchroot_path = os.path.join(self.working_dir_inchroot, afdo_name)
    afdo_cmd = [
        self.AFDO_GENERATE_LLVM_PROF,
        '--binary=%s' % debug_sym_inchroot,
        '--profile=%s' % perf_afdo_inchroot_path,
        '--out=%s' % afdo_inchroot_path,
    ]
    cros_build_lib.run(
        afdo_cmd,
        enter_chroot=True,
        capture_output=True,
        print_cmd=True,
        chroot_args=self.chroot_args)

    logging.info('Generated %s AFDO profile %s', self.arch, afdo_name)
    return afdo_name

  def _UploadArtifacts(self, debug_bin, afdo_name):
    """Upload Chrome debug binary and AFDO profile with version info.

    Args:
      debug_bin: The name of debug Chrome binary.
      afdo_name: The name of AFDO profile.
    """

    afdo_spec = {
        'package': self.chrome_cpv.package,
        'arch': self.arch,
        'version': self.chrome_cpv.version
    }
    debug_bin_full_path = os.path.join(
        self.output_dir,
        os.path.basename(debug_bin) + BZ2_COMPRESSION_SUFFIX)
    chrome_version = CHROME_ARCH_VERSION % afdo_spec
    debug_bin_name_with_version = \
        chrome_version + '.debug' + BZ2_COMPRESSION_SUFFIX

    # Upload Chrome debug binary and rename it
    _UploadAFDOArtifactToGSBucket(
        BENCHMARK_AFDO_GS_URL,
        debug_bin_full_path,
        rename=debug_bin_name_with_version)

    # Upload Benchmark AFDO profile as is
    _UploadAFDOArtifactToGSBucket(
        BENCHMARK_AFDO_GS_URL,
        os.path.join(self.output_dir, afdo_name + BZ2_COMPRESSION_SUFFIX))

  def _GenerateAFDOData(self):
    """Generate AFDO profile data from 'perf' data.

    Given the 'perf' profile, generate an AFDO profile using
    create_llvm_prof. It also compresses Chrome debug binary to upload.

    Returns:
      The name created AFDO artifact.
    """
    debug_bin = self.CHROME_DEBUG_BIN % {
        'root': self.chroot_path,
        'board': self.board
    }
    afdo_name = self._CreateAFDOFromPerfData()

    # Because debug_bin and afdo file are in different paths, call the compress
    # function separately.
    _CompressAFDOFiles(
        targets=[debug_bin],
        input_dir=None,
        output_dir=self.output_dir,
        suffix=BZ2_COMPRESSION_SUFFIX)

    _CompressAFDOFiles(
        targets=[afdo_name],
        input_dir=self.working_dir,
        output_dir=self.output_dir,
        suffix=BZ2_COMPRESSION_SUFFIX)

    self._UploadArtifacts(debug_bin, afdo_name)
    return afdo_name

  def Perform(self):
    """Main function to generate benchmark AFDO profiles.

    Raises:
      GenerateBenchmarkAFDOProfilesError, if it never finds a perf.data
      file uploaded to GS bucket.
    """
    if self._WaitForAFDOPerfData():
      afdo_file = self._GenerateAFDOData()
      # FIXME(tcwang): reuse the CreateAndUploadMergedAFDOProfile from afdo.py
      # as a temporary solution to keep Android/Linux AFDO running.
      # Need to move the logic to this file, and run it either at generating
      # benchmark profiles, or at verifying Chrome profiles.
      gs_context = gs.GSContext()
      afdo.CreateAndUploadMergedAFDOProfile(gs_context, self.buildroot,
                                            afdo_file)
    else:
      raise GenerateBenchmarkAFDOProfilesError(
          'Could not find current "perf" profile.')


def CanGenerateAFDOData(board):
  """Does this board has the capability of generating its own AFDO data?"""
  return board in AFDO_DATA_GENERATORS_LLVM


def CanVerifyKernelAFDOData(board):
  """Does the board eligible to verify kernel AFDO profile.

  We use the name of the board to find the kernel version, so we only
  support using boards specified inside KERNEL_AFDO_VERIFIER_BOARDS.
  """
  return board in KERNEL_AFDO_VERIFIER_BOARDS


def CheckAFDOArtifactExists(buildroot, chrome_root, board, target):
  """Check if the artifact to upload in the builder already exists or not.

  Args:
    buildroot: The path to build root.
    chrome_root: The path to Chrome root.
    board: The name of the board.
    target: The target to check. It can be one of:
      "orderfile_generate": We need to check if the name of the orderfile to
      generate is already in the GS bucket or not.
      "orderfile_verify": We need to find if the latest unvetted orderfile is
      already verified or not.
      "benchmark_afdo": We need to check if the name of the benchmark AFDO
      profile is already in the GS bucket or not.
      "kernel_afdo": We need to check if the latest unvetted kernel profile is
      already verified or not.
      "chrome_afdo": We need to check if a release profile created with
      latest unvetted benchmark and CWP profiles is already verified or not.

  Returns:
    True if exists. False otherwise.

  Raises:
    ValueError if the target is invalid.
  """
  gs_context = gs.GSContext()
  if target == 'orderfile_generate':
    # For orderfile_generate builder, get the orderfile name from chrome ebuild
    orderfile_name = _GetOrderfileName(chrome_root)
    # Need to append the suffix
    orderfile_name += '.orderfile'
    return gs_context.Exists(
        os.path.join(ORDERFILE_GS_URL_UNVETTED,
                     orderfile_name + XZ_COMPRESSION_SUFFIX))

  if target == 'orderfile_verify':
    # Check if the latest unvetted orderfile is already verified
    orderfile_name = _FindLatestAFDOArtifact(ORDERFILE_GS_URL_UNVETTED,
                                             _RankValidOrderfiles)
    return gs_context.Exists(
        os.path.join(ORDERFILE_GS_URL_VETTED, orderfile_name))

  if target == 'benchmark_afdo':
    # For benchmark_afdo_generate builder, get the name from chrome ebuild
    afdo_name = _GetBenchmarkAFDOName(buildroot, board)
    return gs_context.Exists(
        os.path.join(BENCHMARK_AFDO_GS_URL,
                     afdo_name + BZ2_COMPRESSION_SUFFIX))

  if target == 'kernel_afdo':
    # Check the latest unvetted kernel AFDO is already verified
    source_url = os.path.join(KERNEL_PROFILE_URL,
                              KERNEL_AFDO_VERIFIER_BOARDS[board])
    dest_url = os.path.join(KERNEL_AFDO_GS_URL_VETTED,
                            KERNEL_AFDO_VERIFIER_BOARDS[board])
    afdo_name = _FindLatestAFDOArtifact(source_url, _RankValidCWPProfiles)
    return gs_context.Exists(os.path.join(dest_url, afdo_name))

  if target == 'chrome_afdo':
    # Check the latest unvetted chrome AFDO profiles are verified
    benchmark_afdo = _FindLatestAFDOArtifact(BENCHMARK_AFDO_GS_URL,
                                             _RankValidBenchmarkProfiles)
    arch = CHROME_AFDO_VERIFIER_BOARDS[board]
    source_url = os.path.join(CWP_AFDO_GS_URL, arch)
    cwp_afdo = _FindLatestAFDOArtifact(source_url, _RankValidCWPProfiles)
    merged_name = MERGED_AFDO_NAME % (_GetCombinedAFDOName(
        _ParseCWPProfileName(os.path.splitext(cwp_afdo)[0]),
        arch,
        _ParseBenchmarkProfileName(os.path.splitext(benchmark_afdo)[0])))
    # The profile name also contained 'redacted' info
    merged_name += '-redacted.afdo'
    return gs_context.Exists(
        os.path.join(RELEASE_AFDO_GS_URL_VETTED,
                     merged_name + XZ_COMPRESSION_SUFFIX))

  raise ValueError('Unsupported target %s to check' % target)


def _UploadVettedAFDOArtifacts(artifact_type, subcategory=None):
  """Upload vetted artifact type to GS bucket.

  When we upload vetted artifacts, there is a race condition that
  a new unvetted artifact might just be uploaded after the builder
  starts. Thus we don't want to poll the bucket for latest vetted
  artifact, and instead we look within the ebuilds.

  Args:
    artifact_type: The type of AFDO artifact to upload. Supports:
    ('orderfile', 'kernel_afdo', 'benchmark_afdo', 'cwp_afdo')
    subcategory: Optional. Name of subcategory, used to upload
    kernel AFDO or CWP AFDO.

  Returns:
    Name of the AFDO artifact, if successfully uploaded. Otherwise
    returns None.

  Raises:
    ValueError: if artifact_type is not supported, or subcategory
    is not provided for kernel_afdo or cwp_afdo.
  """
  gs_context = gs.GSContext()
  if artifact_type == 'orderfile':
    artifact = _GetArtifactVersionInEbuild('chromeos-chrome',
                                           'UNVETTED_ORDERFILE')
    full_name = artifact + XZ_COMPRESSION_SUFFIX
    source_url = os.path.join(ORDERFILE_GS_URL_UNVETTED, full_name)
    dest_url = os.path.join(ORDERFILE_GS_URL_VETTED, full_name)
  elif artifact_type == 'kernel_afdo':
    if not subcategory:
      raise ValueError('Subcategory is required for kernel_afdo.')
    artifact = _GetArtifactVersionInEbuild(
        'chromeos-kernel-' + subcategory.replace('.', '_'),
        'AFDO_PROFILE_VERSION')
    full_name = artifact + KERNEL_AFDO_COMPRESSION_SUFFIX
    source_url = os.path.join(KERNEL_PROFILE_URL, subcategory, full_name)
    dest_url = os.path.join(KERNEL_AFDO_GS_URL_VETTED, subcategory, full_name)
  else:
    raise ValueError('Only orderfile and kernel_afdo are supported.')

  if gs_context.Exists(dest_url):
    return None

  logging.info('Copying artifact from %s to %s', source_url, dest_url)
  gs_context.Copy(source_url, dest_url, acl='public-read')

  return artifact


def _PublishVettedAFDOArtifacts(json_file, uploaded, title=None):
  """Publish the uploaded AFDO artifact to metadata.

  Since there're no better ways for PUpr to keep track of uploading
  new files in GS bucket, we need to commit a change to a metadata file
  to reflect the upload.

  Args:
    json_file: The path to the JSON file that contains the metadata.
    uploaded: Dict contains pairs of (package, artifact) showing the
    newest name of uploaded 'artifact' to update for the 'package'.
    title: Optional. Used for putting into commit message.

  Raises:
    PublishVettedAFDOArtifactsError:
    if the artifact to update is not already in the metadata file, or
    if the uploaded artifact is the same as in metadata file (no new
    AFDO artifact uploaded). This should be caught earlier before uploading.
  """
  # Perform a git pull first to sync the checkout containing metadata,
  # in case there are some updates during the builder was running.
  # Possible race conditions are:
  # (1) Concurrent running builders modifying the same file, but different
  # entries. e.g. kernel_afdo.json containing three kernel profiles.
  # Sync'ing in this case should be safe no matter what.
  # (2) Concurrent updates to modify the same entry. This is unsafe because
  # we might update an entry with an older profile. The checking logic in the
  # function should guarantee we always update to a newer artifact, otherwise
  # the builder fails.
  git.RunGit(
      TOOLCHAIN_UTILS_PATH, ['pull', TOOLCHAIN_UTILS_REPO, 'refs/heads/master'],
      print_cmd=True)

  afdo_versions = json.loads(osutils.ReadFile(json_file))
  if title:
    commit_message = title + '\n\n'
  else:
    commit_message = 'afdo_metadata: Publish new profiles.\n\n'

  commit_body = 'Update %s from %s to %s\n'
  for package, artifact in uploaded.items():
    if not artifact:
      # It's OK that one package has nothing to publish because it's not
      # uploaded. The case of all packages have nothing to publish is already
      # checked before this function.
      continue
    if package not in afdo_versions:
      raise PublishVettedAFDOArtifactsError(
          'The key %s is not in JSON file.' % package)

    old_value = afdo_versions[package]['name']

    if package == 'benchmark':
      update_to_newer_profile = _RankValidBenchmarkProfiles(
          old_value) < _RankValidBenchmarkProfiles(artifact)
    else:
      update_to_newer_profile = _RankValidCWPProfiles(
          old_value) < _RankValidCWPProfiles(artifact)
    if not update_to_newer_profile:
      raise PublishVettedAFDOArtifactsError(
          'The artifact %s to update is not newer than the JSON file %s' %
          (artifact, old_value))

    afdo_versions[package]['name'] = artifact

    commit_message += commit_body % (package, str(old_value), artifact)

  with open(json_file, 'w') as f:
    json.dump(afdo_versions, f, indent=4)

  modifications = git.RunGit(
      TOOLCHAIN_UTILS_PATH, ['status', '--porcelain', '-uno'],
      capture_output=True,
      print_cmd=True).output
  if not modifications:
    raise PublishVettedAFDOArtifactsError(
        'AFDO info for the ebuilds did not change.')
  logging.info('Git status: %s', modifications)
  git_diff = git.RunGit(
      TOOLCHAIN_UTILS_PATH, ['diff'], capture_output=True,
      print_cmd=True).output
  logging.info('Git diff: %s', git_diff)
  # Commit the change
  git.RunGit(
      TOOLCHAIN_UTILS_PATH, ['commit', '-a', '-m', commit_message],
      print_cmd=True)
  # Push the change, this should create a CL, and auto-submit it.
  git.GitPush(
      TOOLCHAIN_UTILS_PATH,
      'HEAD',
      git.RemoteRef(TOOLCHAIN_UTILS_REPO, 'refs/for/master%submit'),
      print_cmd=True)


def _CreateReleaseChromeAFDO(cwp_afdo, arch, benchmark_afdo, output_dir):
  """Create an AFDO profile to be used in release Chrome.

  This means we want to merge the CWP and benchmark AFDO profiles into
  one, and redact all ICF symbols.

  Args:
    cwp_afdo: Name of the verified CWP profile.
    arch: Architecture name of the CWP profile.
    benchmark_afdo: Name of the verified benchmark profile.
    output_dir: A directory to store all artifacts.

  Returns:
    Full path to a generated release AFDO profile.
  """
  gs_context = gs.GSContext()
  # Download profiles from gsutil.
  cwp_full_name = cwp_afdo + XZ_COMPRESSION_SUFFIX
  benchmark_full_name = benchmark_afdo + BZ2_COMPRESSION_SUFFIX

  cwp_afdo_url = os.path.join(CWP_AFDO_GS_URL, arch, cwp_full_name)
  cwp_afdo_local = os.path.join(output_dir, cwp_full_name)
  gs_context.Copy(cwp_afdo_url, cwp_afdo_local)

  benchmark_afdo_url = os.path.join(BENCHMARK_AFDO_GS_URL, benchmark_full_name)
  benchmark_afdo_local = os.path.join(output_dir, benchmark_full_name)
  gs_context.Copy(benchmark_afdo_url, benchmark_afdo_local)

  # Decompress the files.
  cwp_local_uncompressed = os.path.join(output_dir, cwp_afdo)
  cros_build_lib.UncompressFile(cwp_afdo_local, cwp_local_uncompressed)
  benchmark_local_uncompressed = os.path.join(output_dir, benchmark_afdo)
  cros_build_lib.UncompressFile(benchmark_afdo_local,
                                benchmark_local_uncompressed)

  # Merge profiles.
  merge_weights = [(cwp_local_uncompressed, RELEASE_CWP_MERGE_WEIGHT),
                   (benchmark_local_uncompressed,
                    RELEASE_BENCHMARK_MERGE_WEIGHT)]
  merged_profile_name = MERGED_AFDO_NAME % (
      _GetCombinedAFDOName(_ParseCWPProfileName(cwp_afdo), arch,
                           _ParseBenchmarkProfileName(benchmark_afdo)))
  merged_profile = os.path.join(output_dir, merged_profile_name)
  _MergeAFDOProfiles(merge_weights, merged_profile)

  # Redact profiles.
  redacted_profile_name = merged_profile_name + '-redacted.afdo'
  redacted_profile = os.path.join(output_dir, redacted_profile_name)
  _RedactAFDOProfile(merged_profile, redacted_profile)

  return redacted_profile


def _UploadReleaseChromeAFDO():
  """Upload an AFDO profile to be used in release Chrome.

  Returns:
    Full path to the release AFDO profile uploaded.
  """
  with osutils.TempDir() as tempdir:
    # This path should be a full path to the profile
    profile_path = _GetArtifactVersionInEbuild('chromeos-chrome',
                                               'UNVETTED_AFDO_FILE')
    # Compress the final AFDO profile.
    ret = _CompressAFDOFiles([profile_path], None, tempdir,
                             XZ_COMPRESSION_SUFFIX)
    # Upload to GS bucket
    _UploadAFDOArtifactToGSBucket(RELEASE_AFDO_GS_URL_VETTED, ret[0])

    return profile_path


def UploadAndPublishVettedAFDOArtifacts(artifact_type, board):
  """Main function to upload a vetted AFDO artifact.

  As suggested by the name, this function first uploads the vetted artifact
  to GS bucket, and publishes the change to metadata file if needed.

  Args:
    artifact_type: Type of the artifact. 'Orderfile' or 'kernel_afdo'
    board: Name of the board.

  Returns:
    Status of the upload and publish.
  """

  uploaded = {}
  if artifact_type == 'chrome_afdo':
    # For current board, there is a release AFDO in chroot used for
    # verification.
    uploaded['chrome-afdo'] = _UploadReleaseChromeAFDO()
  elif artifact_type == 'kernel_afdo':
    kver = KERNEL_AFDO_VERIFIER_BOARDS[board]
    name = 'chromeos-kernel-' + kver.replace('.', '_')
    uploaded[name] = _UploadVettedAFDOArtifacts(artifact_type, kver)
    json_file = os.path.join(TOOLCHAIN_UTILS_PATH,
                             'afdo_metadata/kernel_afdo.json')
    title = 'afdo_metadata: Publish new profiles for kernel %s.' % kver
  else:
    uploaded['orderfile'] = _UploadVettedAFDOArtifacts('orderfile')

  if not [x for x in uploaded.values() if x]:
    # Nothing to publish to should be caught earlier
    return False

  if artifact_type == 'kernel_afdo':
    # No need to publish orderfile or chrome AFDO changes.
    # Skia autoroller can pick it up from uploads.
    _PublishVettedAFDOArtifacts(json_file, uploaded, title)
  return True
