#import <Cocoa/Cocoa.h>
#include <cmath>

#include "app/macos/macos_bridge.h"

static NSString* const kPrefFullScreenByDefault = @"FullScreenByDefault";
static NSString* const kPrefSoundMuted = @"SoundMuted";
static NSString* const kPrefSoundVolume = @"SoundVolume";

@interface TrekMacAppController : NSObject <NSWindowDelegate>

@property (nonatomic, strong, nullable) NSWindow* settingsWindow;
@property (nonatomic, strong, nullable) NSButton* fullScreenCheckbox;
@property (nonatomic, strong, nullable) NSButton* settingsMuteCheckbox;
@property (nonatomic, strong, nullable) NSSlider* settingsVolumeSlider;
@property (nonatomic, strong, nullable) NSTextField* settingsVolumeLabel;

@property (nonatomic, strong, nullable) NSWindow* helpWindow;

@property (nonatomic, strong, nullable) NSMenuItem* soundMenuMuteItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuIncreaseItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuDecreaseItem;
@property (nonatomic, strong, nullable) NSMenuItem* soundMenuVolumeIndicator;
@property (nonatomic, strong, nullable) NSTimer* trackingTimer;

@property (nonatomic, assign) trek::macos::SoundBridgeCallbacks callbacks;

- (void)openSettingsWindow:(nullable id)sender;
- (void)openHelpWindow:(nullable id)sender;
- (void)toggleMute:(nullable id)sender;
- (void)increaseVolume:(nullable id)sender;
- (void)decreaseVolume:(nullable id)sender;
- (void)syncSoundUIWithVolume:(float)vol muted:(BOOL)muted;
- (void)startMenuTrackingTimer;
- (void)stopMenuTrackingTimer;

@end

static TrekMacAppController* s_controller = nil;

@implementation TrekMacAppController

- (void)openSettingsWindow:(nullable id)sender {
    if (!self.settingsWindow) {
        [self buildSettingsWindow];
    }
    
    const BOOL isFs = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
    self.fullScreenCheckbox.state = isFs ? NSControlStateValueOn : NSControlStateValueOff;
    
    float vol = 1.0f;
    bool muted = false;
    if (self.callbacks.get_volume) vol = self.callbacks.get_volume(self.callbacks.context);
    if (self.callbacks.is_muted) muted = self.callbacks.is_muted(self.callbacks.context);
    [self syncSoundUIWithVolume:vol muted:muted];
    
    [self.settingsWindow makeKeyAndOrderFront:sender];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)buildSettingsWindow {
    const NSRect frame = NSMakeRect(0, 0, 440, 220);
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
    displayHeader.frame = NSMakeRect(20, 175, 400, 20);
    [content addSubview:displayHeader];

    NSButton* fsCheck = [NSButton checkboxWithTitle:@"Open in Full Screen by default"
                                             target:self
                                             action:@selector(fullScreenCheckboxChanged:)];
    fsCheck.frame = NSMakeRect(30, 145, 380, 24);
    const BOOL fsVal = [[NSUserDefaults standardUserDefaults] boolForKey:kPrefFullScreenByDefault];
    fsCheck.state = fsVal ? NSControlStateValueOn : NSControlStateValueOff;
    [content addSubview:fsCheck];
    self.fullScreenCheckbox = fsCheck;

    NSTextField* fsHint = [NSTextField labelWithString:@"When enabled, EGA Trek opens directly into full screen upon launch."];
    fsHint.font = [NSFont systemFontOfSize:11];
    fsHint.textColor = [NSColor secondaryLabelColor];
    fsHint.frame = NSMakeRect(48, 128, 370, 16);
    [content addSubview:fsHint];

    // Separator line
    NSBox* separator = [[NSBox alloc] initWithFrame:NSMakeRect(20, 118, 400, 1)];
    separator.boxType = NSBoxSeparator;
    [content addSubview:separator];

    // --- Audio Section ---
    NSTextField* audioHeader = [NSTextField labelWithString:@"Game Audio"];
    audioHeader.font = [NSFont boldSystemFontOfSize:13];
    audioHeader.frame = NSMakeRect(20, 90, 400, 20);
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
        @"  SAVE        Save game state to disk\n"
        @"  Q)uit       Quit game\n"
        @"  Shift-F1    Boss Mode (instant DOS prompt cover screen)\n"
        @"  Cmd+Q       Quit application\n"
        @"  Ctrl+Cmd+F  Toggle Full Screen\n"
        @"  Cmd+,       Open Settings\n"
        @"  Cmd+Shift+M Toggle Sound Mute\n"
        @"  Cmd+=       Increase Sound Volume\n"
        @"  Cmd+-       Decrease Sound Volume\n\n"
        @"------------------------------------------------------------\n"
        @"[Help Placeholder]\n"
        @"Interactive tutorials, tactical mission guides, and detailed star\n"
        @"fleet manuals are currently under development and will be provided\n"
        @"in upcoming releases.\n";

    text.string = helpText;
    scroll.documentView = text;
    [content addSubview:scroll];

    // Close button
    NSButton* closeBtn = [NSButton buttonWithTitle:@"Close" target:win action:@selector(performClose:)];
    closeBtn.frame = NSMakeRect(450, 15, 90, 30);
    closeBtn.keyEquivalent = @"\r";
    closeBtn.autoresizingMask = NSViewMinYMargin | NSViewMinXMargin;
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

void setup_macos_app(SDL_Window* window, const SoundBridgeCallbacks& callbacks) {
    (void)window;
    @autoreleasepool {
        NSDictionary* defaults = @{
            kPrefFullScreenByDefault: @NO,
            kPrefSoundMuted: @NO,
            kPrefSoundVolume: @1.0f
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

        // 2. Add Sound Menu (between App menu and Window menu)
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

}  // namespace trek::macos
