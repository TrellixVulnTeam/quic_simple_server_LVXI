
// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/soda/constants.h"

#include "base/files/file_enumerator.h"
#include "base/path_service.h"
#include "components/component_updater/component_updater_paths.h"

namespace soda {

constexpr base::FilePath::CharType kSodaInstallationRelativePath[] =
    FILE_PATH_LITERAL("SODA");

constexpr base::FilePath::CharType kSodaBinaryRelativePath[] =
    FILE_PATH_LITERAL("SODAFiles/libsoda.so");

constexpr base::FilePath::CharType kSodaConfigFileRelativePath[] =
    FILE_PATH_LITERAL("SODAFiles/en_us/dictation.ascii_proto");

const base::FilePath GetSodaDirectory() {
  base::FilePath components_dir;
  base::PathService::Get(component_updater::DIR_COMPONENT_USER,
                         &components_dir);

  base::FileEnumerator enumerator(
      components_dir.Append(kSodaInstallationRelativePath), false,
      base::FileEnumerator::DIRECTORIES);
  base::FilePath latest_version_dir;
  for (base::FilePath version_dir = enumerator.Next(); !version_dir.empty();
       version_dir = enumerator.Next()) {
    latest_version_dir =
        latest_version_dir < version_dir ? version_dir : latest_version_dir;
  }

  return latest_version_dir;
}

const base::FilePath GetSodaBinaryPath() {
  base::FilePath soda_dir = GetSodaDirectory();
  return soda_dir.empty() ? soda_dir : soda_dir.Append(kSodaBinaryRelativePath);
}

const base::FilePath GetSodaConfigPath() {
  base::FilePath soda_dir = GetSodaDirectory();
  return soda_dir.empty() ? soda_dir
                          : soda_dir.Append(kSodaConfigFileRelativePath);
}

}  // namespace soda
