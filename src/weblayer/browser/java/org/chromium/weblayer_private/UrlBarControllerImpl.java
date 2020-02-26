// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.weblayer_private;

import android.content.Context;
import android.os.Bundle;
import android.support.annotation.NonNull;
import android.util.TypedValue;
import android.widget.TextView;

import org.chromium.weblayer_private.interfaces.IObjectWrapper;
import org.chromium.weblayer_private.interfaces.IUrlBarController;
import org.chromium.weblayer_private.interfaces.ObjectWrapper;
import org.chromium.weblayer_private.interfaces.StrictModeWorkaround;

/**
 *  Implementation of {@link IUrlBarController}.
 */
public class UrlBarControllerImpl extends IUrlBarController.Stub {
    // To be kept in sync with the constants in UrlBarOptions.java
    public static final String URL_TEXT_SIZE = "UrlTextSize";
    public static final float DEFAULT_TEXT_SIZE = 10.0F;
    public static final float MINIMUM_TEXT_SIZE = 5.0F;

    private BrowserImpl mBrowserImpl;

    public UrlBarControllerImpl(BrowserImpl browserImpl) {
        mBrowserImpl = browserImpl;
    }

    void destroy() {
        mBrowserImpl = null;
    }

    @Override
    public IObjectWrapper /* View */ createUrlBarView(Bundle options) {
        StrictModeWorkaround.apply();
        if (mBrowserImpl == null) {
            throw new IllegalStateException("UrlBarView cannot be created without a valid Browser");
        }
        Context context = mBrowserImpl.getContext();
        if (context == null) throw new IllegalStateException("BrowserFragment not attached yet.");

        UrlBarView urlBarView = new UrlBarView(context, options);
        return ObjectWrapper.wrap(urlBarView);
    }

    private class UrlBarView extends TextView implements BrowserImpl.VisibleSecurityStateObserver {
        private float mTextSize;
        public UrlBarView(@NonNull Context context, Bundle options) {
            super(context);

            updateView();

            mTextSize = options.getFloat(URL_TEXT_SIZE, DEFAULT_TEXT_SIZE);
            setTextSize(TypedValue.COMPLEX_UNIT_SP, Math.max(MINIMUM_TEXT_SIZE, mTextSize));
        }

        // BrowserImpl.VisibleSecurityStateObserver
        @Override
        public void onVisibleSecurityStateOfActiveTabChanged() {
            updateView();
        }

        @Override
        protected void onAttachedToWindow() {
            if (mBrowserImpl != null) {
                mBrowserImpl.addVisibleSecurityStateObserver(this);
                updateView();
            }

            super.onAttachedToWindow();
        }

        @Override
        protected void onDetachedFromWindow() {
            if (mBrowserImpl != null) mBrowserImpl.removeVisibleSecurityStateObserver(this);
            super.onDetachedFromWindow();
        }

        private void updateView() {
            // TODO(crbug.com/1025607): Add a way to get a formatted URL based
            // on mOptions.
            if (mBrowserImpl == null) return;
            setText(mBrowserImpl.getActiveTab().getWebContents().getVisibleUrl());
        }
    }
}