#import <Cocoa/Cocoa.h>
#include <cmath>
#include <cstring>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "app/macos/macos_bridge.h"
#include "core/save_file.h"

static NSString* const kPrefFullScreenByDefault = @"FullScreenByDefault";
static NSString* const kPrefSoundMuted = @"SoundMuted";
static NSString* const kPrefSoundVolume = @"SoundVolume";
static NSString* const kPrefSkipBriefing = @"SkipBriefing";
static NSString* const kPrefDefaultDifficulty = @"DefaultDifficulty";
static NSString* const kPrefDefaultPassword = @"DefaultPassword";
static NSString* const kPrefEnableMouseMovement = @"EnableMouseMovement";

@interface TrekMacAppController : NSObject <NSWindowDelegate, NSTextFieldDelegate>

@property (nonatomic, strong, nullable) NSWindow* settingsWindow;
@property (nonatomic, strong, nullable) NSButton* fullScreenCheckbox;
@property (nonatomic, strong, nullable) NSButton* skipBriefingCheckbox;
@property (nonatomic, strong, nullable) NSPopUpButton* difficultyPopup;
@property (nonatomic, strong, nullable) NSTextField* passwordField;
@property (nonatomic, strong, nullable) NSButton* mouseMovementCheckbox;
@property (nonatomic, strong, nullable) NSButton* settingsMuteCheckbox;
@property (nonatomic, strong, nullable) NSSlider* settingsVolumeSlider;
@property (nonatomic, strong, nullable) NSTextField* settingsVolumeLabel;

@property (nonatomic, strong, nullable) NSWindow* helpWindow;

@property (nonatomic, strong, nullable) NSMenuItem* soundMenuMuteItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuIncreaseItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuDecreaseItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuVolumeIndicator;
@property (nonatomic, strong, nullable) NSTimer* trackingTimer;

@property (nonatomic, assign) trek::macos::AppBridgeCallbacks callbacks;

- (void)openSettingsWindow:(nullable id)sender;
- (void)openHelpWindow:(nullable id)sender;
- (void)toggleMute:(nullable id)sender;
- (void)increaseVolume:(nullable id)sender;
- (void)decreaseVolume:(nullable id)sender;
- (void)syncSoundUIWithVolume:(float)vol muted:(BOOL)muted;
- (void)startMenuTrackingTimer;
- (void)stopMenuTrackingTimer;

- (void)newGame:(nullable id)sender;
- (void)openGame:(nullable id)sender;
- (void)saveGame:(nullable id)sender;
- (void)saveGameAs:(nullable id)sender;
- (void)promptSavePanelWithTitle:(NSString*)title message:(NSString*)message;

- (void)enterSectorCoordinates:(nullable id)sender;
- (void)enterQuadrantCoordinates:(nullable id)sender;

- (BOOL)validateMenuItem:(NSMenuItem*)item;

@end

static TrekMacAppController* s_controller = nil;

@implementation TrekMacAppController

- (void)openSettingsWindow:(nullable id)sender {
    if (!self.settingsWindow) {
        [self buildSettingsWindow];
    }
    
    const BOOL isFs = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
    self.fullScreenCheckbox.state = isFs ? NSControlStateValueOn : NSControlStateValueOff;

    const BOOL skipVal = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefSkipBriefing];
    self.skipBriefingCheckbox.state = skipVal ? NSControlStateValueOn : NSControlStateValueOff;

    const NSInteger savedDiff = [[NSUserDefaults standardUserDefaults] integerForKey:kPrefDefaultDifficulty];
    [self.difficultyPopup selectItemWithTag:savedDiff];

    NSString* savedPass = [[NSUserDefaults standardUserDefaults] stringForKey:kPrefDefaultPassword];
    self.passwordField.stringValue = savedPass ? savedPass : @"";

    id mmObj = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefEnableMouseMovement];
    const BOOL mmVal = (mmObj != nil) ? [mmObj boolValue] : YES;
    self.mouseMovementCheckbox.state = mmVal ? NSControlStateValueOn : NSControlStateValueOff;
    
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
    
    [self.settingsWindow makeKeyAndOrderFront:sender];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)buildSettingsWindow {
    const NSRect frame = NSMakeRect(0, 0, 460, 430);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.title = @"Settings";
    [win center];
    win.releasedWhenClosed = NO;
    win.delegate = self;

    NSView* content = [[NSView alloc] initWithFrame:frame];
    win.contentView = content;

    // --- Startup & Display Section ---
    NSTextField* displayHeader = [NSTextField labelWithString:@"Startup & Display"];
    displayHeader.font = [NSFont boldSystemFontOfSize:13];
    displayHeader.frame = NSMakeRect(20, 390, 420, 20);
    [content addSubview:displayHeader];

    NSButton* fsCheck = [NSButton checkboxWithTitle:@"Open in Full Screen by default"
                                             target:self
                                             action:@selector(fullScreenCheckboxChanged:)];
    fsCheck.frame = NSMakeRect(30, 362, 400, 24);
    const BOOL fsVal = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
    fsCheck.state = fsVal ? NSControlStateValueOn : NSControlStateValueOff;
    [content addSubview:fsCheck];
    self.fullScreenCheckbox = fsCheck;

    NSTextField* fsHint = [NSTextField labelWithString:@"When enabled, EGA Trek opens directly into full screen upon launch."];
    fsHint.font = [NSFont systemFontOfSize:11];
    fsHint.textColor = [NSColor secondaryLabelColor];
    fsHint.frame = NSMakeRect(48, 345, 390, 16);
    [content addSubview:fsHint];

    // Separator line 1
    NSBox* separator1 = [[NSBox alloc] initWithFrame:NSMakeRect(20, 332, 420, 1)];
    separator1.boxType = NSBoxSeparator;
    [content addSubview:separator1];

    // --- Gameplay & Controls Section ---
    NSTextField* gameHeader = [NSTextField labelWithString:@"Gameplay & Controls"];
    gameHeader.font = [NSFont boldSystemFontOfSize:13];
    gameHeader.frame = NSMakeRect(20, 302, 420, 20);
    [content addSubview:gameHeader];

    NSButton* skipCheck = [NSButton checkboxWithTitle:@"Auto skip briefing on startup"
                                               target:self
                                               action:@selector(skipBriefingCheckboxChanged:)];
    skipCheck.frame = NSMakeRect(30, 274, 400, 24);
    const BOOL skipVal = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefSkipBriefing];
    skipCheck.state = skipVal ? NSControlStateValueOn : NSControlStateValueOff;
    [content addSubview:skipCheck];
    self.skipBriefingCheckbox = skipCheck;

    NSTextField* skipHint = [NSTextField labelWithString:@"When enabled, automatically skips the briefing during initial game startup. When disabled (default), prompts for Y or N."];
    skipHint.font = [NSFont systemFontOfSize:11];
    skipHint.textColor = [NSColor secondaryLabelColor];
    skipHint.frame = NSMakeRect(48, 257, 390, 16);
    [content addSubview:skipHint];

    NSTextField* diffLabel = [NSTextField labelWithString:@"Default difficulty:"];
    diffLabel.frame = NSMakeRect(30, 225, 150, 18);
    [content addSubview:diffLabel];

    NSPopUpButton* diffPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(180, 221, 180, 26) pullsDown:NO];
    [diffPopup addItemWithTitle:@"Ask Every Time"];
    diffPopup.lastItem.tag = 0;
    [diffPopup addItemWithTitle:@"1 - Novice"];
    diffPopup.lastItem.tag = 1;
    [diffPopup addItemWithTitle:@"2 - Fair"];
    diffPopup.lastItem.tag = 2;
    [diffPopup addItemWithTitle:@"3 - Good"];
    diffPopup.lastItem.tag = 3;
    [diffPopup addItemWithTitle:@"4 - Expert"];
    diffPopup.lastItem.tag = 4;
    [diffPopup addItemWithTitle:@"5 - Emeritus"];
    diffPopup.lastItem.tag = 5;
    diffPopup.target = self;
    diffPopup.action = @selector(difficultyPopupChanged:);
    const NSInteger savedDiff = [[NSUserDefaults standardUserDefaults] integerForKey:kPrefDefaultDifficulty];
    [diffPopup selectItemWithTag:savedDiff];
    [content addSubview:diffPopup];
    self.difficultyPopup = diffPopup;

    NSTextField* passLabel = [NSTextField labelWithString:@"Default self-destruct password:"];
    passLabel.frame = NSMakeRect(30, 192, 190, 18);
    [content addSubview:passLabel];

    NSTextField* passField = [[NSTextField alloc] initWithFrame:NSMakeRect(225, 190, 180, 22)];
    passField.placeholderString = @"None (Ask Every Time)";
    passField.delegate = self;
    passField.target = self;
    passField.action = @selector(passwordFieldChanged:);
    NSString* savedPass = [[NSUserDefaults standardUserDefaults] stringForKey:kPrefDefaultPassword];
    if (savedPass) passField.stringValue = savedPass;
    [content addSubview:passField];
    self.passwordField = passField;

    NSButton* mouseCheck = [NSButton checkboxWithTitle:@"Enable mouse movement"
                                                target:self
                                                action:@selector(mouseMovementCheckboxChanged:)];
    mouseCheck.frame = NSMakeRect(30, 152, 400, 24);
    id mmObj = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefEnableMouseMovement];
    const BOOL mmVal = (mmObj != nil) ? [mmObj boolValue] : YES;
    mouseCheck.state = mmVal ? NSControlStateValueOn : NSControlStateValueOff;
    [content addSubview:mouseCheck];
    self.mouseMovementCheckbox = mouseCheck;

    NSTextField* mouseHint = [NSTextField labelWithString:@"Clicking sector or quadrant maps auto-fills movement coordinates."];
    mouseHint.font = [NSFont systemFontOfSize:11];
    mouseHint.textColor = [NSColor secondaryLabelColor];
    mouseHint.frame = NSMakeRect(48, 135, 390, 16);
    [content addSubview:mouseHint];

    // Separator line 2
    NSBox* separator2 = [[NSBox alloc] initWithFrame:NSMakeRect(20, 122, 420, 1)];
    separator2.boxType = NSBoxSeparator;
    [content addSubview:separator2];

    // --- Audio Section ---
    NSTextField* audioHeader = [NSTextField labelWithString:@"Game Audio"];
    audioHeader.font = [NSFont boldSystemFontOfSize:13];
    audioHeader.frame = NSMakeRect(20, 92, 420, 20);
    [content addSubview:audioHeader];

    NSButton* muteCheck = [NSButton checkboxWithTitle:@"Mute sound"
                                               target:self
                                               action:@selector(settingsMuteCheckboxChanged:)];
    muteCheck.frame = NSMakeRect(30, 62, 120, 24);
    [content addSubview:muteCheck];
    self.settingsMuteCheckbox = muteCheck;

    NSTextField* volTitle = [NSTextField labelWithString:@"Volume:"];
    volTitle.frame = NSMakeRect(160, 66, 60, 18);
    [content addSubview:volTitle];

    NSSlider* slider = [NSSlider sliderWithValue:1.0
                                        minValue:0.0
                                        maxValue:1.0
                                          target:self
                                          action:@selector(settingsVolumeSliderChanged:)];
    slider.frame = NSMakeRect(220, 64, 140, 20);
    slider.continuous = YES;
    [content addSubview:slider];
    self.settingsVolumeSlider = slider;

    NSTextField* volVal = [NSTextField labelWithString:@"100%"];
    volVal.frame = NSMakeRect(370, 66, 50, 18);
    [content addSubview:volVal];
    self.settingsVolumeLabel = volVal;

    self.settingsWindow = win;
}

- (void)skipBriefingCheckboxChanged:(NSButton*)sender {
    const BOOL skip = (sender.state == NSControlStateValueOn);
    [[NSUserDefaults standardUserDefaults] setBool:skip forKey:kPrefSkipBriefing];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)difficultyPopupChanged:(NSPopUpButton*)sender {
    const NSInteger tag = sender.selectedItem.tag;
    [[NSUserDefaults standardUserDefaults] setInteger:tag forKey:kPrefDefaultDifficulty];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)passwordFieldChanged:(NSTextField*)sender {
    NSString* val = sender.stringValue ? sender.stringValue : @"";
    [[NSUserDefaults standardUserDefaults] setObject:val forKey:kPrefDefaultPassword];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)controlTextDidChange:(NSNotification*)note {
    if (note.object == self.passwordField) {
        [self passwordFieldChanged:self.passwordField];
    }
}

- (void)mouseMovementCheckboxChanged:(NSButton*)sender {
    const BOOL mm = (sender.state == NSControlStateValueOn);
    [[NSUserDefaults standardUserDefaults] setBool:mm forKey:kPrefEnableMouseMovement];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)fullScreenCheckboxChanged:(NSButton*)sender {
    const BOOL isFs = (sender.state == NSControlStateValueOn);
    [[NSUserDefaults standardUserDefaults] setBool:isFs forKey:kPrefFullScreenByDefault];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

- (void)settingsMuteCheckboxChanged:(NSButton*)sender {
    const BOOL m = (sender.state == NSControlStateValueOn);
    if (self.callbacks.set_muted) {
        self.callbacks.set_muted(self.callbacks.context, m);
    }
    float vol = 1.0f;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:m];
}

- (void)settingsVolumeSliderChanged:(NSSlider*)sender {
    const float val = [sender floatValue];
    if (self.callbacks.set_volume) {
        self.callbacks.set_volume(self.callbacks.context, val);
    }
    bool muted = false;
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:val muted:muted];
}

- (void)openHelpWindow:(nullable id)sender {
    if (!self.helpWindow) {
        [self buildHelpWindow];
    }
    [self.helpWindow makeKeyAndOrderFront:sender];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)buildHelpWindow {
    const NSRect frame = NSMakeRect(0, 0, 560, 500);
    NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    win.title = @"EGA Trek Help";
    win.minSize = NSMakeSize(440, 340);
    [win center];
    win.releasedWhenClosed = NO;
    win.delegate = self;

    NSView* content = [[NSView alloc] initWithFrame:frame];
    win.contentView = content;

    // Header
    NSTextField* titleLabel = [NSTextField labelWithString:@"EGA Trek Help"];
    titleLabel.font = [NSFont boldSystemFontOfSize:18];
    titleLabel.frame = NSMakeRect(20, 455, 520, 28);
    titleLabel.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable;
    [content addSubview:titleLabel];

    NSTextField* subtitleLabel = [NSTextField labelWithString:@"Command Quick Reference & Game Guide"];
    subtitleLabel.font = [NSFont systemFontOfSize:12];
    subtitleLabel.textColor = [NSColor secondaryLabelColor];
    subtitleLabel.frame = NSMakeRect(20, 435, 520, 18);
    subtitleLabel.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable;
    [content addSubview:subtitleLabel];

    // Scrollable TextView
    const NSRect scrollFrame = NSMakeRect(20, 55, 520, 370);
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:scrollFrame];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = NO;
    scroll.borderType = NSBezelBorder;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSTextView* text = [[NSTextView alloc] initWithFrame:scroll.contentView.bounds];
    text.editable = NO;
    text.selectable = YES;
    text.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    text.textContainer.containerSize = NSMakeSize(scroll.contentSize.width, CGFLOAT_MAX);
    text.textContainer.widthTracksTextView = YES;
    text.autoresizingMask = NSViewWidthSizable;

    NSString* helpText =
        @"EGA TREK (Version 1.0)\n"
        @"Native Port for macOS Apple Silicon\n"
        @"------------------------------------------------------------\n\n"
        @"COMMAND QUICK REFERENCE\n\n"
        @"Navigation & Operations:\n"
        @"  M)ove       Move to quadrant or sector (coordinates X,Y)\n"
        @"  W)arp       Set Warp engine speed (0 - 8)\n"
        @"  O)rbit      Standard Orbit around planet\n"
        @"  D)ock       Dock with adjacent StarBase (replenishes energy & torpedos)\n"
        @"  F)ix        Fix and repair ship systems while docked at StarBase\n"
        @"  HAIL        Hail a StarBase\n"
        @"  MINE        Send Mining party down to planet\n"
        @"  LOAD        Load mined raw dilithium crystals\n\n"
        @"Tactical & Combat:\n"
        @"  P)hasers    Fire Phasers (specify energy units to fire)\n"
        @"  T)orps      Fire Photon Torpedos (specify trajectory 1-9)\n"
        @"  SHUP        Shields Up (or Up Arrow key)\n"
        @"  SHDN        Shields Down (or Down Arrow key)\n"
        @"  S)elf       Ship Self-Destruct sequence\n\n"
        @"Sensors & Intel:\n"
        @"  L)ong       Long range sensor scan\n"
        @"  C)hart      Display galactic star chart of known space\n"
        @"  R)epair     State of repair and damage control report\n"
        @"  E)nergy     Energy transfer between systems\n"
        @"  A#          Acknowledge incoming Starfleet message #\n\n"
        @"Game & System Controls:\n"
        @"  Cmd+N       New Game\n"
        @"  Cmd+O       Open Saved Game (.egatrek)\n"
        @"  Cmd+S       Save Game\n"
        @"  Cmd+Shift+S Save Game As…\n"
        @"  Cmd+K       Set Sector Coordinates…\n"
        @"  Cmd+J       Set Quadrant Coordinates…\n"
        @"  Cmd+Q       Quit application\n"
        @"  Ctrl+Cmd+F  Toggle Full Screen\n"
        @"  Cmd+,       Open Settings\n"
        @"  Cmd+Shift+M Toggle Sound Mute\n"
        @"  Cmd+=       Increase Sound Volume\n"
        @"  Cmd+-       Decrease Sound Volume\n"
        @"  Shift-F1    Boss Mode (instant DOS prompt cover screen)\n\n"
        @"Mouse Controls:\n"
        @"  Click Title Screen: Advance past title screen\n"
        @"  Click Sector Map:   Auto-fill movement to clicked sector\n"
        @"  Click Quadrant Map: Auto-fill warp movement to clicked quadrant\n"
        @"  Click Message Box:  Dismiss / acknowledge incoming message\n";

    if (self.callbacks.get_doc_text) {
        const char* doc = self.callbacks.get_doc_text(self.callbacks.context);
        if (doc && doc[0]) {
            NSString* docStr = [NSString stringWithUTF8String:doc];
            helpText = [helpText stringByAppendingFormat:@"\n============================================================\nORIGINAL GAME MANUAL (TREK.DOC)\n============================================================\n\n%@", docStr];
        }
    }

    text.string = helpText;
    scroll.documentView = text;
    [content addSubview:scroll];

    // Close button
    NSButton* closeBtn = [NSButton buttonWithTitle:@"Close" target:win action:@selector(performClose:)];
    closeBtn.frame = NSMakeRect(450, 15, 90, 30);
    closeBtn.keyEquivalent = @"\r";
    closeBtn.autoresizingMask = NSViewMaxYMargin | NSViewMinXMargin;
    [content addSubview:closeBtn];

    self.helpWindow = win;
}

- (void)toggleMute:(nullable id)sender {
    (void)sender;
    if (self.callbacks.toggle_mute) {
        self.callbacks.toggle_mute(self.callbacks.context);
    }
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
}

- (void)increaseVolume:(nullable id)sender {
    (void)sender;
    if (self.callbacks.increase_volume) {
        self.callbacks.increase_volume(self.callbacks.context);
    }
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
}

- (void)decreaseVolume:(nullable id)sender {
    (void)sender;
    if (self.callbacks.decrease_volume) {
        self.callbacks.decrease_volume(self.callbacks.context);
    }
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
}

- (void)syncSoundUIWithVolume:(float)vol muted:(BOOL)muted {
    const int percent = (int)std::round(vol * 100.0f);

    // Update Sound Menu items
    if (self.soundMenuMuteItem) {
        self.soundMenuMuteItem.title = muted ? @"Unmute" : @"Mute";
        self.soundMenuMuteItem.state = muted ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (self.soundMenuVolumeIndicator) {
        self.soundMenuVolumeIndicator.title = [NSString stringWithFormat:@"Volume: %d%%", percent];
    }

    // Update Settings window controls if open
    if (self.settingsMuteCheckbox) {
        self.settingsMuteCheckbox.state = muted ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (self.settingsVolumeSlider) {
        self.settingsVolumeSlider.floatValue = vol;
    }
    if (self.settingsVolumeLabel) {
        self.settingsVolumeLabel.stringValue = [NSString stringWithFormat:@"%d%%", percent];
    }
}

- (void)startMenuTrackingTimer {
    [self stopMenuTrackingTimer];
    self.trackingTimer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
                                                 target:self
                                               selector:@selector(onMenuTrackingTick:)
                                               userInfo:nil
                                                repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:self.trackingTimer forMode:NSRunLoopCommonModes];
}

- (void)stopMenuTrackingTimer {
    if (self.trackingTimer) {
        [self.trackingTimer invalidate];
        self.trackingTimer = nil;
    }
}

- (void)onMenuTrackingTick:(NSTimer*)timer {
    (void)timer;
    if (self.callbacks.step_frame) {
        self.callbacks.step_frame(self.callbacks.context);
    }
}

- (void)newGame:(nullable id)sender {
    (void)sender;
    if (self.callbacks.new_game) {
        self.callbacks.new_game(self.callbacks.context);
    }
}

- (void)openGame:(nullable id)sender {
    (void)sender;
    if (self.callbacks.confirm_unsaved_changes) {
        if (!self.callbacks.confirm_unsaved_changes(self.callbacks.context, "Do you want to save the changes to the current game before opening another?")) {
            return;
        }
    }
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Open Saved Game";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[ @"egatrek", @"sav", @"SAV" ];
    panel.allowsOtherFileTypes = YES;

    if (self.callbacks.get_default_save_dir) {
        const char* dir = self.callbacks.get_default_save_dir(self.callbacks.context);
        if (dir && dir[0]) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir]];
        }
    }

    if ([panel runModal] == NSModalResponseOK) {
        NSString* path = panel.URL.path;
        if (path.length > 0) {
            const char* cPath = [path UTF8String];
            if (!trek::validate_save_file(cPath)) {
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = @"Unable to Load Save File";
                alert.informativeText = @"The selected file is not a valid EGA Trek save file.";
                alert.alertStyle = NSAlertStyleWarning;
                [alert addButtonWithTitle:@"OK"];
                [alert runModal];
                return;
            }
            if (self.callbacks.open_game) {
                self.callbacks.open_game(self.callbacks.context, cPath);
            }
        }
    }
}

- (void)promptSavePanelWithTitle:(NSString*)title message:(NSString*)message {
    if (self.callbacks.can_save && !self.callbacks.can_save(self.callbacks.context)) {
        NSBeep();
        return;
    }
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = title;
    panel.message = message;
    panel.allowedFileTypes = @[ @"egatrek", @"sav" ];
    panel.allowsOtherFileTypes = YES;
    panel.canCreateDirectories = YES;

    NSString* defaultName = @"TREK.egatrek";
    if (self.callbacks.get_active_save_path) {
        const char* cur = self.callbacks.get_active_save_path(self.callbacks.context);
        if (cur && cur[0]) {
            NSString* curStr = [NSString stringWithUTF8String:cur];
            defaultName = [curStr lastPathComponent];
            panel.directoryURL = [NSURL fileURLWithPath:[curStr stringByDeletingLastPathComponent]];
        }
    }
    if (!panel.directoryURL && self.callbacks.get_default_save_dir) {
        const char* dir = self.callbacks.get_default_save_dir(self.callbacks.context);
        if (dir && dir[0]) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir]];
        }
    }
    panel.nameFieldStringValue = defaultName;

    if ([panel runModal] == NSModalResponseOK) {
        NSString* path = panel.URL.path;
        if (path.length > 0 && self.callbacks.save_game_as) {
            self.callbacks.save_game_as(self.callbacks.context, [path UTF8String]);
        }
    }
}

- (void)saveGame:(nullable id)sender {
    (void)sender;
    if (self.callbacks.can_save && !self.callbacks.can_save(self.callbacks.context)) {
        NSBeep();
        return;
    }
    if (self.callbacks.has_active_save && self.callbacks.has_active_save(self.callbacks.context)) {
        if (self.callbacks.save_game) {
            self.callbacks.save_game(self.callbacks.context);
        }
    } else {
        [self promptSavePanelWithTitle:@"Save Game" message:@"Choose a name and location to save your game:"];
    }
}

- (void)saveGameAs:(nullable id)sender {
    (void)sender;
    [self promptSavePanelWithTitle:@"Save Game As" message:@"Choose a name and location to save a copy of your game:"];
}

- (void)enterSectorCoordinates:(nullable id)sender {
    (void)sender;
    if (self.callbacks.can_control && !self.callbacks.can_control(self.callbacks.context)) {
        NSBeep();
        return;
    }
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Set Sector Coordinates";
    alert.informativeText = @"Enter sector coordinates as row, column (1-8, e.g. 3,5):";
    alert.alertStyle = NSAlertStyleInformational;
    [alert addButtonWithTitle:@"Set"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField* input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
    input.placeholderString = @"3,5";
    alert.accessoryView = input;
    [alert.window setInitialFirstResponder:input];

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        NSString* text = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (text.length > 0 && self.callbacks.enter_coordinates) {
            self.callbacks.enter_coordinates(self.callbacks.context, [text UTF8String]);
        }
    }
}

- (void)enterQuadrantCoordinates:(nullable id)sender {
    (void)sender;
    if (self.callbacks.can_control && !self.callbacks.can_control(self.callbacks.context)) {
        NSBeep();
        return;
    }
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Set Quadrant Coordinates";
    alert.informativeText = @"Enter quadrant coordinates as row, column (1-8, e.g. 6,2):";
    alert.alertStyle = NSAlertStyleInformational;
    [alert addButtonWithTitle:@"Set"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField* input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
    input.placeholderString = @"6,2";
    alert.accessoryView = input;
    [alert.window setInitialFirstResponder:input];

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        NSString* text = [input.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (text.length > 0 && self.callbacks.enter_coordinates) {
            self.callbacks.enter_coordinates(self.callbacks.context, [text UTF8String]);
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(saveGame:) || item.action == @selector(saveGameAs:)) {
        if (self.callbacks.can_save) {
            return self.callbacks.can_save(self.callbacks.context) ? YES : NO;
        }
        return NO;
    }
    if (item.action == @selector(enterSectorCoordinates:) || item.action == @selector(enterQuadrantCoordinates:)) {
        if (self.callbacks.can_control) {
            return self.callbacks.can_control(self.callbacks.context) ? YES : NO;
        }
        return NO;
    }
    return YES;
}

- (void)dealloc {
    [self stopMenuTrackingTimer];
}

@end

namespace trek::macos {

bool is_fullscreen_by_default() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
}

void set_fullscreen_by_default(bool enabled) {
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:kPrefFullScreenByDefault];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

float get_saved_sound_volume() {
    id val = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefSoundVolume];
    if (val == nil) return 1.0f;
    return [val floatValue];
}

bool get_saved_sound_muted() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kPrefSoundMuted];
}

void save_sound_preferences(float volume, bool muted) {
    [[NSUserDefaults standardUserDefaults] setFloat:volume forKey:kPrefSoundVolume];
    [[NSUserDefaults standardUserDefaults] setBool:muted forKey:kPrefSoundMuted];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

bool should_skip_briefing() {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kPrefSkipBriefing];
}

void set_skip_briefing(bool skip) {
    [[NSUserDefaults standardUserDefaults] setBool:skip forKey:kPrefSkipBriefing];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

int get_default_difficulty() {
    return static_cast<int>([[NSUserDefaults standardUserDefaults] integerForKey:kPrefDefaultDifficulty]);
}

void set_default_difficulty(int difficulty) {
    [[NSUserDefaults standardUserDefaults] setInteger:difficulty forKey:kPrefDefaultDifficulty];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

std::string get_default_password() {
    NSString* val = [[NSUserDefaults standardUserDefaults] stringForKey:kPrefDefaultPassword];
    return val ? [val UTF8String] : "";
}

void set_default_password(const std::string& password) {
    NSString* val = [NSString stringWithUTF8String:password.c_str()];
    [[NSUserDefaults standardUserDefaults] setObject:val forKey:kPrefDefaultPassword];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

bool is_mouse_movement_enabled() {
    id obj = [[NSUserDefaults standardUserDefaults] objectForKey:kPrefEnableMouseMovement];
    return (obj != nil) ? [obj boolValue] : true;
}

void set_mouse_movement_enabled(bool enabled) {
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:kPrefEnableMouseMovement];
    [[NSUserDefaults standardUserDefaults] synchronize];
}

void setup_macos_app(SDL_Window* window, const SoundBridgeCallbacks& callbacks) {
    (void)window;
    @autoreleasepool {
        NSDictionary* defaults = @{
            kPrefFullScreenByDefault: @NO,
            kPrefSoundMuted: @NO,
            kPrefSoundVolume: @1.0f,
            kPrefSkipBriefing: @NO,
            kPrefDefaultDifficulty: @0,
            kPrefDefaultPassword: @"",
            kPrefEnableMouseMovement: @YES
        };
        [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];

        if (!s_controller) {
            s_controller = [[TrekMacAppController alloc] init];
        }
        s_controller.callbacks = callbacks;

        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu) return;

        // 1. Hook up the Application Menu's "Settings…" / "Preferences…" item
        if (mainMenu.numberOfItems > 0) {
            NSMenuItem* appMenuItem = [mainMenu itemAtIndex:0];
            if (appMenuItem.submenu) {
                for (NSMenuItem* item in appMenuItem.submenu.itemArray) {
                    if ([item.keyEquivalent isEqualToString:@","] ||
                        [item.title containsString:@"Settings"] ||
                        [item.title containsString:@"Preferences"]) {
                        item.target = s_controller;
                        item.action = @selector(openSettingsWindow:);
                    }
                }
            }
        }

        // 2. Add File Menu (right after Application Menu at index 1)
        NSInteger fileIdx = [mainMenu indexOfItemWithTitle:@"File"];
        if (fileIdx < 0) {
            NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];

            NSMenuItem* newItem = [[NSMenuItem alloc] initWithTitle:@"New Game"
                                                             action:@selector(newGame:)
                                                      keyEquivalent:@"n"];
            newItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            newItem.target = s_controller;
            [fileMenu addItem:newItem];

            NSMenuItem* openItem = [[NSMenuItem alloc] initWithTitle:@"Open…"
                                                              action:@selector(openGame:)
                                                       keyEquivalent:@"o"];
            openItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            openItem.target = s_controller;
            [fileMenu addItem:openItem];

            [fileMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* saveItem = [[NSMenuItem alloc] initWithTitle:@"Save"
                                                              action:@selector(saveGame:)
                                                       keyEquivalent:@"s"];
            saveItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            saveItem.target = s_controller;
            [fileMenu addItem:saveItem];

            NSMenuItem* saveAsItem = [[NSMenuItem alloc] initWithTitle:@"Save As…"
                                                                action:@selector(saveGameAs:)
                                                         keyEquivalent:@"S"];
            saveAsItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
            saveAsItem.target = s_controller;
            [fileMenu addItem:saveAsItem];

            NSMenuItem* fileTopItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
            fileTopItem.submenu = fileMenu;

            if (mainMenu.numberOfItems > 1) {
                [mainMenu insertItem:fileTopItem atIndex:1];
            } else {
                [mainMenu addItem:fileTopItem];
            }
        }

        // 3. Add Controls Menu (between File and Sound)
        NSInteger controlsIdx = [mainMenu indexOfItemWithTitle:@"Controls"];
        if (controlsIdx < 0) {
            NSMenu* controlsMenu = [[NSMenu alloc] initWithTitle:@"Controls"];

            NSMenuItem* sectorItem = [[NSMenuItem alloc] initWithTitle:@"Set Sector Coordinates…"
                                                                action:@selector(enterSectorCoordinates:)
                                                         keyEquivalent:@"k"];
            sectorItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            sectorItem.target = s_controller;
            [controlsMenu addItem:sectorItem];

            NSMenuItem* quadItem = [[NSMenuItem alloc] initWithTitle:@"Set Quadrant Coordinates…"
                                                              action:@selector(enterQuadrantCoordinates:)
                                                       keyEquivalent:@"j"];
            quadItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            quadItem.target = s_controller;
            [controlsMenu addItem:quadItem];

            NSMenuItem* controlsTopItem = [[NSMenuItem alloc] initWithTitle:@"Controls" action:nil keyEquivalent:@""];
            controlsTopItem.submenu = controlsMenu;

            NSInteger filePos = [mainMenu indexOfItemWithTitle:@"File"];
            if (filePos >= 0 && filePos + 1 <= mainMenu.numberOfItems) {
                [mainMenu insertItem:controlsTopItem atIndex:filePos + 1];
            } else {
                [mainMenu addItem:controlsTopItem];
            }
        }

        // 4. Add Sound Menu (between Controls menu and Window menu)
        NSInteger soundIdx = [mainMenu indexOfItemWithTitle:@"Sound"];
        if (soundIdx < 0) {
            NSMenu* soundMenu = [[NSMenu alloc] initWithTitle:@"Sound"];

            NSMenuItem* muteItem = [[NSMenuItem alloc] initWithTitle:@"Mute"
                                                              action:@selector(toggleMute:)
                                                       keyEquivalent:@"m"];
            muteItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
            muteItem.target = s_controller;
            [soundMenu addItem:muteItem];
            s_controller.soundMenuMuteItem = muteItem;

            [soundMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* incItem = [[NSMenuItem alloc] initWithTitle:@"Increase Volume"
                                                             action:@selector(increaseVolume:)
                                                      keyEquivalent:@"="];
            incItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            incItem.target = s_controller;
            [soundMenu addItem:incItem];
            s_controller.soundMenuIncreaseItem = incItem;

            NSMenuItem* decItem = [[NSMenuItem alloc] initWithTitle:@"Decrease Volume"
                                                             action:@selector(decreaseVolume:)
                                                      keyEquivalent:@"-"];
            decItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            decItem.target = s_controller;
            [soundMenu addItem:decItem];
            s_controller.soundMenuDecreaseItem = decItem;

            [soundMenu addItem:[NSMenuItem separatorItem]];

            NSMenuItem* volIndicator = [[NSMenuItem alloc] initWithTitle:@"Volume: 100%"
                                                                  action:nil
                                                           keyEquivalent:@""];
            volIndicator.enabled = NO;
            [soundMenu addItem:volIndicator];
            s_controller.soundMenuVolumeIndicator = volIndicator;

            NSMenuItem* soundTopItem = [[NSMenuItem alloc] initWithTitle:@"Sound" action:nil keyEquivalent:@""];
            soundTopItem.submenu = soundMenu;

            NSInteger windowIdx = [mainMenu indexOfItemWithTitle:@"Window"];
            if (windowIdx >= 0) {
                [mainMenu insertItem:soundTopItem atIndex:windowIdx];
            } else {
                [mainMenu addItem:soundTopItem];
            }
        }

        // 3. Add Help Menu
        NSInteger helpIdx = [mainMenu indexOfItemWithTitle:@"Help"];
        if (helpIdx < 0) {
            NSMenu* helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];

            NSMenuItem* helpItem = [[NSMenuItem alloc] initWithTitle:@"EGA Trek Help"
                                                              action:@selector(openHelpWindow:)
                                                       keyEquivalent:@"?"];
            helpItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            helpItem.target = s_controller;
            [helpMenu addItem:helpItem];

            NSMenuItem* helpTopItem = [[NSMenuItem alloc] initWithTitle:@"Help" action:nil keyEquivalent:@""];
            helpTopItem.submenu = helpMenu;
            [mainMenu addItem:helpTopItem];

            [NSApp setHelpMenu:helpMenu];
        }

        // 4. Ensure menu bar and window controls appear when mouse moves to the top edge in full screen
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillEnterFullScreenNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [NSMenu setMenuBarVisible:YES];
        }];

        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidEnterFullScreenNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [NSMenu setMenuBarVisible:YES];
        }];

        // 5. Run a 60 Hz timer during menu tracking so audio and emulation continue smoothly
        [[NSNotificationCenter defaultCenter] addObserverForName:NSMenuDidBeginTrackingNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [s_controller startMenuTrackingTimer];
        }];

        [[NSNotificationCenter defaultCenter] addObserverForName:NSMenuDidEndTrackingNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            (void)note;
            [s_controller stopMenuTrackingTimer];
        }];

        // Initialize UI with current sound values
        float vol = 1.0f;
        bool muted = false;
        if (callbacks.get_volume) vol = callbacks.get_volume(callbacks.context);
        if (callbacks.is_muted) muted = callbacks.is_muted(callbacks.context);
        [s_controller syncSoundUIWithVolume:vol muted:muted];
    }
}

void sync_macos_sound_ui(float volume, bool muted) {
    if (s_controller) {
        [s_controller syncSoundUIWithVolume:volume muted:muted];
    }
}

void show_settings_window() {
    if (s_controller) {
        [s_controller openSettingsWindow:nil];
    }
}

void show_help_window() {
    if (s_controller) {
        [s_controller openHelpWindow:nil];
    }
}

bool prompt_restore_panel(char* out_path, std::size_t max_len, const char* default_dir) {
    if (!out_path || max_len == 0) return false;
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Restore Saved Game";
        panel.message = @"Select an EGA Trek saved game to restore:";
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.allowedFileTypes = @[ @"egatrek", @"sav", @"SAV" ];
        panel.allowsOtherFileTypes = YES;
        if (default_dir && default_dir[0]) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:default_dir]];
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSString* path = panel.URL.path;
            if (path.length > 0) {
                const char* cPath = [path UTF8String];
                if (!trek::validate_save_file(cPath)) {
                    NSAlert* alert = [[NSAlert alloc] init];
                    alert.messageText = @"Invalid Save File";
                    alert.informativeText = @"The selected file is not a valid EGA Trek save file. Starting a new game.";
                    alert.alertStyle = NSAlertStyleWarning;
                    [alert addButtonWithTitle:@"OK"];
                    [alert runModal];
                    return false;
                }
                std::strncpy(out_path, cPath, max_len - 1);
                out_path[max_len - 1] = '\0';
                return true;
            }
        }
        return false;
    }
}

bool prompt_save_as_panel(char* out_path, std::size_t max_len, const char* default_dir, const char* default_name) {
    if (!out_path || max_len == 0) return false;
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.title = @"Save Game";
        panel.message = @"Choose a name and location to save your game:";
        panel.allowedFileTypes = @[ @"egatrek", @"sav" ];
        panel.allowsOtherFileTypes = YES;
        panel.canCreateDirectories = YES;

        if (default_dir && default_dir[0]) {
            panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:default_dir]];
        }
        if (default_name && default_name[0]) {
            panel.nameFieldStringValue = [NSString stringWithUTF8String:default_name];
        } else {
            panel.nameFieldStringValue = @"TREK.egatrek";
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSString* path = panel.URL.path;
            if (path.length > 0) {
                std::strncpy(out_path, [path UTF8String], max_len - 1);
                out_path[max_len - 1] = '\0';
                return true;
            }
        }
        return false;
    }
}

UnsavedPromptResult prompt_unsaved_changes_alert(const char* action_message) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        if (action_message && action_message[0]) {
            alert.messageText = [NSString stringWithUTF8String:action_message];
        } else {
            alert.messageText = @"Do you want to save the changes you made to this game?";
        }
        alert.informativeText = @"Your progress will be lost if you don't save.";
        alert.alertStyle = NSAlertStyleWarning;

        [alert addButtonWithTitle:@"Save"];       // First button -> NSAlertFirstButtonReturn
        [alert addButtonWithTitle:@"Cancel"];     // Second button -> NSAlertSecondButtonReturn
        [alert addButtonWithTitle:@"Don't Save"]; // Third button -> NSAlertThirdButtonReturn

        if (alert.buttons.count >= 3) {
            [alert.buttons[1] setKeyEquivalent:@"\033"];
            [alert.buttons[2] setKeyEquivalent:@"d"];
            [alert.buttons[2] setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        }

        NSModalResponse res = [alert runModal];
        if (res == NSAlertFirstButtonReturn) return UnsavedPromptResult::Save;
        if (res == NSAlertThirdButtonReturn) return UnsavedPromptResult::DontSave;
        return UnsavedPromptResult::Cancel;
    }
}

void show_alert(const char* title, const char* message) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:title ? title : "Notice"];
        alert.informativeText = [NSString stringWithUTF8String:message ? message : ""];
        alert.alertStyle = NSAlertStyleInformational;
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

}  // namespace trek::macos
