#import "macos_candidate_panel.h"

#include <algorithm>

#include "macos_presentation.h"

@interface PPFlippedView : NSView
@end
@implementation PPFlippedView
- (BOOL)isFlipped { return YES; }
@end

@implementation PPCandidatePanel

- (instancetype)init {
  self = [super initWithContentRect:NSZeroRect
                         styleMask:NSWindowStyleMaskBorderless |
                                   NSWindowStyleMaskNonactivatingPanel
                           backing:NSBackingStoreBuffered
                             defer:NO];
  if (!self) return nil;
  self.releasedWhenClosed = NO;
  self.hidesOnDeactivate = NO;  // The input method is never the active app.
  self.ignoresMouseEvents = YES;
  self.level = NSPopUpMenuWindowLevel;
  self.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                            NSWindowCollectionBehaviorFullScreenAuxiliary;
  self.backgroundColor = [NSColor windowBackgroundColor];
  self.hasShadow = YES;
  return self;
}

- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }

- (void)showSnapshot:(const predictable_pinyin::StateSnapshot&)snapshot
          caretRect:(NSRect)caret {
  using namespace predictable_pinyin;
  if (snapshot.phase == Phase::kIdle || snapshot.candidates.empty()) {
    [self orderOut:nil];
    return;
  }
  NSScreen* screen = [NSScreen mainScreen];
  for (NSScreen* candidate in [NSScreen screens]) {
    if (NSPointInRect(caret.origin, candidate.frame)) {
      screen = candidate;
      break;
    }
  }
  if (!screen) return;
  const NSRect bounds = screen.visibleFrame;
  const CGFloat padding = 10;
  const CGFloat width = MIN(440, NSWidth(bounds));
  const CGFloat innerWidth = width - 2 * padding;
  const CGFloat rowHeight = 28;
  const std::string hint = BuildMacOSHint(snapshot);
  NSTextField* footer = [NSTextField wrappingLabelWithString:
      [NSString stringWithUTF8String:hint.c_str()] ?: @""];
  footer.identifier = @"hint-footer";
  footer.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
  footer.textColor = [NSColor secondaryLabelColor];
  footer.selectable = NO;
  const CGFloat hintHeight = ceil([footer.cell cellSizeForBounds:
      NSMakeRect(0, 0, innerWidth, CGFLOAT_MAX)].height);

  constexpr std::size_t pageSize = 10;
  const std::size_t selected = std::min(
      static_cast<std::size_t>(std::max(0, snapshot.selected_index)),
      snapshot.candidates.size() - 1);
  const std::size_t pageStart = (selected / pageSize) * pageSize;
  const std::size_t pageEnd = std::min(pageStart + pageSize, snapshot.candidates.size());
  // Keep selection and footer visible even on a small screen.
  const std::size_t maxRows = static_cast<std::size_t>(MAX(1,
      floor((NSHeight(bounds) - hintHeight - 4 * padding) / rowHeight)));
  const std::size_t rowCount = std::min(pageEnd - pageStart, maxRows);
  const std::size_t start = std::max(pageStart, selected + 1 > rowCount
      ? selected + 1 - rowCount : std::size_t{0});
  const CGFloat height = rowCount * rowHeight + hintHeight + 4 * padding;
  NSView* content = [[PPFlippedView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
  self.contentView = content;
  for (std::size_t row = 0; row < rowCount; ++row) {
    const std::size_t index = start + row;
    NSString* candidate = [NSString stringWithUTF8String:snapshot.candidates[index].c_str()];
    NSString* label = index < snapshot.candidate_labels.size()
        ? [NSString stringWithUTF8String:snapshot.candidate_labels[index].c_str()] : @"";
    NSString* prefix = [NSString stringWithFormat:@"%@%@  ",
        index == selected ? @"› " : @"  ", candidate];
    NSMutableAttributedString* title = [[NSMutableAttributedString alloc]
        initWithString:[prefix stringByAppendingString:label]
        attributes:@{
          NSFontAttributeName: [NSFont systemFontOfSize:16],
          NSForegroundColorAttributeName: index == selected
              ? [NSColor controlAccentColor] : [NSColor labelColor]
        }];
    if (index < snapshot.candidate_label_highlights.size()) {
      const auto count = static_cast<NSUInteger>(snapshot.candidate_label_highlights[index]);
      if (count > 0 && count <= label.length) {
        [title addAttribute:NSForegroundColorAttributeName value:[NSColor systemOrangeColor]
                      range:NSMakeRange(prefix.length, count)];
      }
    }
    NSTextField* rowLabel = [NSTextField labelWithAttributedString:title];
    rowLabel.frame = NSMakeRect(padding, padding + row * rowHeight, innerWidth, rowHeight);
    rowLabel.identifier = @"candidate-row";
    rowLabel.selectable = NO;
    rowLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [content addSubview:rowLabel];
  }
  NSBox* separator = [[NSBox alloc] initWithFrame:
      NSMakeRect(padding, padding + rowCount * rowHeight + padding, innerWidth, 1)];
  separator.boxType = NSBoxSeparator;
  [content addSubview:separator];
  footer.frame = NSMakeRect(padding, height - padding - hintHeight, innerWidth, hintHeight);
  [content addSubview:footer];

  CGFloat y = NSMinY(caret) - height - 4;
  if (y < NSMinY(bounds)) y = NSMaxY(caret) + 4;
  y = MAX(NSMinY(bounds), MIN(y, NSMaxY(bounds) - height));
  const CGFloat x = MAX(NSMinX(bounds), MIN(NSMinX(caret), NSMaxX(bounds) - width));
  [self setFrame:NSMakeRect(x, y, width, height) display:YES];
  [self orderFrontRegardless];
}

@end
