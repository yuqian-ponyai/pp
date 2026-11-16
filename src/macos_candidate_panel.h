#pragma once

#import <AppKit/AppKit.h>

#include "predictable_state_machine.h"

// One window owns both real candidates and a nonselectable hint footer.
@interface PPCandidatePanel : NSPanel
- (void)showSnapshot:(const predictable_pinyin::StateSnapshot&)snapshot
          caretRect:(NSRect)caret;
@end
