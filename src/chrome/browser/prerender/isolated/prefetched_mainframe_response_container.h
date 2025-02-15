// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_ISOLATED_PREFETCHED_MAINFRAME_RESPONSE_CONTAINER_H_
#define CHROME_BROWSER_PRERENDER_ISOLATED_PREFETCHED_MAINFRAME_RESPONSE_CONTAINER_H_

#include <memory>
#include <string>

#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

// This class encapsulates a whole HTTP response which can be used for caching
// and later replaying a prefetched request.
class PrefetchedMainframeResponseContainer {
 public:
  PrefetchedMainframeResponseContainer(network::mojom::URLResponseHeadPtr head,
                                       std::unique_ptr<std::string> body);
  ~PrefetchedMainframeResponseContainer();

  // Takes ownership of the response head.
  network::mojom::URLResponseHeadPtr TakeHead();

  // Take ownership of the response body.
  std::unique_ptr<std::string> TakeBody();

 private:
  network::mojom::URLResponseHeadPtr head_;
  std::unique_ptr<std::string> body_;

  PrefetchedMainframeResponseContainer(
      const PrefetchedMainframeResponseContainer&) = delete;
  PrefetchedMainframeResponseContainer& operator=(
      const PrefetchedMainframeResponseContainer&) = delete;
};

#endif  // CHROME_BROWSER_PRERENDER_ISOLATED_PREFETCHED_MAINFRAME_RESPONSE_CONTAINER_H_
