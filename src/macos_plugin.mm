// Predictable Pinyin macOS input method (IMK).
//
// Thin Objective-C++ bridge: translates NSEvent key events to single-char
// tokens, forwards them to PredictableStateMachine, and reflects the returned
// StateSnapshot back via IMK APIs. Mirrors src/ibus_plugin.cc.

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#import <InputMethodKit/InputMethodKit.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include "predictable_state_machine.h"

using predictable_pinyin::Phase;
using predictable_pinyin::PredictableStateMachine;
using predictable_pinyin::RimeSession;
using predictable_pinyin::StateSnapshot;

static NSString* const kBundleID =
    @"im.predictablepinyin.inputmethod.PredictablePinyin";
static NSString* const kConnectionName =
    @"im.predictablepinyin.inputmethod.PredictablePinyin_Connection";

// ---------------------------------------------------------------------------
// Helpers (paths, env)
// ---------------------------------------------------------------------------

static std::string GetEnvOrDefault(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : fallback;
}

static std::string SharedSupportDir() {
  NSString* path = [[NSBundle mainBundle] sharedSupportPath];
  return std::string(path ? path.UTF8String : "");
}

static std::string DefaultUserDataDir() {
  NSString* home = NSHomeDirectory();
  return std::string(home.UTF8String) + "/Library/Rime";
}

static void EnsureRimeUserData(const std::string& user_data_dir) {
  namespace fs = std::filesystem;
  fs::create_directories(user_data_dir);
  fs::path yaml = fs::path(user_data_dir) / "default.custom.yaml";
  if (!fs::exists(yaml)) {
    std::ofstream(yaml) << "patch:\n  schema_list:\n"
                           "    - schema: predictable_pinyin\n"
                           "    - schema: luna_pinyin\n"
                           "    - schema: pinyin_simp\n";
  }
}

// ---------------------------------------------------------------------------
// Key translation (parity with src/ibus_plugin.cc TranslateKey)
// ---------------------------------------------------------------------------

// Returns a single-char token for the state machine, or '\0' for keys we pass
// through. We key off characters rather than keycodes so punctuation shifted
// by the layout (e.g., `!`, `?`) maps cleanly.
static char TranslateKey(NSEvent* event) {
  NSString* chars = [event charactersIgnoringModifiers];
  if (chars.length == 0) return '\0';

  // Special keys from keyCode
  switch (event.keyCode) {
    case 36:  // kVK_Return
    case 76:  // kVK_ANSI_KeypadEnter
      return '\0';
    case 48:  // kVK_Tab
      return '\t';
    case 49:  // kVK_Space
      return ' ';
    case 51:  // kVK_Delete (Backspace)
      return '\b';
    default:
      break;
  }

  unichar c = [chars characterAtIndex:0];
  if (c >= 'a' && c <= 'z') return static_cast<char>(c);
  if (c >= 'A' && c <= 'Z') return static_cast<char>('a' + (c - 'A'));
  switch (c) {
    case ';': case ',': case '.': case '!': case '?': case ':':
    case '\\': case '(': case ')': case '[': case ']': case '<':
    case '>': case '~': case '\'':
      return static_cast<char>(c);
    default:
      return '\0';
  }
}

// ---------------------------------------------------------------------------
// Preedit formatting (parity with src/ibus_plugin.cc BuildPreedit)
// ---------------------------------------------------------------------------

static std::string BuildPreedit(const StateSnapshot& snap) {
  if (snap.phase == Phase::kIdle) return {};
  std::string preedit =
      snap.preedit.empty() ? snap.pinyin_buffer : snap.preedit;
  if (snap.phase == Phase::kStrokeInput || snap.phase == Phase::kSelecting) {
    preedit += " | ";
    preedit += snap.stroke_buffer;
  }
  if (!snap.hint.empty()) {
    preedit += "    ";
    preedit += snap.hint;
  }
  return preedit;
}

// ---------------------------------------------------------------------------
// PPInputController
// ---------------------------------------------------------------------------

// IMK candidate panel is owned by the process (one window) but driven per
// controller. Created in main() alongside the IMKServer; the controller pulls
// it via this accessor on demand.
static IMKCandidates* gCandidates = nil;

@interface PPInputController : IMKInputController {
  RimeSession* _session;
  PredictableStateMachine* _machine;
  StateSnapshot _snapshot;
  BOOL _chineseMode;
  BOOL _shiftOnly;   // true while Shift is held with no other key since down
}
@end

@implementation PPInputController

- (instancetype)initWithServer:(IMKServer*)server
                      delegate:(id)delegate
                        client:(id)inputClient {
  self = [super initWithServer:server delegate:delegate client:inputClient];
  if (!self) return nil;

  _session = nullptr;
  _machine = nullptr;
  new (&_snapshot) StateSnapshot();
  _chineseMode = YES;
  _shiftOnly = NO;

  try {
    const std::string shared = GetEnvOrDefault(
        "PREDICTABLE_PINYIN_SHARED_DATA_DIR", SharedSupportDir());
    const std::string user = GetEnvOrDefault(
        "PREDICTABLE_PINYIN_USER_DATA_DIR", DefaultUserDataDir());
    EnsureRimeUserData(user);

    const std::string schema = GetEnvOrDefault(
        "PREDICTABLE_PINYIN_SCHEMA_ID", "predictable_pinyin");
    const std::filesystem::path prism = GetEnvOrDefault(
        "PREDICTABLE_PINYIN_PRISM_PATH", shared + "/pinyin_simp.prism.txt");
    const std::filesystem::path stroke_dict = GetEnvOrDefault(
        "PREDICTABLE_PINYIN_STROKE_DICT_PATH", shared + "/stroke.dict.yaml");
    const std::filesystem::path hanzi_db = GetEnvOrDefault(
        "PREDICTABLE_PINYIN_HANZI_DB_PATH", shared + "/hanzi_db.csv");
    const std::filesystem::path pinyin_dict = GetEnvOrDefault(
        "PREDICTABLE_PINYIN_PINYIN_DICT_PATH",
        shared + "/pinyin_simp.dict.yaml");

    auto session = std::make_unique<RimeSession>(shared, user, schema);
    auto machine = std::make_unique<PredictableStateMachine>(session.get());
    if (machine->Initialize(prism, stroke_dict, hanzi_db, pinyin_dict)) {
      _snapshot = machine->Snapshot();
      _machine = machine.release();
      _session = session.release();
    } else {
      NSLog(@"predictable-pinyin: machine->Initialize returned false");
    }
  } catch (const std::exception& e) {
    NSLog(@"predictable-pinyin: init failed: %s", e.what());
  } catch (...) {
    NSLog(@"predictable-pinyin: init failed (unknown exception)");
  }
  return self;
}

- (void)dealloc {
  delete _machine;
  _machine = nullptr;
  delete _session;
  _session = nullptr;
  _snapshot.~StateSnapshot();
}

// Default IMK delivery is key-down only; opt in to flagsChanged so the
// Shift-alone toggle below is reachable.
- (NSUInteger)recognizedEvents:(id)sender {
  return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}

// Called by IMK for every key event while the IM is active.
- (BOOL)handleEvent:(NSEvent*)event client:(id)sender {
  if (event.type == NSEventTypeFlagsChanged) {
    return [self handleFlagsChanged:event client:sender];
  }
  if (event.type != NSEventTypeKeyDown) return NO;

  // Any non-shift key press cancels the Shift-toggle state.
  _shiftOnly = NO;

  // Pass Ctrl/Alt/Cmd combos through to the app.
  const NSEventModifierFlags kBlocking =
      NSEventModifierFlagControl | NSEventModifierFlagOption |
      NSEventModifierFlagCommand;
  if (event.modifierFlags & kBlocking) return NO;

  if (!_chineseMode || _machine == nullptr) return NO;

  const char key = TranslateKey(event);
  if (key == '\0') {
    return _snapshot.phase != Phase::kIdle;
  }

  const Phase prev = _snapshot.phase;
  _snapshot = _machine->HandleKey(key);

  if (!_snapshot.commit_text.empty()) {
    [sender insertText:[NSString stringWithUTF8String:_snapshot.commit_text.c_str()]
       replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
  }

  if (prev == Phase::kIdle && _snapshot.phase == Phase::kIdle &&
      _snapshot.commit_text.empty()) {
    return NO;
  }

  [self updateUIWithClient:sender];
  return YES;
}

// Shift-alone toggles CN/EN (matches ibus plugin behavior).
- (BOOL)handleFlagsChanged:(NSEvent*)event client:(id)sender {
  const NSEventModifierFlags flags = event.modifierFlags;
  const BOOL shiftDown = (flags & NSEventModifierFlagShift) != 0;
  const BOOL otherDown = (flags & (NSEventModifierFlagControl |
                                    NSEventModifierFlagOption |
                                    NSEventModifierFlagCommand)) != 0;
  if (shiftDown && !otherDown) {
    _shiftOnly = YES;
  } else if (!shiftDown && _shiftOnly) {
    _shiftOnly = NO;
    _chineseMode = !_chineseMode;
    if (_machine) {
      _snapshot = _machine->Reset();
      [self updateUIWithClient:sender];
    }
  } else {
    _shiftOnly = NO;
  }
  return NO;
}

- (void)updateUIWithClient:(id)sender {
  const std::string preedit = BuildPreedit(_snapshot);
  NSString* preeditStr = [NSString stringWithUTF8String:preedit.c_str()];
  NSAttributedString* attr = [[NSAttributedString alloc]
      initWithString:preeditStr ?: @""];
  [sender setMarkedText:attr
         selectionRange:NSMakeRange(preeditStr.length, 0)
       replacementRange:NSMakeRange(NSNotFound, NSNotFound)];

  // IMK only renders the candidate panel after we explicitly call
  // updateCandidates + show; returning strings from `-candidates:` is not
  // enough on its own.
  if (gCandidates == nil) return;
  if (_snapshot.candidates.empty()) {
    [gCandidates hide];
  } else {
    [gCandidates updateCandidates];
    [gCandidates show:kIMKLocateCandidatesBelowHint];
  }
}

- (NSArray*)candidates:(id)sender {
  NSMutableArray* arr =
      [NSMutableArray arrayWithCapacity:_snapshot.candidates.size()];
  for (std::size_t i = 0; i < _snapshot.candidates.size(); ++i) {
    NSString* cand =
        [NSString stringWithUTF8String:_snapshot.candidates[i].c_str()];
    NSString* label = (i < _snapshot.candidate_labels.size())
        ? [NSString stringWithUTF8String:_snapshot.candidate_labels[i].c_str()]
        : @"";
    [arr addObject:[NSString stringWithFormat:@"%@ %@", label, cand]];
  }
  return arr;
}

- (void)commitComposition:(id)sender {
  if (_machine != nullptr) {
    _snapshot = _machine->Reset();
  }
  [sender setMarkedText:@""
         selectionRange:NSMakeRange(0, 0)
       replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
  [gCandidates hide];
}

// Called by IMK when the input session ends (focus lost, etc.). Without this,
// the candidate window can persist across focus changes.
- (void)deactivateServer:(id)sender {
  [gCandidates hide];
  [super deactivateServer:sender];
}

@end

// ---------------------------------------------------------------------------
// TIS helpers (installer CLI)
// ---------------------------------------------------------------------------

// Registers the installed .app with Text Input Services so it shows up in
// System Settings → Keyboard → Input Sources. Just `open`-ing the bundle is
// not sufficient on modern macOS — TIS only scans /Library/Input Methods/ on
// login, and new bundles must call `TISRegisterInputSource` explicitly.
// Matches Squirrel's `--register-input-source` flow.
static int RegisterInputSource() {
  NSURL* url = [[NSBundle mainBundle] bundleURL];
  if (url == nil) {
    fprintf(stderr, "predictable-pinyin: bundleURL is nil\n");
    return 1;
  }
  OSStatus err = TISRegisterInputSource((__bridge CFURLRef)url);
  if (err != noErr) {
    fprintf(stderr,
            "predictable-pinyin: TISRegisterInputSource failed (OSStatus=%d)\n",
            (int)err);
    return 1;
  }
  fprintf(stdout, "predictable-pinyin: registered input source from %s\n",
          url.path.UTF8String);
  return 0;
}

// Look up every TIS source belonging to our bundle. TIS indexes the bundle
// asynchronously after `TISRegisterInputSource`, so the caller retries.
static CFArrayRef CopySourcesForBundle() {
  const void* keys[]   = { kTISPropertyBundleID };
  const void* values[] = { (__bridge const void*)kBundleID };
  CFDictionaryRef filter = CFDictionaryCreate(
      kCFAllocatorDefault, keys, values, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFArrayRef list = TISCreateInputSourceList(filter, true);
  CFRelease(filter);
  return list;
}

// Mirrors Register.swift from ~/playground/imk: after register, enable every
// returned source and select the per-mode keyboard source (id != bundleID).
static int EnableInputSource() {
  constexpr int kMaxAttempts = 10;
  CFArrayRef sources = nullptr;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    sources = CopySourcesForBundle();
    if (sources != nullptr && CFArrayGetCount(sources) > 0) break;
    if (sources != nullptr) { CFRelease(sources); sources = nullptr; }
    fprintf(stdout,
            "predictable-pinyin: input source not visible to TIS yet, "
            "retrying (%d/%d)...\n", attempt + 1, kMaxAttempts);
    [NSThread sleepForTimeInterval:0.3];
  }
  if (sources == nullptr || CFArrayGetCount(sources) == 0) {
    if (sources) CFRelease(sources);
    fprintf(stderr,
            "predictable-pinyin: no input sources found for %s after "
            "register. Check Contents/Info.plist, or log out and back in.\n",
            kBundleID.UTF8String);
    return 1;
  }
  const CFIndex n = CFArrayGetCount(sources);
  int failed = 0;
  for (CFIndex i = 0; i < n; ++i) {
    TISInputSourceRef src =
        (TISInputSourceRef)CFArrayGetValueAtIndex(sources, i);
    CFStringRef sid = (CFStringRef)TISGetInputSourceProperty(
        src, kTISPropertyInputSourceID);
    CFStringRef cat = (CFStringRef)TISGetInputSourceProperty(
        src, kTISPropertyInputSourceCategory);
    char idbuf[256] = {0};
    char catbuf[128] = {0};
    if (sid) CFStringGetCString(sid, idbuf, sizeof(idbuf), kCFStringEncodingUTF8);
    if (cat) CFStringGetCString(cat, catbuf, sizeof(catbuf), kCFStringEncodingUTF8);
    OSStatus err = TISEnableInputSource(src);
    fprintf(stdout,
            "predictable-pinyin: enable [%s] %s -> %s (OSStatus=%d)\n",
            catbuf, idbuf, err == noErr ? "ok" : "failed", (int)err);
    if (err != noErr) { ++failed; continue; }
    // The container source (id == bundleID) can't be selected directly;
    // only the per-mode keyboard source can become the active layout.
    const bool is_keyboard =
        cat != nullptr &&
        CFStringCompare(cat, kTISCategoryKeyboardInputSource, 0)
            == kCFCompareEqualTo;
    if (is_keyboard && sid != nullptr &&
        CFStringCompare(sid, (__bridge CFStringRef)kBundleID, 0)
            != kCFCompareEqualTo) {
      OSStatus sel = TISSelectInputSource(src);
      fprintf(stdout, "predictable-pinyin: select %s -> %s (OSStatus=%d)\n",
              idbuf, sel == noErr ? "ok" : "failed", (int)sel);
    }
  }
  CFRelease(sources);
  return failed == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    // CLI subcommands used by the install script. These exit before the IMK
    // run loop so the installer can complete synchronously.
    if (argc > 1) {
      const char* cmd = argv[1];
      if (std::strcmp(cmd, "--register-input-source") == 0 ||
          std::strcmp(cmd, "--install") == 0) {
        return RegisterInputSource();
      }
      if (std::strcmp(cmd, "--enable-input-source") == 0) {
        return EnableInputSource();
      }
    }

    NSApplication* app = [NSApplication sharedApplication];
    // `accessory` keeps the process alive as a background input-method server
    // without a Dock icon. Without this, NSApplication treats us as a regular
    // foreground app with no windows and terminates after a few seconds —
    // which means IMK never finishes registering our connection.
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    IMKServer* server = [[IMKServer alloc]
        initWithName:kConnectionName
        bundleIdentifier:[[NSBundle mainBundle] bundleIdentifier]];
    // One process-wide candidate panel; PPInputController drives show/hide.
    // kIMKSingleColumnScrollingCandidatePanel renders strings vertically with
    // 1..9 / 0 number labels — same shape the ibus engine produces.
    gCandidates = [[IMKCandidates alloc]
        initWithServer:server
             panelType:kIMKSingleColumnScrollingCandidatePanel];
    [app run];
  }
  return 0;
}
