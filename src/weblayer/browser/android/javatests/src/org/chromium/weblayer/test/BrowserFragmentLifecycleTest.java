// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.weblayer.test;

import android.net.Uri;
import android.os.Bundle;
import android.support.test.filters.SmallTest;
import android.support.v4.app.FragmentManager;

import androidx.annotation.NonNull;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.weblayer.Navigation;
import org.chromium.weblayer.NavigationCallback;
import org.chromium.weblayer.NavigationController;
import org.chromium.weblayer.Tab;
import org.chromium.weblayer.shell.InstrumentationActivity;

import java.util.concurrent.CountDownLatch;

/**
 * Tests that fragment lifecycle works as expected.
 */
@RunWith(WebLayerJUnit4ClassRunner.class)
public class BrowserFragmentLifecycleTest {
    @Rule
    public InstrumentationActivityTestRule mActivityTestRule =
            new InstrumentationActivityTestRule();

    @Test
    @SmallTest
    public void successfullyLoadsUrlAfterRecreation() {
        InstrumentationActivity activity = mActivityTestRule.launchShellWithUrl("about:blank");
        Tab tab = TestThreadUtils.runOnUiThreadBlockingNoException(() -> activity.getTab());

        String url = "data:text,foo";
        mActivityTestRule.navigateAndWait(tab, url, false);

        mActivityTestRule.recreateActivity();

        InstrumentationActivity newActivity = mActivityTestRule.getActivity();
        tab = TestThreadUtils.runOnUiThreadBlockingNoException(() -> newActivity.getTab());
        url = "data:text,bar";
        mActivityTestRule.navigateAndWait(tab, url, false);
    }

    @Test
    @SmallTest
    public void restoreAfterRecreate() throws InterruptedException {
        InstrumentationActivity activity = mActivityTestRule.launchShellWithUrl("about:blank");
        Tab tab = TestThreadUtils.runOnUiThreadBlockingNoException(() -> activity.getTab());

        String url = "data:text,foo";
        mActivityTestRule.navigateAndWait(tab, url, false);

        mActivityTestRule.recreateActivity();

        InstrumentationActivity newActivity = mActivityTestRule.getActivity();
        CountDownLatch latch = new CountDownLatch(1);
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            Tab restoredTab = newActivity.getTab();
            // It's possible the NavigationController hasn't loaded yet, handle either scenario.
            NavigationController navigationController = restoredTab.getNavigationController();
            if (navigationController.getNavigationListSize() == 1
                    && navigationController.getNavigationEntryDisplayUri(0).equals(
                            Uri.parse(url))) {
                latch.countDown();
                return;
            }
            navigationController.registerNavigationCallback(new NavigationCallback() {
                @Override
                public void onNavigationCompleted(@NonNull Navigation navigation) {
                    if (navigation.getUri().equals(Uri.parse(url))) {
                        latch.countDown();
                    }
                }
            });
        });
        latch.await();
    }

    // https://crbug.com/1021041
    @Test
    @SmallTest
    public void handlesFragmentDestroyWhileNavigating() throws InterruptedException {
        CountDownLatch latch = new CountDownLatch(1);
        InstrumentationActivity activity = mActivityTestRule.launchShellWithUrl("about:blank");
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            NavigationController navigationController = activity.getTab().getNavigationController();
            navigationController.registerNavigationCallback(new NavigationCallback() {
                @Override
                public void onReadyToCommitNavigation(@NonNull Navigation navigation) {
                    FragmentManager fm = activity.getSupportFragmentManager();
                    fm.beginTransaction()
                            .remove(fm.getFragments().get(0))
                            .runOnCommit(latch::countDown)
                            .commit();
                }
            });
            navigationController.navigate(Uri.parse("data:text,foo"));
        });
        latch.await();
    }

    private void restoresPreviousSession(Bundle extras) throws InterruptedException {
        extras.putString(InstrumentationActivity.EXTRA_PERSISTENCE_ID, "x");
        final String url = mActivityTestRule.getTestDataURL("simple_page.html");
        InstrumentationActivity activity = mActivityTestRule.launchShellWithUrl(url, extras);

        mActivityTestRule.recreateActivity();

        InstrumentationActivity newActivity = mActivityTestRule.getActivity();
        Tab tab = TestThreadUtils.runOnUiThreadBlockingNoException(() -> newActivity.getTab());
        Assert.assertNotNull(tab);
        CountDownLatch latch = new CountDownLatch(1);
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            // It's possible the NavigationController hasn't loaded yet, handle either scenario.
            NavigationController navigationController = tab.getNavigationController();
            if (navigationController.getNavigationListSize() == 1
                    && navigationController.getNavigationEntryDisplayUri(0).equals(
                            Uri.parse(url))) {
                latch.countDown();
                return;
            }
            navigationController.registerNavigationCallback(new NavigationCallback() {
                @Override
                public void onNavigationCompleted(@NonNull Navigation navigation) {
                    if (navigation.getUri().equals(Uri.parse(url))) {
                        latch.countDown();
                    }
                }
            });
        });
        latch.await();
    }

    @Test
    @SmallTest
    public void restoresPreviousSession() throws InterruptedException {
        restoresPreviousSession(new Bundle());
    }

    @Test
    @SmallTest
    public void restoresPreviousSessionIncognito() throws InterruptedException {
        Bundle extras = new Bundle();
        // This forces incognito.
        extras.putString(InstrumentationActivity.EXTRA_PROFILE_NAME, null);
        restoresPreviousSession(extras);
    }
}
