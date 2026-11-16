#import "macos_candidate_panel.h"

#include <catch2/catch_test_macros.hpp>

#include "macos_presentation.h"

using namespace predictable_pinyin;

TEST_CASE("macOS candidates and nonselectable guidance share one window on every page",
          "[macos][ui]") {
  @autoreleasepool {
    [NSApplication sharedApplication];
    REQUIRE([NSScreen mainScreen] != nil);
    NSWindow* previousKeyWindow = [NSApp keyWindow];
    PPCandidatePanel* panel = [[PPCandidatePanel alloc] init];
    const NSRect bounds = [NSScreen mainScreen].visibleFrame;
    StateSnapshot snap;
    snap.phase = Phase::kSelecting;
    snap.hint = "SPACE commits [1]  J→2  K→3  L→5  F→next page";
    for (int i = 0; i < 23; ++i) {
      snap.candidates.push_back("中" + std::to_string(i));
      snap.candidate_labels.push_back("SZhs");
      snap.candidate_label_highlights.push_back(2);
    }
    for (const int selected : {0, 10, 22}) {
      snap.selected_index = selected;
      const NSRect caret = NSMakeRect(NSMidX(bounds), NSMidY(bounds), 1, 20);
      [panel showSnapshot:snap caretRect:caret];
      CHECK(panel.visible);
      CHECK_FALSE(panel.canBecomeKeyWindow);
      CHECK_FALSE(panel.canBecomeMainWindow);
      CHECK(panel.ignoresMouseEvents);
      CHECK([NSApp keyWindow] == previousKeyWindow);
      CHECK(NSContainsRect(bounds, panel.frame));
      std::size_t rows = 0;
      NSTextField* footer = nil;
      NSTextField* first = nil;
      for (NSView* view in panel.contentView.subviews) {
        if ([view.identifier isEqualToString:@"hint-footer"]) {
          REQUIRE([view isKindOfClass:[NSTextField class]]);
          footer = (NSTextField*)view;
        } else if ([view.identifier isEqualToString:@"candidate-row"]) {
          REQUIRE([view isKindOfClass:[NSTextField class]]);
          NSTextField* row = (NSTextField*)view;
          CHECK_FALSE(row.editable);
          CHECK_FALSE(row.selectable);
          CHECK(row.action == nullptr);
          if (!first) first = row;
          NSString* visible = [row.stringValue
              stringByTrimmingCharactersInSet:
                  [NSCharacterSet characterSetWithCharactersInString:@" ›"]];
          CHECK([visible hasPrefix:[NSString stringWithUTF8String:
              snap.candidates[(selected / 10) * 10 + rows].c_str()]]);
          CHECK(row.window == panel);
          CHECK(NSContainsRect(panel.contentView.bounds, row.frame));
          ++rows;
        }
      }
      REQUIRE(footer != nil);
      REQUIRE(first != nil);
      CHECK(rows == (selected == 22 ? 3 : 10));
      CHECK(footer.window == first.window);
      CHECK(NSContainsRect(panel.contentView.bounds, footer.frame));
      CHECK(NSMinY(footer.frame) > NSMaxY(first.frame));
      CHECK_FALSE(footer.selectable);
      CHECK_FALSE(footer.editable);
      CHECK(footer.action == nullptr);
      CHECK([footer.stringValue isEqualToString:
          [NSString stringWithUTF8String:BuildMacOSHint(snap).c_str()]]);
      CHECK(snap.candidates.size() == 23);  // Footer never becomes a candidate.
    }
    for (const NSPoint edge : {bounds.origin,
                               NSMakePoint(NSMaxX(bounds) - 1, NSMaxY(bounds) - 1)}) {
      [panel showSnapshot:snap caretRect:NSMakeRect(edge.x, edge.y, 0, 20)];
      CHECK(NSContainsRect(bounds, panel.frame));
    }
    snap.phase = Phase::kIdle;
    [panel showSnapshot:snap caretRect:NSZeroRect];
    CHECK_FALSE(panel.visible);
    snap.phase = Phase::kStrokeInput;
    snap.candidates.clear();
    [panel showSnapshot:snap caretRect:NSZeroRect];
    CHECK_FALSE(panel.visible);
    [panel orderOut:nil];
  }
}
