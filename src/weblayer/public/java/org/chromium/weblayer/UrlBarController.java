// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.weblayer;

import android.os.RemoteException;
import android.view.View;

import org.chromium.weblayer_private.interfaces.APICallException;
import org.chromium.weblayer_private.interfaces.IUrlBarController;
import org.chromium.weblayer_private.interfaces.ObjectWrapper;

/**
 * UrlBarController enables creation of URL bar views and retrieval of information about them.
 */
public final class UrlBarController {
    private final IUrlBarController mImpl;

    UrlBarController(IUrlBarController urlBarController) {
        mImpl = urlBarController;
    }

    /**
     * Creates a URL bar view based on the options provided.
     * @param options The options provided to tweak the URL bar display.
     * @since 81
     */
    public View createUrlBarView(UrlBarOptions options) {
        ThreadCheck.ensureOnUiThread();
        if (WebLayer.getSupportedMajorVersionInternal() < 81) {
            throw new UnsupportedOperationException();
        }

        try {
            return ObjectWrapper.unwrap(mImpl.createUrlBarView(options.getBundle()), View.class);
        } catch (RemoteException exception) {
            throw new APICallException(exception);
        }
    }
}
