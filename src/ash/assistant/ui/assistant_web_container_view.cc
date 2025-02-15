// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/assistant/ui/assistant_web_container_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "ash/assistant/model/assistant_ui_model.h"
#include "ash/assistant/ui/assistant_ui_constants.h"
#include "ash/assistant/ui/assistant_view_delegate.h"
#include "ash/assistant/ui/assistant_web_view.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/window/caption_button_layout_constants.h"

namespace ash {

namespace {

constexpr int kPreferredWindowWidthDip = 768;

// This height includes the window's |non_client_frame_view|'s height.
constexpr int kPreferredWindowHeightDip = 768;

// The minimum padding of the window to the edges of the screen.
constexpr int kPreferredPaddingMinDip = 48;

}  // namespace

AssistantWebContainerView::AssistantWebContainerView(
    AssistantViewDelegate* assistant_view_delegate,
    AssistantWebViewDelegate* web_container_view_delegate)
    : assistant_view_delegate_(assistant_view_delegate),
      web_container_view_delegate_(web_container_view_delegate) {
  InitLayout();
}

AssistantWebContainerView::~AssistantWebContainerView() = default;

const char* AssistantWebContainerView::GetClassName() const {
  return "AssistantWebContainerView";
}

gfx::Size AssistantWebContainerView::CalculatePreferredSize() const {
  // TODO(b/142565300): Handle virtual keyboard resize.
  const gfx::Rect work_area =
      display::Screen::GetScreen()
          ->GetDisplayNearestWindow(GetWidget()->GetNativeWindow())
          .work_area();

  const int width = std::min(work_area.width() - 2 * kPreferredPaddingMinDip,
                             kPreferredWindowWidthDip);
  const int height = std::min(work_area.height() - 2 * kPreferredPaddingMinDip,
                              kPreferredWindowHeightDip);
  const int non_client_frame_view_height =
      views::GetCaptionButtonLayoutSize(
          views::CaptionButtonLayoutSize::kNonBrowserCaption)
          .height();
  return gfx::Size(width, height - non_client_frame_view_height);
}

bool AssistantWebContainerView::GoBack() {
  return assistant_web_view_->GoBack();
}

void AssistantWebContainerView::OpenUrl(const GURL& url) {
  assistant_web_view_->OpenUrl(url);
}

void AssistantWebContainerView::InitLayout() {
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_WINDOW;
  params.delegate = this;
  params.name = GetClassName();

  views::Widget* widget = new views::Widget;
  widget->Init(std::move(params));

  SetLayoutManager(std::make_unique<views::FillLayout>());
  SetBackground(views::CreateSolidBackground(SK_ColorWHITE));

  // Web view.
  assistant_web_view_ = AddChildView(std::make_unique<AssistantWebView>(
      assistant_view_delegate_, web_container_view_delegate_));
}

}  // namespace ash
