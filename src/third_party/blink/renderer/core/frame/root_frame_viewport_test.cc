// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/root_frame_viewport.h"

#include "base/single_thread_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/scheduler/test/renderer_scheduler_test_support.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/scroll/scroll_alignment.h"
#include "third_party/blink/renderer/core/scroll/scroll_into_view_params_type_converters.h"
#include "third_party/blink/renderer/core/scroll/scroll_types.h"
#include "third_party/blink/renderer/core/scroll/scrollable_area.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme_overlay_mock.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/platform/geometry/double_rect.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread_scheduler.h"

namespace blink {

class ScrollableAreaStub : public GarbageCollected<ScrollableAreaStub>,
                           public ScrollableArea {
  USING_GARBAGE_COLLECTED_MIXIN(ScrollableAreaStub);

 public:
  ScrollableAreaStub(const IntSize& viewport_size, const IntSize& contents_size)
      : user_input_scrollable_x_(true),
        user_input_scrollable_y_(true),
        viewport_size_(viewport_size),
        contents_size_(contents_size),
        timer_task_runner_(
            blink::scheduler::GetSingleThreadTaskRunnerForTesting()) {}

  void SetViewportSize(const IntSize& viewport_size) {
    viewport_size_ = viewport_size;
  }

  IntSize ViewportSize() const { return viewport_size_; }

  // ScrollableArea Impl
  int ScrollSize(ScrollbarOrientation orientation) const override {
    IntSize scroll_dimensions =
        MaximumScrollOffsetInt() - MinimumScrollOffsetInt();

    return (orientation == kHorizontalScrollbar) ? scroll_dimensions.Width()
                                                 : scroll_dimensions.Height();
  }

  void SetUserInputScrollable(bool x, bool y) {
    user_input_scrollable_x_ = x;
    user_input_scrollable_y_ = y;
  }

  IntSize ScrollOffsetInt() const override {
    return FlooredIntSize(scroll_offset_);
  }
  ScrollOffset GetScrollOffset() const override { return scroll_offset_; }
  IntSize MinimumScrollOffsetInt() const override { return IntSize(); }
  ScrollOffset MinimumScrollOffset() const override { return ScrollOffset(); }
  IntSize MaximumScrollOffsetInt() const override {
    return FlooredIntSize(MaximumScrollOffset());
  }

  IntRect VisibleContentRect(
      IncludeScrollbarsInRect = kExcludeScrollbars) const override {
    return IntRect(IntPoint(FlooredIntSize(scroll_offset_)), viewport_size_);
  }

  IntSize ContentsSize() const override { return contents_size_; }
  void SetContentSize(const IntSize& contents_size) {
    contents_size_ = contents_size;
  }

  scoped_refptr<base::SingleThreadTaskRunner> GetTimerTaskRunner() const final {
    return timer_task_runner_;
  }

  ScrollbarTheme& GetPageScrollbarTheme() const override {
    DEFINE_STATIC_LOCAL(ScrollbarThemeOverlayMock, theme, ());
    return theme;
  }
  bool ScrollAnimatorEnabled() const override { return true; }

  void Trace(blink::Visitor* visitor) override {
    ScrollableArea::Trace(visitor);
  }

 protected:
  CompositorElementId GetScrollElementId() const override {
    return CompositorElementId();
  }
  void UpdateScrollOffset(const ScrollOffset& offset,
                          mojom::blink::ScrollIntoViewParams::Type) override {
    scroll_offset_ = offset;
  }
  bool ShouldUseIntegerScrollOffset() const override { return true; }
  bool IsThrottled() const override { return false; }
  bool IsActive() const override { return true; }
  bool IsScrollCornerVisible() const override { return true; }
  IntRect ScrollCornerRect() const override { return IntRect(); }
  bool ScrollbarsCanBeActive() const override { return true; }
  bool ShouldPlaceVerticalScrollbarOnLeft() const override { return true; }
  void ScrollControlWasSetNeedsPaintInvalidation() override {}
  bool UserInputScrollable(ScrollbarOrientation orientation) const override {
    return orientation == kHorizontalScrollbar ? user_input_scrollable_x_
                                               : user_input_scrollable_y_;
  }
  bool ScheduleAnimation() override { return true; }
  WebColorScheme UsedColorScheme() const override {
    return ComputedStyle::InitialStyle().UsedColorScheme();
  }

  ScrollOffset ClampedScrollOffset(const ScrollOffset& offset) {
    ScrollOffset min_offset = MinimumScrollOffset();
    ScrollOffset max_offset = MaximumScrollOffset();
    float width = std::min(std::max(offset.Width(), min_offset.Width()),
                           max_offset.Width());
    float height = std::min(std::max(offset.Height(), min_offset.Height()),
                            max_offset.Height());
    return ScrollOffset(width, height);
  }

  bool user_input_scrollable_x_;
  bool user_input_scrollable_y_;
  ScrollOffset scroll_offset_;
  IntSize viewport_size_;
  IntSize contents_size_;
  scoped_refptr<base::SingleThreadTaskRunner> timer_task_runner_;
};

class RootLayoutViewportStub : public ScrollableAreaStub {
 public:
  RootLayoutViewportStub(const IntSize& viewport_size,
                         const IntSize& contents_size)
      : ScrollableAreaStub(viewport_size, contents_size) {}

  ScrollOffset MaximumScrollOffset() const override {
    return ScrollOffset(ContentsSize() - ViewportSize());
  }

  PhysicalRect DocumentToFrame(const PhysicalRect& rect) const {
    PhysicalRect ret = rect;
    ret.Move(-PhysicalOffset::FromFloatSizeRound(GetScrollOffset()));
    return ret;
  }

 private:
  int VisibleWidth() const override { return viewport_size_.Width(); }
  int VisibleHeight() const override { return viewport_size_.Height(); }
};

class VisualViewportStub : public ScrollableAreaStub {
 public:
  VisualViewportStub(const IntSize& viewport_size, const IntSize& contents_size)
      : ScrollableAreaStub(viewport_size, contents_size), scale_(1) {}

  ScrollOffset MaximumScrollOffset() const override {
    ScrollOffset visible_viewport(ViewportSize());
    visible_viewport.Scale(1 / scale_);

    ScrollOffset max_offset = ScrollOffset(ContentsSize()) - visible_viewport;
    return ScrollOffset(max_offset);
  }

  void SetScale(float scale) { scale_ = scale; }

 private:
  int VisibleWidth() const override { return viewport_size_.Width() / scale_; }
  int VisibleHeight() const override {
    return viewport_size_.Height() / scale_;
  }
  IntRect VisibleContentRect(IncludeScrollbarsInRect) const override {
    FloatSize size(viewport_size_);
    size.Scale(1 / scale_);
    return IntRect(IntPoint(FlooredIntSize(GetScrollOffset())),
                   ExpandedIntSize(size));
  }

  float scale_;
};

class RootFrameViewportTest : public testing::Test {
 public:
  RootFrameViewportTest() = default;

 protected:
  void SetUp() override {}
};

// Tests that scrolling the viewport when the layout viewport is
// !userInputScrollable (as happens when overflow:hidden is set) works
// correctly, that is, the visual viewport can scroll, but not the layout.
TEST_F(RootFrameViewportTest, UserInputScrollable) {
  IntSize viewport_size(100, 150);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(200, 300));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  visual_viewport->SetScale(2);

  // Disable just the layout viewport's horizontal scrolling, the
  // RootFrameViewport should remain scrollable overall.
  layout_viewport->SetUserInputScrollable(false, true);
  visual_viewport->SetUserInputScrollable(true, true);

  EXPECT_TRUE(root_frame_viewport->UserInputScrollable(kHorizontalScrollbar));
  EXPECT_TRUE(root_frame_viewport->UserInputScrollable(kVerticalScrollbar));

  // Layout viewport shouldn't scroll since it's not horizontally scrollable,
  // but visual viewport should.
  root_frame_viewport->UserScroll(ScrollGranularity::kScrollByPrecisePixel,
                                  FloatSize(300, 0),
                                  ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 0), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 0), root_frame_viewport->GetScrollOffset());

  // Vertical scrolling should be unaffected.
  root_frame_viewport->UserScroll(ScrollGranularity::kScrollByPrecisePixel,
                                  FloatSize(0, 300),
                                  ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(0, 150), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 75), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 225), root_frame_viewport->GetScrollOffset());

  // Try the same checks as above but for the vertical direction.
  // ===============================================

  root_frame_viewport->SetScrollOffset(
      ScrollOffset(), mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());

  // Disable just the layout viewport's vertical scrolling, the
  // RootFrameViewport should remain scrollable overall.
  layout_viewport->SetUserInputScrollable(true, false);
  visual_viewport->SetUserInputScrollable(true, true);

  EXPECT_TRUE(root_frame_viewport->UserInputScrollable(kHorizontalScrollbar));
  EXPECT_TRUE(root_frame_viewport->UserInputScrollable(kVerticalScrollbar));

  // Layout viewport shouldn't scroll since it's not vertically scrollable,
  // but visual viewport should.
  root_frame_viewport->UserScroll(ScrollGranularity::kScrollByPrecisePixel,
                                  FloatSize(0, 300),
                                  ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 75), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 75), root_frame_viewport->GetScrollOffset());

  // Horizontal scrolling should be unaffected.
  root_frame_viewport->UserScroll(ScrollGranularity::kScrollByPrecisePixel,
                                  FloatSize(300, 0),
                                  ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(100, 0), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 75), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(150, 75), root_frame_viewport->GetScrollOffset());
}

// Make sure scrolls using the scroll animator (scroll(), setScrollOffset())
// work correctly when one of the subviewports is explicitly scrolled without
// using the // RootFrameViewport interface.
TEST_F(RootFrameViewportTest, TestScrollAnimatorUpdatedBeforeScroll) {
  IntSize viewport_size(100, 150);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(200, 300));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  visual_viewport->SetScale(2);

  visual_viewport->SetScrollOffset(
      ScrollOffset(50, 75),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic);
  EXPECT_EQ(ScrollOffset(50, 75), root_frame_viewport->GetScrollOffset());

  // If the scroll animator doesn't update, it will still think it's at (0, 0)
  // and so it may early exit.
  root_frame_viewport->SetScrollOffset(
      ScrollOffset(0, 0),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(0, 0), root_frame_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 0), visual_viewport->GetScrollOffset());

  // Try again for userScroll()
  visual_viewport->SetScrollOffset(
      ScrollOffset(50, 75),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic);
  EXPECT_EQ(ScrollOffset(50, 75), root_frame_viewport->GetScrollOffset());

  root_frame_viewport->UserScroll(ScrollGranularity::kScrollByPrecisePixel,
                                  FloatSize(-50, 0),
                                  ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(0, 75), root_frame_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 75), visual_viewport->GetScrollOffset());

  // Make sure the layout viewport is also accounted for.
  root_frame_viewport->SetScrollOffset(
      ScrollOffset(0, 0),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  layout_viewport->SetScrollOffset(
      ScrollOffset(100, 150),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic);
  EXPECT_EQ(ScrollOffset(100, 150), root_frame_viewport->GetScrollOffset());

  root_frame_viewport->UserScroll(ScrollGranularity::kScrollByPrecisePixel,
                                  FloatSize(-100, 0),
                                  ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(0, 150), root_frame_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 150), layout_viewport->GetScrollOffset());
}

// Test that the scrollIntoView correctly scrolls the main frame
// and visual viewport such that the given rect is centered in the viewport.
TEST_F(RootFrameViewportTest, ScrollIntoView) {
  IntSize viewport_size(100, 150);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(200, 300));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  // Test that the visual viewport is scrolled if the viewport has been
  // resized (as is the case when the ChromeOS keyboard comes up) but not
  // scaled.
  visual_viewport->SetViewportSize(IntSize(100, 100));
  root_frame_viewport->ScrollIntoView(
      layout_viewport->DocumentToFrame(PhysicalRect(100, 250, 50, 50)),
      CreateScrollIntoViewParams(
          ScrollAlignment::kAlignToEdgeIfNeeded,
          ScrollAlignment::kAlignToEdgeIfNeeded,
          mojom::blink::ScrollIntoViewParams::Type::kProgrammatic, true,
          mojom::blink::ScrollIntoViewParams::Behavior::kInstant));
  EXPECT_EQ(ScrollOffset(50, 150), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 50), visual_viewport->GetScrollOffset());

  root_frame_viewport->ScrollIntoView(
      layout_viewport->DocumentToFrame(PhysicalRect(25, 75, 50, 50)),
      CreateScrollIntoViewParams(
          ScrollAlignment::kAlignToEdgeIfNeeded,
          ScrollAlignment::kAlignToEdgeIfNeeded,
          mojom::blink::ScrollIntoViewParams::Type::kProgrammatic, true,
          mojom::blink::ScrollIntoViewParams::Behavior::kInstant));
  EXPECT_EQ(ScrollOffset(25, 75), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 0), visual_viewport->GetScrollOffset());

  // Reset the visual viewport's size, scale the page, and repeat the test
  visual_viewport->SetViewportSize(IntSize(100, 150));
  visual_viewport->SetScale(2);
  root_frame_viewport->SetScrollOffset(
      ScrollOffset(), mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());

  root_frame_viewport->ScrollIntoView(
      layout_viewport->DocumentToFrame(PhysicalRect(50, 75, 50, 75)),
      CreateScrollIntoViewParams(
          ScrollAlignment::kAlignToEdgeIfNeeded,
          ScrollAlignment::kAlignToEdgeIfNeeded,
          mojom::blink::ScrollIntoViewParams::Type::kProgrammatic, true,
          mojom::blink::ScrollIntoViewParams::Behavior::kInstant));
  EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 75), visual_viewport->GetScrollOffset());

  root_frame_viewport->ScrollIntoView(
      layout_viewport->DocumentToFrame(PhysicalRect(190, 290, 10, 10)),
      CreateScrollIntoViewParams(
          ScrollAlignment::kAlignToEdgeIfNeeded,
          ScrollAlignment::kAlignToEdgeIfNeeded,
          mojom::blink::ScrollIntoViewParams::Type::kProgrammatic, true,
          mojom::blink::ScrollIntoViewParams::Behavior::kInstant));
  EXPECT_EQ(ScrollOffset(100, 150), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 75), visual_viewport->GetScrollOffset());

  // Scrolling into view the viewport rect itself should be a no-op.
  visual_viewport->SetViewportSize(IntSize(100, 100));
  visual_viewport->SetScale(1.5f);
  visual_viewport->SetScrollOffset(
      ScrollOffset(0, 10),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic);
  layout_viewport->SetScrollOffset(
      ScrollOffset(50, 50),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic);
  root_frame_viewport->SetScrollOffset(
      root_frame_viewport->GetScrollOffset(),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());

  root_frame_viewport->ScrollIntoView(
      layout_viewport->DocumentToFrame(PhysicalRect(
          root_frame_viewport->VisibleContentRect(kExcludeScrollbars))),
      CreateScrollIntoViewParams(
          ScrollAlignment::kAlignToEdgeIfNeeded,
          ScrollAlignment::kAlignToEdgeIfNeeded,
          mojom::blink::ScrollIntoViewParams::Type::kProgrammatic, true,
          mojom::blink::ScrollIntoViewParams::Behavior::kInstant));
  EXPECT_EQ(ScrollOffset(50, 50), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 10), visual_viewport->GetScrollOffset());

  root_frame_viewport->ScrollIntoView(
      layout_viewport->DocumentToFrame(PhysicalRect(
          root_frame_viewport->VisibleContentRect(kExcludeScrollbars))),
      CreateScrollIntoViewParams(
          ScrollAlignment::kAlignCenterAlways,
          ScrollAlignment::kAlignCenterAlways,
          mojom::blink::ScrollIntoViewParams::Type::kProgrammatic, true,
          mojom::blink::ScrollIntoViewParams::Behavior::kInstant));
  EXPECT_EQ(ScrollOffset(50, 50), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 10), visual_viewport->GetScrollOffset());

  root_frame_viewport->ScrollIntoView(
      layout_viewport->DocumentToFrame(PhysicalRect(
          root_frame_viewport->VisibleContentRect(kExcludeScrollbars))),
      CreateScrollIntoViewParams(
          ScrollAlignment::kAlignTopAlways, ScrollAlignment::kAlignTopAlways,
          mojom::blink::ScrollIntoViewParams::Type::kProgrammatic, true,
          mojom::blink::ScrollIntoViewParams::Behavior::kInstant));
  EXPECT_EQ(ScrollOffset(50, 50), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 10), visual_viewport->GetScrollOffset());
}

// Tests that the setScrollOffset method works correctly with both viewports.
TEST_F(RootFrameViewportTest, SetScrollOffset) {
  IntSize viewport_size(500, 500);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(1000, 2000));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  visual_viewport->SetScale(2);

  // Ensure that the visual viewport scrolls first.
  root_frame_viewport->SetScrollOffset(
      ScrollOffset(100, 100),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(100, 100), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());

  // Scroll to the visual viewport's extent, the layout viewport should scroll
  // the remainder.
  root_frame_viewport->SetScrollOffset(
      ScrollOffset(300, 400),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(250, 250), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 150), layout_viewport->GetScrollOffset());

  // Only the layout viewport should scroll further. Make sure it doesn't scroll
  // out of bounds.
  root_frame_viewport->SetScrollOffset(
      ScrollOffset(780, 1780),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(250, 250), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(500, 1500), layout_viewport->GetScrollOffset());

  // Scroll all the way back.
  root_frame_viewport->SetScrollOffset(
      ScrollOffset(0, 0),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(0, 0), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());
}

// Tests that the visible rect (i.e. visual viewport rect) is correctly
// calculated, taking into account both viewports and page scale.
TEST_F(RootFrameViewportTest, VisibleContentRect) {
  IntSize viewport_size(500, 401);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(1000, 2000));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  root_frame_viewport->SetScrollOffset(
      ScrollOffset(100, 75),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());

  EXPECT_EQ(IntPoint(100, 75),
            root_frame_viewport->VisibleContentRect().Location());
  EXPECT_EQ(ScrollOffset(500, 401),
            DoubleSize(root_frame_viewport->VisibleContentRect().Size()));

  visual_viewport->SetScale(2);

  EXPECT_EQ(IntPoint(100, 75),
            root_frame_viewport->VisibleContentRect().Location());
  EXPECT_EQ(ScrollOffset(250, 201),
            DoubleSize(root_frame_viewport->VisibleContentRect().Size()));
}

// Tests that scrolls on the root frame scroll the visual viewport before
// trying to scroll the layout viewport.
TEST_F(RootFrameViewportTest, ViewportScrollOrder) {
  IntSize viewport_size(100, 100);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(200, 300));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  visual_viewport->SetScale(2);

  root_frame_viewport->SetScrollOffset(
      ScrollOffset(40, 40), mojom::blink::ScrollIntoViewParams::Type::kUser,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback(base::BindOnce(
          [](ScrollableArea* visual_viewport, ScrollableArea* layout_viewport) {
            EXPECT_EQ(ScrollOffset(40, 40), visual_viewport->GetScrollOffset());
            EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());
          },
          visual_viewport, layout_viewport)));
  EXPECT_EQ(ScrollOffset(40, 40), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());

  root_frame_viewport->SetScrollOffset(
      ScrollOffset(60, 60),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback(base::BindOnce(
          [](ScrollableArea* visual_viewport, ScrollableArea* layout_viewport) {
            EXPECT_EQ(ScrollOffset(50, 50), visual_viewport->GetScrollOffset());
            EXPECT_EQ(ScrollOffset(10, 10), layout_viewport->GetScrollOffset());
          },
          visual_viewport, layout_viewport)));
  EXPECT_EQ(ScrollOffset(50, 50), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(10, 10), layout_viewport->GetScrollOffset());
}

// Tests that setting an alternate layout viewport scrolls the alternate
// instead of the original.
TEST_F(RootFrameViewportTest, SetAlternateLayoutViewport) {
  IntSize viewport_size(100, 100);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(200, 300));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* alternate_scroller = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(600, 500));

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  visual_viewport->SetScale(2);

  root_frame_viewport->SetScrollOffset(
      ScrollOffset(100, 100), mojom::blink::ScrollIntoViewParams::Type::kUser,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(50, 50), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 50), layout_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(100, 100), root_frame_viewport->GetScrollOffset());

  root_frame_viewport->SetLayoutViewport(*alternate_scroller);
  EXPECT_EQ(ScrollOffset(50, 50), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 0), alternate_scroller->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 50), root_frame_viewport->GetScrollOffset());

  root_frame_viewport->SetScrollOffset(
      ScrollOffset(200, 200), mojom::blink::ScrollIntoViewParams::Type::kUser,
      mojom::blink::ScrollIntoViewParams::Behavior::kInstant,
      ScrollableArea::ScrollCallback());
  EXPECT_EQ(ScrollOffset(50, 50), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(150, 150), alternate_scroller->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(200, 200), root_frame_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(50, 50), layout_viewport->GetScrollOffset());

  EXPECT_EQ(ScrollOffset(550, 450), root_frame_viewport->MaximumScrollOffset());
}

// Tests that scrolls on the root frame scroll the visual viewport before
// trying to scroll the layout viewport when using
// DistributeScrollBetweenViewports directly.
TEST_F(RootFrameViewportTest, DistributeScrollOrder) {
  IntSize viewport_size(100, 100);
  auto* layout_viewport = MakeGarbageCollected<RootLayoutViewportStub>(
      viewport_size, IntSize(200, 300));
  auto* visual_viewport =
      MakeGarbageCollected<VisualViewportStub>(viewport_size, viewport_size);

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      *visual_viewport, *layout_viewport);

  visual_viewport->SetScale(2);

  root_frame_viewport->DistributeScrollBetweenViewports(
      ScrollOffset(60, 60),
      mojom::blink::ScrollIntoViewParams::Type::kProgrammatic,
      mojom::blink::ScrollIntoViewParams::Behavior::kSmooth,
      RootFrameViewport::kVisualViewport,
      ScrollableArea::ScrollCallback(base::BindOnce(
          [](ScrollableArea* visual_viewport, ScrollableArea* layout_viewport) {
            EXPECT_EQ(ScrollOffset(50, 50), visual_viewport->GetScrollOffset());
            EXPECT_EQ(ScrollOffset(10, 10), layout_viewport->GetScrollOffset());
          },
          visual_viewport, layout_viewport)));
  root_frame_viewport->UpdateCompositorScrollAnimations();
  root_frame_viewport->ServiceScrollAnimations(1);
  EXPECT_EQ(ScrollOffset(0, 0), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(0, 0), layout_viewport->GetScrollOffset());
  root_frame_viewport->ServiceScrollAnimations(1000000);
  EXPECT_EQ(ScrollOffset(50, 50), visual_viewport->GetScrollOffset());
  EXPECT_EQ(ScrollOffset(10, 10), layout_viewport->GetScrollOffset());
}

class RootFrameViewportRenderTest : public RenderingTest {
 public:
  RootFrameViewportRenderTest()
      : RenderingTest(MakeGarbageCollected<EmptyLocalFrameClient>()) {}
};

TEST_F(RootFrameViewportRenderTest,
       ApplyPendingHistoryRestoreScrollOffsetTwice) {
  HistoryItem::ViewState view_state;
  view_state.page_scale_factor_ = 1.5;
  RootFrameViewport* root_frame_viewport = static_cast<RootFrameViewport*>(
      GetDocument().View()->GetScrollableArea());
  root_frame_viewport->SetPendingHistoryRestoreScrollOffset(view_state, false);
  root_frame_viewport->ApplyPendingHistoryRestoreScrollOffset();

  // Override the 1.5 scale with 1.0.
  GetDocument().GetPage()->GetVisualViewport().SetScale(1.0f);

  // The second call to ApplyPendingHistoryRestoreScrollOffset should
  // do nothing, since the history was already restored.
  root_frame_viewport->ApplyPendingHistoryRestoreScrollOffset();
  EXPECT_EQ(1.0f, GetDocument().GetPage()->GetVisualViewport().Scale());
}

}  // namespace blink
