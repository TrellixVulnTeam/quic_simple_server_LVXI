// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Helper to call postMessage on the guest frame.
 *
 * @param {Object} message
 */
function postToGuestWindow(message) {
  const guest =
      /** @type{HTMLIFrameElement} */ (document.querySelector('iframe'));
  // The next line should probably be passing a transfer argument, but that
  // causes Chrome to send a "null" message. The transfer seems to work without
  // the third argument (but inefficiently, perhaps).
  guest.contentWindow.postMessage(message, 'chrome://media-app-guest');
}

/**
 * Loads a file in the guest.
 *
 * @param {File} file
 */
async function loadFile(file) {
  postToGuestWindow({'file': file});
}

/**
 * Loads a file from a handle received via the fileHandling API.
 *
 * @param {FileSystemHandle} handle
 */
async function loadFileFromHandle(handle) {
  if (!handle.isFile) {
    return;
  }

  const fileHandle = /** @type{FileSystemFileHandle} */ (handle);
  const file = await fileHandle.getFile();
  loadFile(file);
}

// Wait for 'load' (and not DOMContentLoaded) to ensure the subframe has been
// loaded and is ready to respond to postMessage.
window.addEventListener('load', () => {
  window.launchQueue.setConsumer(params => {
    if (!params || !params.files || params.files.length == 0) {
      return;
    }

    loadFileFromHandle(params.files[0]);
  });
});
