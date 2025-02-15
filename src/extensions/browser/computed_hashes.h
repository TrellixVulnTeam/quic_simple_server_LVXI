// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_COMPUTED_HASHES_H_
#define EXTENSIONS_BROWSER_COMPUTED_HASHES_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/optional.h"

namespace extensions {

using IsCancelledCallback = base::RepeatingCallback<bool(void)>;
using ShouldComputeHashesCallback =
    base::RepeatingCallback<bool(const base::FilePath& relative_path)>;

// A class for storage and serialization of a set of SHA256 block hashes
// computed over the files inside an extension.
class ComputedHashes {
 public:
  // Hashes data for relative paths.
  // System specific path canonicalization is taken care of inside this class.
  class Data {
   public:
    struct HashInfo {
      int block_size;
      std::vector<std::string> hashes;
      // The relative unix style path.
      // Note that we use canonicalized paths as keys to HashInfo's container
      // |items_|.
      //
      // TODO(http://crbug.com/796395#c28): Consider removing this once
      // ContentVerifier::ShouldVerifyAnyPaths works with canonicalized relative
      // paths.
      base::FilePath relative_unix_path;
      HashInfo(int block_size,
               std::vector<std::string> hashes,
               base::FilePath relative_unix_path);
      ~HashInfo();

      HashInfo(const HashInfo&) = delete;
      HashInfo& operator=(const HashInfo&) = delete;
      HashInfo(HashInfo&&);
      HashInfo& operator=(HashInfo&&);
    };
    using Items = std::map<base::FilePath::StringType, HashInfo>;

    Data();
    ~Data();

    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    Data(Data&&);
    Data& operator=(Data&&);

    // For |relative_path|, adds hash information with |block_size| and
    // |hashes|.
    // Note that |relative_path| will be canonicalized.
    void Add(const base::FilePath& relative_path,
             int block_size,
             std::vector<std::string> hashes);

    // Removes the item that corresponds to |relative_path|.
    void Remove(const base::FilePath& relative_path);

    // Returns HashInfo* for |relative_path| or nullptr if not found.
    const HashInfo* GetItem(const base::FilePath& relative_path) const;

    const Items& items() const;

   private:
    // All items, stored by canonicalized FilePath::StringType key.
    Items items_;
  };

  explicit ComputedHashes(Data&& data);
  ComputedHashes(const ComputedHashes&) = delete;
  ComputedHashes& operator=(const ComputedHashes&) = delete;
  ComputedHashes(ComputedHashes&&);
  ComputedHashes& operator=(ComputedHashes&&);
  ~ComputedHashes();

  // Reads computed hashes from the computed_hashes.json file, returns nullopt
  // upon any failure.
  static base::Optional<ComputedHashes> CreateFromFile(
      const base::FilePath& path);

  // Computes hashes for files in |extension_root|. Returns nullopt upon any
  // failure. Callback |should_compute_hashes_for| is used to determine whether
  // we need hashes for a resource or not.
  // TODO(https://crbug.com/796395#c24) To support per-file block size instead
  // of passing |block_size| as an argument make callback
  // |should_compute_hashes_for| return optional<int>: nullopt if hashes are not
  // needed for this file, block size for this file otherwise.
  static base::Optional<ComputedHashes::Data> Compute(
      const base::FilePath& extension_root,
      int block_size,
      const IsCancelledCallback& is_cancelled,
      const ShouldComputeHashesCallback& should_compute_hashes_for_resource);

  // Saves computed hashes to given file, returns false upon any failure (and
  // true on success).
  bool WriteToFile(const base::FilePath& path) const;

  // Gets hash info for |relative_path|. The block size and hashes for
  // |relative_path| will be copied into the out parameters. Returns false if
  // resource was not found (and true on success).
  bool GetHashes(const base::FilePath& relative_path,
                 int* block_size,
                 std::vector<std::string>* hashes) const;

  // Returns the SHA256 hash of each |block_size| chunk in |contents|.
  static std::vector<std::string> GetHashesForContent(
      const std::string& contents,
      size_t block_size);

 private:
  // Builds hashes for one resource and checks them against
  // verified_contents.json if needed. Returns nullopt if nothing should be
  // added to computed_hashes.json for this resource.
  static base::Optional<std::vector<std::string>> ComputeAndCheckResourceHash(
      const base::FilePath& full_path,
      int block_size);

  Data data_;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_COMPUTED_HASHES_H_
