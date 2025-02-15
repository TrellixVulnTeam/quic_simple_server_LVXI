# -*- coding: utf-8 -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for handling Chrome OS partition."""

from __future__ import print_function

import os
import tempfile

from chromite.lib import constants
from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import image_lib
from chromite.lib import path_util

from chromite.lib.paygen import filelib


DLC_IMAGE = 'dlc'
CROS_IMAGE = 'cros'


def ExtractPartition(filename, partition, out_part):
  """Extracts partition from an image file.

  Args:
    filename: The image file.
    partition: The partition name. e.g. ROOT or KERNEL.
    out_part: The output partition file.
  """
  parts = image_lib.GetImageDiskPartitionInfo(filename)
  part_info = [p for p in parts if p.name == partition][0]

  offset = int(part_info.start)
  length = int(part_info.size)

  filelib.CopyFileSegment(filename, 'rb', length, out_part, 'wb',
                          in_seek=offset)


def Ext2FileSystemSize(ext2_file):
  """Return the size of an ext2 filesystem in bytes.

  Args:
    ext2_file: The path to the ext2 file.
  """
  # dumpe2fs is normally installed in /sbin but doesn't require root.
  dump = cros_build_lib.run(['/sbin/dumpe2fs', '-h', ext2_file],
                            print_cmd=False, capture_output=True,
                            encoding='utf-8').stdout
  fs_blocks = 0
  fs_blocksize = 0
  for line in dump.split('\n'):
    if not line:
      continue
    label, data = line.split(':')[:2]
    if label == 'Block count':
      fs_blocks = int(data)
    elif label == 'Block size':
      fs_blocksize = int(data)

  return fs_blocks * fs_blocksize


def PatchKernel(image, kern_file):
  """Patches a kernel with vblock from a stateful partition.

  Args:
    image: The stateful partition image.
    kern_file: The kernel file.
  """

  with tempfile.NamedTemporaryFile(prefix='stateful') as state_out, \
       tempfile.NamedTemporaryFile(prefix='vmlinuz_hd.vblock') as vblock:
    ExtractPartition(image, constants.PART_STATE, state_out)
    cros_build_lib.run(
        ['e2cp', '%s:/vmlinuz_hd.vblock' % state_out, vblock])
    filelib.CopyFileSegment(
        vblock, 'rb', os.path.getsize(vblock), kern_file, 'r+b')


def ExtractKernel(image, kern_out):
  """Extracts the kernel from the given image.

  Args:
    image: The image containing the kernel partition.
    kern_out: The output kernel file.
  """
  ExtractPartition(image, constants.PART_KERN_B, kern_out)
  with open(kern_out, 'rb') as kern:
    if not any(kern.read(65536)):
      logging.warn('%s: Kernel B is empty, patching kernel A.', image)
      ExtractPartition(image, constants.PART_KERN_A, kern_out)
      PatchKernel(image, kern_out)


def ExtractRoot(image, root_out, truncate=True):
  """Extract the rootfs partition from a gpt image.

  Args:
    image: The input image file.
    root_out: The output root partition file.
    truncate: If true, truncate the partition to the file system size.
  """
  ExtractPartition(image, constants.PART_ROOT_A, root_out)

  if not truncate:
    return

  # We only update the filesystem part of the partition, which is stored in the
  # gpt script. So we need to truncated it to the file system size if asked for.
  root_out_size = Ext2FileSystemSize(root_out)
  if root_out_size:
    with open(root_out, 'ab') as root:
      root.truncate(root_out_size)
    logging.info('Truncated root to %d bytes.', root_out_size)
  else:
    raise IOError('Error truncating the rootfs to filesystem size.')


def IsSquashfsImage(image):
  """Returns true if the image is detected to be Squashfs."""
  try:
    # -s: Display file system superblock.
    cros_build_lib.run(
        ['unsquashfs', '-s', path_util.ToChrootPath(image)],
        stdout=True,
        enter_chroot=True)
    return True
  except cros_build_lib.RunCommandError:
    return False


def IsExt4Image(image):
  """Returns true if the image is detected to be ext2/ext3/ext4."""
  try:
    # -l: Listing the content of the superblock structure.
    cros_build_lib.sudo_run(
        ['tune2fs', '-l', path_util.ToChrootPath(image)], stdout=True,
        enter_chroot=True)
    return True
  except cros_build_lib.RunCommandError:
    return False


def IsGptImage(image):
  """Returns true if the image is a GPT image."""
  try:
    return bool(image_lib.GetImageDiskPartitionInfo(image))
  except cros_build_lib.RunCommandError:
    return False


def LookupImageType(image):
  """Returns the image type given the path to an image.

  Args:
    image: The path to a GPT or Squashfs Image.

  Returns:
    The type of the image. None if it cannot detect the image type.
  """
  if IsGptImage(image):
    return CROS_IMAGE
  elif IsSquashfsImage(image) or IsExt4Image(image):
    return DLC_IMAGE

  return None
