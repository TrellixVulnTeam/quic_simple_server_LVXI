// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "weblayer/browser/tab_impl.h"

#include "base/auto_reset.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "components/autofill/content/browser/content_autofill_driver_factory.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/autofill_provider.h"
#include "components/captive_portal/core/buildflags.h"
#include "components/find_in_page/find_tab_helper.h"
#include "components/find_in_page/find_types.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/renderer_preferences.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "weblayer/browser/autofill_client_impl.h"
#include "weblayer/browser/browser_impl.h"
#include "weblayer/browser/file_select_helper.h"
#include "weblayer/browser/i18n_util.h"
#include "weblayer/browser/isolated_world_ids.h"
#include "weblayer/browser/navigation_controller_impl.h"
#include "weblayer/browser/profile_impl.h"
#include "weblayer/browser/session_service.h"
#include "weblayer/public/download_delegate.h"
#include "weblayer/public/fullscreen_delegate.h"
#include "weblayer/public/new_tab_delegate.h"
#include "weblayer/public/tab_observer.h"

#if !defined(OS_ANDROID)
#include "ui/views/controls/webview/webview.h"
#endif

#if defined(OS_ANDROID)
#include "base/android/callback_android.h"
#include "base/android/jni_string.h"
#include "base/json/json_writer.h"
#include "base/trace_event/trace_event.h"
#include "components/autofill/android/autofill_provider_android.h"
#include "components/embedder_support/android/delegate/color_chooser_android.h"
#include "weblayer/browser/java/jni/TabImpl_jni.h"
#include "weblayer/browser/top_controls_container_view.h"
#endif

#if BUILDFLAG(ENABLE_CAPTIVE_PORTAL_DETECTION)
#include "components/captive_portal/content/captive_portal_tab_helper.h"
#include "weblayer/browser/captive_portal_service_factory.h"
#endif

#if defined(OS_ANDROID)
using base::android::AttachCurrentThread;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
#endif

namespace weblayer {

namespace {

#if defined(OS_ANDROID)
const base::Feature kImmediatelyHideBrowserControlsForTest{
    "ImmediatelyHideBrowserControlsForTest", base::FEATURE_DISABLED_BY_DEFAULT};

// The time that must elapse after a navigation before the browser controls can
// be hidden. This value matches what chrome has in
// TabStateBrowserControlsVisibilityDelegate.
base::TimeDelta GetBrowserControlsAllowHideDelay() {
  if (base::FeatureList::IsEnabled(kImmediatelyHideBrowserControlsForTest))
    return base::TimeDelta();

  return base::TimeDelta::FromSeconds(3);
}

bool g_system_autofill_disabled_for_testing = false;

#endif

NewTabType NewTabTypeFromWindowDisposition(WindowOpenDisposition disposition) {
  // WindowOpenDisposition has a *ton* of types, but the following are really
  // the only ones that should be hit for this code path.
  switch (disposition) {
    case WindowOpenDisposition::NEW_FOREGROUND_TAB:
      return NewTabType::kForeground;
    case WindowOpenDisposition::NEW_BACKGROUND_TAB:
      return NewTabType::kBackground;
    case WindowOpenDisposition::NEW_POPUP:
      return NewTabType::kNewPopup;
    case WindowOpenDisposition::NEW_WINDOW:
      return NewTabType::kNewWindow;
    default:
      // The set of allowed types are in
      // ContentTabClientImpl::CanCreateWindow().
      NOTREACHED();
      return NewTabType::kForeground;
  }
}

#if BUILDFLAG(ENABLE_CAPTIVE_PORTAL_DETECTION)
// Opens a captive portal login page in |web_contents|.
void OpenCaptivePortalLoginTabInWebContents(
    content::WebContents* web_contents) {
  // In Chrome this opens in a new tab, but WebLayer's TabImpl has no support
  // for opening new tabs (its OpenURLFromTab() method DCHECKs if the
  // disposition is not |CURRENT_TAB|).
  // TODO(crbug.com/1047130): Revisit if TabImpl gets support for opening URLs
  // in new tabs.
  content::OpenURLParams params(
      CaptivePortalServiceFactory::GetForBrowserContext(
          web_contents->GetBrowserContext())
          ->test_url(),
      content::Referrer(), WindowOpenDisposition::CURRENT_TAB,
      ui::PAGE_TRANSITION_LINK, false);
  web_contents->OpenURL(params);
}
#endif

// Pointer value of this is used as a key in base::SupportsUserData for
// WebContents. Value of the key is an instance of |UserData|.
constexpr int kWebContentsUserDataKey = 0;

struct UserData : public base::SupportsUserData::Data {
  TabImpl* controller = nullptr;
};

#if defined(OS_ANDROID)
Tab* g_last_tab;

void HandleJavaScriptResult(
    const base::android::ScopedJavaGlobalRef<jobject>& callback,
    base::Value result) {
  std::string json;
  base::JSONWriter::Write(result, &json);
  base::android::RunStringCallbackAndroid(callback, json);
}
#endif

}  // namespace

#if defined(OS_ANDROID)
TabImpl::TabImpl(ProfileImpl* profile, const JavaParamRef<jobject>& java_impl)
    : TabImpl(profile) {
  java_impl_ = java_impl;
}
#endif

TabImpl::TabImpl(ProfileImpl* profile,
                 std::unique_ptr<content::WebContents> web_contents)
    : profile_(profile), web_contents_(std::move(web_contents)) {
#if defined(OS_ANDROID)
  g_last_tab = this;
#endif
  if (web_contents_) {
    // This code path is hit when the page requests a new tab, which should
    // only be possible from the same profile.
    DCHECK_EQ(profile_->GetBrowserContext(),
              web_contents_->GetBrowserContext());
  } else {
    content::WebContents::CreateParams create_params(
        profile_->GetBrowserContext());
    web_contents_ = content::WebContents::Create(create_params);
  }

  UpdateRendererPrefs(false);
  locale_change_subscription_ =
      i18n::RegisterLocaleChangeCallback(base::BindRepeating(
          &TabImpl::UpdateRendererPrefs, base::Unretained(this), true));

  std::unique_ptr<UserData> user_data = std::make_unique<UserData>();
  user_data->controller = this;
  web_contents_->SetUserData(&kWebContentsUserDataKey, std::move(user_data));

  web_contents_->SetDelegate(this);
  Observe(web_contents_.get());

  navigation_controller_ = std::make_unique<NavigationControllerImpl>(this);

  find_in_page::FindTabHelper::CreateForWebContents(web_contents_.get());
  GetFindTabHelper()->AddObserver(this);

  sessions::SessionTabHelper::CreateForWebContents(
      web_contents_.get(),
      base::BindRepeating(&TabImpl::GetSessionServiceTabHelperDelegate,
                          base::Unretained(this)));

#if BUILDFLAG(ENABLE_CAPTIVE_PORTAL_DETECTION)
  CaptivePortalTabHelper::CreateForWebContents(
      web_contents_.get(),
      CaptivePortalServiceFactory::GetForBrowserContext(
          web_contents_->GetBrowserContext()),
      base::BindRepeating(&OpenCaptivePortalLoginTabInWebContents,
                          web_contents_.get()));
#endif
}

TabImpl::~TabImpl() {
  DCHECK(!browser_);

  GetFindTabHelper()->RemoveObserver(this);

  // Destruct WebContents now to avoid it calling back when this object is
  // partially destructed. DidFinishNavigation can be called while destroying
  // WebContents, so stop observing first.
  Observe(nullptr);
  web_contents_.reset();
}

// static
TabImpl* TabImpl::FromWebContents(content::WebContents* web_contents) {
  return reinterpret_cast<UserData*>(
             web_contents->GetUserData(&kWebContentsUserDataKey))
      ->controller;
}

void TabImpl::SetDownloadDelegate(DownloadDelegate* delegate) {
  download_delegate_ = delegate;
}

void TabImpl::SetErrorPageDelegate(ErrorPageDelegate* delegate) {
  error_page_delegate_ = delegate;
}

void TabImpl::SetFullscreenDelegate(FullscreenDelegate* delegate) {
  if (delegate == fullscreen_delegate_)
    return;

  const bool had_delegate = (fullscreen_delegate_ != nullptr);
  const bool has_delegate = (delegate != nullptr);

  // If currently fullscreen, and the delegate is being set to null, force an
  // exit now (otherwise the delegate can't take us out of fullscreen).
  if (is_fullscreen_ && fullscreen_delegate_ && had_delegate != has_delegate)
    OnExitFullscreen();

  fullscreen_delegate_ = delegate;
  // Whether fullscreen is enabled depends upon whether there is a delegate. If
  // having a delegate changed, then update the renderer (which is where
  // fullscreen enabled is tracked).
  content::RenderViewHost* host = web_contents_->GetRenderViewHost();
  if (had_delegate != has_delegate && host)
    host->OnWebkitPreferencesChanged();
}

void TabImpl::SetNewTabDelegate(NewTabDelegate* delegate) {
  new_tab_delegate_ = delegate;
}

void TabImpl::AddObserver(TabObserver* observer) {
  observers_.AddObserver(observer);
}

void TabImpl::RemoveObserver(TabObserver* observer) {
  observers_.RemoveObserver(observer);
}

NavigationController* TabImpl::GetNavigationController() {
  return navigation_controller_.get();
}

void TabImpl::ExecuteScript(const base::string16& script,
                            bool use_separate_isolate,
                            JavaScriptResultCallback callback) {
  if (use_separate_isolate) {
    web_contents_->GetMainFrame()->ExecuteJavaScriptInIsolatedWorld(
        script, std::move(callback), ISOLATED_WORLD_ID_WEBLAYER);
  } else {
    content::RenderFrameHost::AllowInjectingJavaScript();
    web_contents_->GetMainFrame()->ExecuteJavaScript(script,
                                                     std::move(callback));
  }
}

void TabImpl::ExecuteScriptWithUserGestureForTests(
    const base::string16& script) {
  web_contents_->GetMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
      script);
}

#if !defined(OS_ANDROID)
void TabImpl::AttachToView(views::WebView* web_view) {
  web_view->SetWebContents(web_contents_.get());
  web_contents_->Focus();
}
#endif

#if defined(OS_ANDROID)
// static
void TabImpl::DisableAutofillSystemIntegrationForTesting() {
  g_system_autofill_disabled_for_testing = true;
}

static jlong JNI_TabImpl_CreateTab(JNIEnv* env,
                                   jlong profile,
                                   const JavaParamRef<jobject>& java_impl) {
  return reinterpret_cast<intptr_t>(
      new TabImpl(reinterpret_cast<ProfileImpl*>(profile), java_impl));
}

static void JNI_TabImpl_DeleteTab(JNIEnv* env, jlong tab) {
  std::unique_ptr<Tab> owned_tab;
  TabImpl* tab_impl = reinterpret_cast<TabImpl*>(tab);
  DCHECK(tab_impl);
  if (tab_impl->browser())
    owned_tab = tab_impl->browser()->RemoveTab(tab_impl);
  else
    owned_tab.reset(tab_impl);
}

ScopedJavaLocalRef<jobject> TabImpl::GetWebContents(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  return web_contents_->GetJavaWebContents();
}

void TabImpl::SetTopControlsContainerView(
    JNIEnv* env,
    const JavaParamRef<jobject>& caller,
    jlong native_top_controls_container_view) {
  top_controls_container_view_ = reinterpret_cast<TopControlsContainerView*>(
      native_top_controls_container_view);
}

void TabImpl::ExecuteScript(JNIEnv* env,
                            const JavaParamRef<jstring>& script,
                            bool use_separate_isolate,
                            const JavaParamRef<jobject>& callback) {
  base::android::ScopedJavaGlobalRef<jobject> jcallback(env, callback);
  ExecuteScript(base::android::ConvertJavaStringToUTF16(script),
                use_separate_isolate,
                base::BindOnce(&HandleJavaScriptResult, jcallback));
}

void TabImpl::SetJavaImpl(JNIEnv* env, const JavaParamRef<jobject>& impl) {
  // This should only be called early on and only once.
  DCHECK(!java_impl_);
  java_impl_ = impl;
}

void TabImpl::OnAutofillProviderChanged(
    JNIEnv* env,
    const JavaParamRef<jobject>& autofill_provider) {
  if (g_system_autofill_disabled_for_testing)
    return;

  if (!autofill_provider_) {
    // The first invocation should be when instantiating the autofill
    // infrastructure, at which point the Java-side object should not be null.
    DCHECK(autofill_provider);

    // Initialize the native side of the autofill infrastructure.
    autofill_provider_ = std::make_unique<autofill::AutofillProviderAndroid>(
        autofill_provider, web_contents_.get());
    InitializeAutofill();
    return;
  }

  // The AutofillProvider Java object has been changed; inform
  // |autofill_provider_|.
  auto* provider =
      static_cast<autofill::AutofillProviderAndroid*>(autofill_provider_.get());
  provider->OnJavaAutofillProviderChanged(env, autofill_provider);
}
#endif

content::WebContents* TabImpl::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params) {
  if (params.disposition != WindowOpenDisposition::CURRENT_TAB) {
    NOTIMPLEMENTED();
    return nullptr;
  }

  source->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(params));
  return source;
}

void TabImpl::DidNavigateMainFramePostCommit(
    content::WebContents* web_contents) {
  for (auto& observer : observers_)
    observer.DisplayedUrlChanged(web_contents->GetVisibleURL());
}

content::ColorChooser* TabImpl::OpenColorChooser(
    content::WebContents* web_contents,
    SkColor color,
    const std::vector<blink::mojom::ColorSuggestionPtr>& suggestions) {
#if defined(OS_ANDROID)
  return new web_contents_delegate_android::ColorChooserAndroid(
      web_contents, color, suggestions);
#else
  return nullptr;
#endif
}

void TabImpl::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    std::unique_ptr<content::FileSelectListener> listener,
    const blink::mojom::FileChooserParams& params) {
  FileSelectHelper::RunFileChooser(render_frame_host, std::move(listener),
                                   params);
}

int TabImpl::GetTopControlsHeight() {
#if defined(OS_ANDROID)
  return top_controls_container_view_
             ? top_controls_container_view_->GetTopControlsHeight()
             : 0;
#else
  return 0;
#endif
}

bool TabImpl::DoBrowserControlsShrinkRendererSize(
    const content::WebContents* web_contents) {
#if defined(OS_ANDROID)
  TRACE_EVENT0("weblayer", "Java_TabImpl_doBrowserControlsShrinkRendererSize");
  return Java_TabImpl_doBrowserControlsShrinkRendererSize(AttachCurrentThread(),
                                                          java_impl_);
#else
  return false;
#endif
}

bool TabImpl::EmbedsFullscreenWidget() {
  return true;
}

void TabImpl::EnterFullscreenModeForTab(
    content::WebContents* web_contents,
    const GURL& origin,
    const blink::mojom::FullscreenOptions& options) {
  // TODO: support |options|.
  is_fullscreen_ = true;
  auto exit_fullscreen_closure = base::BindOnce(&TabImpl::OnExitFullscreen,
                                                weak_ptr_factory_.GetWeakPtr());
  base::AutoReset<bool> reset(&processing_enter_fullscreen_, true);
  fullscreen_delegate_->EnterFullscreen(std::move(exit_fullscreen_closure));
#if defined(OS_ANDROID)
  // Make sure browser controls cannot show when the tab is fullscreen.
  UpdateBrowserControlsState(content::BROWSER_CONTROLS_STATE_HIDDEN,
                             content::BROWSER_CONTROLS_STATE_BOTH, false);
#endif
}

void TabImpl::ExitFullscreenModeForTab(content::WebContents* web_contents) {
  is_fullscreen_ = false;
  fullscreen_delegate_->ExitFullscreen();
#if defined(OS_ANDROID)
  // Attempt to show browser controls when exiting fullscreen.
  UpdateBrowserControlsState(content::BROWSER_CONTROLS_STATE_BOTH,
                             content::BROWSER_CONTROLS_STATE_SHOWN, true);
#endif
}

bool TabImpl::IsFullscreenForTabOrPending(
    const content::WebContents* web_contents) {
  return is_fullscreen_;
}

blink::mojom::DisplayMode TabImpl::GetDisplayMode(
    const content::WebContents* web_contents) {
  return is_fullscreen_ ? blink::mojom::DisplayMode::kFullscreen
                        : blink::mojom::DisplayMode::kBrowser;
}

void TabImpl::AddNewContents(content::WebContents* source,
                             std::unique_ptr<content::WebContents> new_contents,
                             WindowOpenDisposition disposition,
                             const gfx::Rect& initial_rect,
                             bool user_gesture,
                             bool* was_blocked) {
  if (!new_tab_delegate_)
    return;

  std::unique_ptr<Tab> tab =
      std::make_unique<TabImpl>(profile_, std::move(new_contents));
  new_tab_delegate_->OnNewTab(std::move(tab),
                              NewTabTypeFromWindowDisposition(disposition));
}

void TabImpl::CloseContents(content::WebContents* source) {
  if (new_tab_delegate_)
    new_tab_delegate_->CloseTab();
}

void TabImpl::FindReply(content::WebContents* web_contents,
                        int request_id,
                        int number_of_matches,
                        const gfx::Rect& selection_rect,
                        int active_match_ordinal,
                        bool final_update) {
  GetFindTabHelper()->HandleFindReply(request_id, number_of_matches,
                                      selection_rect, active_match_ordinal,
                                      final_update);
}

#if defined(OS_ANDROID)
// FindMatchRectsReply and OnFindResultAvailable forward find-related results to
// the Java TabImpl. The find actions themselves are initiated directly from
// Java via FindInPageBridge.
void TabImpl::FindMatchRectsReply(content::WebContents* web_contents,
                                  int version,
                                  const std::vector<gfx::RectF>& rects,
                                  const gfx::RectF& active_rect) {
  JNIEnv* env = AttachCurrentThread();
  // Create the details object.
  ScopedJavaLocalRef<jobject> details_object =
      Java_TabImpl_createFindMatchRectsDetails(
          env, version, rects.size(),
          ScopedJavaLocalRef<jobject>(Java_TabImpl_createRectF(
              env, active_rect.x(), active_rect.y(), active_rect.right(),
              active_rect.bottom())));

  // Add the rects.
  for (size_t i = 0; i < rects.size(); ++i) {
    const gfx::RectF& rect = rects[i];
    Java_TabImpl_setMatchRectByIndex(
        env, details_object, i,
        ScopedJavaLocalRef<jobject>(Java_TabImpl_createRectF(
            env, rect.x(), rect.y(), rect.right(), rect.bottom())));
  }

  Java_TabImpl_onFindMatchRectsAvailable(env, java_impl_, details_object);
}
#endif

void TabImpl::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
#if defined(OS_ANDROID)
  if (navigation_handle->IsInMainFrame() &&
      !navigation_handle->IsSameDocument()) {
    // Force the browser controls to show initially, then allow hiding after a
    // short delay.
    UpdateBrowserControlsState(content::BROWSER_CONTROLS_STATE_SHOWN,
                               content::BROWSER_CONTROLS_STATE_BOTH, true);
    update_browser_controls_state_timer_.Start(
        FROM_HERE, GetBrowserControlsAllowHideDelay(),
        base::BindOnce(&TabImpl::UpdateBrowserControlsState,
                       base::Unretained(this),
                       content::BROWSER_CONTROLS_STATE_BOTH,
                       content::BROWSER_CONTROLS_STATE_BOTH, true));
  }
#endif
}

void TabImpl::RenderProcessGone(base::TerminationStatus status) {
  for (auto& observer : observers_)
    observer.OnRenderProcessGone();
}

void TabImpl::OnFindResultAvailable(content::WebContents* web_contents) {
#if defined(OS_ANDROID)
  const find_in_page::FindNotificationDetails& find_result =
      GetFindTabHelper()->find_result();
  JNIEnv* env = AttachCurrentThread();
  Java_TabImpl_onFindResultAvailable(
      env, java_impl_, find_result.number_of_matches(),
      find_result.active_match_ordinal(), find_result.final_update());
#endif
}

void TabImpl::DidChangeVisibleSecurityState() {
  if (browser_) {
    if (browser_->GetActiveTab() == this)
      browser_->VisibleSecurityStateOfActiveTabChanged();
  }
}

void TabImpl::OnExitFullscreen() {
  // If |processing_enter_fullscreen_| is true, it means the callback is being
  // called while processing EnterFullscreenModeForTab(). WebContents doesn't
  // deal well with this. FATAL as Android generally doesn't run with DCHECKs.
  LOG_IF(FATAL, processing_enter_fullscreen_)
      << "exiting fullscreen while entering fullscreen is not supported";
  web_contents_->ExitFullscreen(/* will_cause_resize */ false);
}

void TabImpl::UpdateRendererPrefs(bool should_sync_prefs) {
  web_contents_->GetMutableRendererPrefs()->accept_languages =
      i18n::GetAcceptLangs();
  if (should_sync_prefs)
    web_contents_->SyncRendererPrefs();
}

#if defined(OS_ANDROID)
void TabImpl::UpdateBrowserControlsState(
    content::BrowserControlsState constraints,
    content::BrowserControlsState current,
    bool animate) {
  // Cancel the timer since the state was set explicitly.
  update_browser_controls_state_timer_.Stop();
  web_contents_->GetMainFrame()->UpdateBrowserControlsState(constraints,
                                                            current, animate);

  if (web_contents_->ShowingInterstitialPage()) {
    web_contents_->GetInterstitialPage()
        ->GetMainFrame()
        ->UpdateBrowserControlsState(constraints, current, animate);
  }
}
#endif

std::unique_ptr<Tab> Tab::Create(Profile* profile) {
  return std::make_unique<TabImpl>(static_cast<ProfileImpl*>(profile));
}

#if defined(OS_ANDROID)
Tab* Tab::GetLastTabForTesting() {
  return g_last_tab;
}
#endif

void TabImpl::InitializeAutofillForTests(
    std::unique_ptr<autofill::AutofillProvider> provider) {
  DCHECK(!autofill_provider_);

  autofill_provider_ = std::move(provider);
  InitializeAutofill();
}

void TabImpl::InitializeAutofill() {
  DCHECK(autofill_provider_);

  content::WebContents* web_contents = web_contents_.get();
  DCHECK(
      !autofill::ContentAutofillDriverFactory::FromWebContents(web_contents));

  AutofillClientImpl::CreateForWebContents(web_contents);
  autofill::ContentAutofillDriverFactory::CreateForWebContentsAndDelegate(
      web_contents, AutofillClientImpl::FromWebContents(web_contents),
      i18n::GetApplicationLocale(),
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER,
      autofill_provider_.get());
}

find_in_page::FindTabHelper* TabImpl::GetFindTabHelper() {
  return find_in_page::FindTabHelper::FromWebContents(web_contents_.get());
}

sessions::SessionTabHelperDelegate* TabImpl::GetSessionServiceTabHelperDelegate(
    content::WebContents* web_contents) {
  DCHECK_EQ(web_contents, web_contents_.get());
  return browser_ ? browser_->session_service() : nullptr;
}

}  // namespace weblayer
