// Native macOS shell for the portable Blazie emulator. Firmware is chosen by the user.
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Carbon/Carbon.h>
#import <CommonCrypto/CommonDigest.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../emu_unit.h"
#include "../chords.h"
#include "../tns_keys.h"
#include "../../../csrc/blazie/bl_state.h"
#include "../../../csrc/blazie/bl_idle.h"
#include "../../../csrc/blazie/bl_files.h"
#include "../../../csrc/blazie/bl_files_state.h"
#include "../../../csrc/blazie/bl_files_xfer.h"
#include "../../../csrc/blazie/fat_img.h"
#include "serial_macos.h"

@class BlazieController;

@interface KeyboardView : NSView
@property (nonatomic, weak) BlazieController *controller;
@end

@interface BlazieController : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuItemValidation> {
    NSWindow *_window;
    NSTextField *_status;
    NSTextField *_display;
    NSTextField *_instructions;
    NSTextField *_serialInfo;
    KeyboardView *_keyboard;
    NSMenuItem *_quickItem;
    NSMenu *_idleMenu;
    NSMenu *_openMenu;
    NSMenuItem *_popClickItem;
    NSMenuItem *_tickItem;
    NSMenuItem *_serialNoneItem;
    NSMenuItem *_serialVirtualItem;
    NSMenuItem *_serialCopyItem;
    NSLock *_lock;
    emu_unit *_unit;
    mac_serial_link *_serialLink;
    NSString *_serialPath;
    NSString *_serialDevicePath;
    NSString *_serialAlias;
    AudioQueueRef _queue;
    atomic_bool _audioRunning;
    BOOL _loading;
    BOOL _quick;
    int _idleSound;
    int _keepOpen;
    BOOL _popClick;
    BOOL _tick;
    BOOL _serialWanted;
    int _kind;
    NSString *_firmwarePath;
    NSString *_statePath;
    chord_state _chord;
    NSMutableSet<NSNumber *> *_tnsDown;
    NSUInteger _loadSerial;
}
- (void)handleKey:(NSEvent *)event down:(BOOL)down;
- (void)handleFlags:(NSEvent *)event;
- (void)releaseKeys;
- (void)renderAudio:(AudioQueueBufferRef)buffer;
- (BOOL)connectVirtualSerial:(BOOL)announce;
- (void)disconnectVirtualSerial:(BOOL)forget;
- (void)refreshSerialMenu;
@end

static BlazieController *gController;

@implementation KeyboardView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }
- (BOOL)isAccessibilityElement { return YES; }
- (NSString *)accessibilityRole { return NSAccessibilityGroupRole; }
- (NSString *)accessibilityLabel { return @"Blazie keyboard. Use the keys for the loaded unit."; }
- (void)keyDown:(NSEvent *)event { [self.controller handleKey:event down:YES]; }
- (void)keyUp:(NSEvent *)event { [self.controller handleKey:event down:NO]; }
- (void)flagsChanged:(NSEvent *)event { [self.controller handleFlags:event]; }
@end

static void audioCallback(void *context, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    (void)queue;
    [(__bridge BlazieController *)context renderAudio:buffer];
}

static int blBit(unsigned short code)
{
    switch (code) {
        case kVK_ANSI_F: return CHORD_DOT(1);
        case kVK_ANSI_D: return CHORD_DOT(2);
        case kVK_ANSI_S: return CHORD_DOT(3);
        case kVK_ANSI_J: return CHORD_DOT(4);
        case kVK_ANSI_K: return CHORD_DOT(5);
        case kVK_ANSI_L: return CHORD_DOT(6);
        case kVK_Space: return CHORD_SPACE;
        case kVK_ANSI_A: case kVK_ANSI_Semicolon: return CHORD_ADVANCE;
        default: return 0;
    }
}

static int tnsCode(NSEvent *event)
{
    const char *name = NULL;
    switch (event.keyCode) {
        case kVK_Return: case kVK_ANSI_KeypadEnter: return tns_code_named("enter");
        case kVK_Delete: return tns_code_named("backspace");
        case kVK_ForwardDelete: return tns_code_named("delete");
        case kVK_Tab: return tns_code_named("tab");
        case kVK_Escape: return tns_code_named("esc");
        case kVK_Space: return tns_code_named("space");
        case kVK_LeftArrow: name = "left"; break;
        case kVK_RightArrow: name = "right"; break;
        case kVK_UpArrow: name = "up"; break;
        case kVK_DownArrow: name = "down"; break;
        case kVK_Home: name = "home"; break;
        case kVK_End: name = "end"; break;
        case kVK_PageUp: name = "pageup"; break;
        case kVK_PageDown: name = "pagedown"; break;
        case kVK_F1: name = "f1"; break;
        case kVK_F2: name = "f2"; break;
        case kVK_F3: name = "f3"; break;
        case kVK_F4: name = "f4"; break;
        case kVK_F5: name = "f5"; break;
        case kVK_F6: name = "f6"; break;
        case kVK_F7: name = "f7"; break;
        case kVK_F8: name = "f8"; break;
        case kVK_F9: name = "f9"; break;
        case kVK_F10: name = "f10"; break;
        case kVK_CapsLock: name = "capslock"; break;
        default: break;
    }
    if (name) return tns_code_named(name);
    NSString *characters = event.charactersIgnoringModifiers.lowercaseString;
    if (characters.length != 1) return 0;
    unichar c = [characters characterAtIndex:0];
    if (c >= 128 || c < 33) return 0;
    char key[2] = {(char)c, 0};
    return tns_code_named(key);
}

static NSString *stateName(NSString *firmware)
{
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    NSData *data = [firmware.stringByStandardizingPath dataUsingEncoding:NSUTF8StringEncoding];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString *name = [NSMutableString stringWithString:firmware.lastPathComponent.stringByDeletingPathExtension.lowercaseString];
    [name appendString:@"-"];
    for (int i = 0; i < 6; i++) [name appendFormat:@"%02x", digest[i]];
    [name appendString:@".state"];
    return name;
}

/* Repair BASIC programs saved by older versions of the IMG importer.  Keep a
   copy of the original memory before the first change. */
static int repairBasicState(NSString *path, char *err, int errlen)
{
    bls_unit saved = {0};
    blf_fs *fs = NULL;
    NSMutableArray<NSString *> *names = [NSMutableArray array];
    int repaired = 0, ok = 0;
    if (!bls_load(path.fileSystemRepresentation, &saved, err, errlen)) return 0;
    fs = blf_open(saved.model, saved.ram, saved.flash, saved.flash_size, err, errlen);
    if (!fs) goto done;
    for (int i = 0; i < blf_count(fs); i++) {
        const blf_file *file = blf_get(fs, i);
        NSString *name = [NSString stringWithUTF8String:file->name];
        if ([name.pathExtension caseInsensitiveCompare:@"bas"] == NSOrderedSame)
            [names addObject:name];
    }
    for (NSString *name in names) {
        int index = blf_find(fs, name.UTF8String);
        unsigned long size;
        const unsigned char *source = index >= 0 ? blf_data(fs, index, &size) : NULL;
        if (!source) { snprintf(err, errlen, "Could not read a BASIC file for repair"); goto done; }
        unsigned char *copy = malloc(size ? (size_t)size : 1);
        if (!copy) { snprintf(err, errlen, "Out of memory repairing BASIC files"); goto done; }
        memcpy(copy, source, (size_t)size);
        int changed = blx_repair_basic(copy, size);
        if (changed) {
            const blf_file *file = blf_get(fs, index);
            unsigned dosTime = file->dos_time, dosDate = file->dos_date;
            if (!blf_replace(fs, index, copy, size, dosTime, dosDate, err, errlen)) {
                free(copy);
                goto done;
            }
            repaired += changed;
        }
        free(copy);
    }
    if (repaired) {
        if (!blf_check(fs, err, errlen)) goto done;
        NSString *backup = [path stringByAppendingString:@".before-basic-repair"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:backup]) {
            NSError *copyError = nil;
            if (![[NSFileManager defaultManager] copyItemAtPath:path toPath:backup error:&copyError]) {
                snprintf(err, errlen, "Could not back up the unit before BASIC repair: %s",
                         copyError.localizedDescription.UTF8String);
                goto done;
            }
        }
        if (!bls_save(path.fileSystemRepresentation, &saved, err, errlen)) goto done;
    }
    ok = 1;
done:
    if (fs) blf_close(fs);
    bls_free(&saved);
    return ok;
}

static NSString *reportText(NSString *summary, const blx_report *report)
{
    if (!report->log) return summary;
    NSString *log = [NSString stringWithUTF8String:report->log];
    if (!log) return summary;
    NSArray<NSString *> *lines = [log componentsSeparatedByString:@"\n"];
    NSUInteger count = MIN((NSUInteger)25, lines.count);
    NSString *preview = [[lines subarrayWithRange:NSMakeRange(0, count)] componentsJoinedByString:@"\n"];
    return [summary stringByAppendingFormat:@"\n\n%@%@", preview, lines.count > 25 ? @"\n…" : @""];
}

static UTType *diskImageType(void)
{
    return [UTType typeWithFilenameExtension:@"img" conformingToType:UTTypeData] ?: UTTypeData;
}

@implementation BlazieController

- (instancetype)init
{
    if ((self = [super init])) {
        _lock = [NSLock new];
        _tnsDown = [NSMutableSet new];
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        id sound = [defaults objectForKey:@"idleSound"];
        id open = [defaults objectForKey:@"keepOpen"];
        _idleSound = sound ? [sound intValue] : BLI_SOUND_BY_VOLUME;
        _keepOpen = open ? [open intValue] : BLI_OPEN_UNTIL_CLICK;
        if (_idleSound < BLI_SOUND_NONE || _idleSound > BLI_SOUND_BY_VOLUME)
            _idleSound = BLI_SOUND_BY_VOLUME;
        if (_keepOpen < BLI_OPEN_OFF || _keepOpen > BLI_OPEN_ALWAYS)
            _keepOpen = BLI_OPEN_UNTIL_CLICK;
        _popClick = [defaults objectForKey:@"popClick"] ? [defaults boolForKey:@"popClick"] : YES;
        _tick = [defaults objectForKey:@"tick"] ? [defaults boolForKey:@"tick"] : YES;
        _quick = [defaults boolForKey:@"quickKeys"];
        _serialWanted = [defaults boolForKey:@"virtualSerialPort"];
        chord_reset(&_chord);
    }
    return self;
}

- (void)status:(NSString *)message
{
    _status.stringValue = message;
    _status.accessibilityLabel = message;
    NSAccessibilityPostNotificationWithUserInfo(NSApp, NSAccessibilityAnnouncementRequestedNotification,
        @{NSAccessibilityAnnouncementKey: message, NSAccessibilityPriorityKey: @(NSAccessibilityPriorityMedium)});
}

- (void)alert:(NSString *)message detail:(NSString *)detail
{
    NSAlert *alert = [NSAlert new];
    alert.messageText = message;
    alert.informativeText = detail ?: @"";
    [alert runModal];
}

- (NSMenuItem *)add:(NSMenu *)menu title:(NSString *)title action:(SEL)action key:(NSString *)key
{
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key ?: @""];
    item.target = self;
    [menu addItem:item];
    return item;
}

- (void)makeMenus
{
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"Blazie" action:nil keyEquivalent:@""];
    [bar addItem:appItem];
    NSMenu *app = [[NSMenu alloc] initWithTitle:@"Blazie"];
    appItem.submenu = app;
    [self add:app title:@"About Blazie" action:@selector(showAbout:) key:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [self add:app title:@"Quit Blazie" action:@selector(quit:) key:@"q"];

    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    [bar addItem:fileItem];
    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    fileItem.submenu = file;
    [self add:file title:@"Open Firmware…" action:@selector(openFirmware:) key:@"o"];
    [self add:file title:@"Save Unit" action:@selector(saveMenu:) key:@"s"];
    [file addItem:[NSMenuItem separatorItem]];
    [self add:file title:@"Export Files to Disk Image…" action:@selector(exportFiles:) key:@""];
    [self add:file title:@"Import Files from Disk Image…" action:@selector(importFiles:) key:@""];

    NSMenuItem *settingsItem = [[NSMenuItem alloc] initWithTitle:@"Settings" action:nil keyEquivalent:@""];
    [bar addItem:settingsItem];
    NSMenu *settings = [[NSMenu alloc] initWithTitle:@"Settings"];
    settingsItem.submenu = settings;
    _quickItem = [self add:settings title:@"Quick Key Response" action:@selector(toggleQuick:) key:@""];
    _quickItem.state = _quick ? NSControlStateValueOn : NSControlStateValueOff;
    [settings addItem:[NSMenuItem separatorItem]];
    NSMenuItem *idleRoot = [[NSMenuItem alloc] initWithTitle:@"Idle Channel Sound" action:nil keyEquivalent:@""];
    _idleMenu = [[NSMenu alloc] initWithTitle:@"Idle Channel Sound"];
    idleRoot.submenu = _idleMenu;
    [settings addItem:idleRoot];
    static const int soundOrder[] = {BLI_SOUND_BY_VOLUME, BLI_SOUND_HISS, BLI_SOUND_WHINE, BLI_SOUND_NONE};
    static NSString *const soundNames[] = {@"No Hiss or Whine", @"Hiss", @"Whine",
                                           @"As the Unit (hiss at even volumes, whine at odd)"};
    for (NSUInteger i = 0; i < sizeof soundOrder / sizeof soundOrder[0]; i++) {
        int sound = soundOrder[i];
        NSMenuItem *item = [self add:_idleMenu title:soundNames[sound] action:@selector(chooseIdleSound:) key:@""];
        item.tag = sound;
    }
    NSMenuItem *openRoot = [[NSMenuItem alloc] initWithTitle:@"Keep Channel Open" action:nil keyEquivalent:@""];
    _openMenu = [[NSMenu alloc] initWithTitle:@"Keep Channel Open"];
    openRoot.submenu = _openMenu;
    [settings addItem:openRoot];
    static NSString *const openNames[] = {@"Only During Speech", @"Until the Unit Clicks Off", @"Always"};
    for (int i = BLI_OPEN_OFF; i <= BLI_OPEN_ALWAYS; i++) {
        NSMenuItem *item = [self add:_openMenu title:openNames[i] action:@selector(chooseKeepOpen:) key:@""];
        item.tag = i;
    }
    [settings addItem:[NSMenuItem separatorItem]];
    _popClickItem = [self add:settings title:@"Pop When Opening and Click When Closing"
                                 action:@selector(togglePopClick:) key:@""];
    _tickItem = [self add:settings title:@"10 Hz Tick" action:@selector(toggleTick:) key:@""];
    [self refreshIdleMenu];
    [settings addItem:[NSMenuItem separatorItem]];
    NSMenuItem *serialRoot = [[NSMenuItem alloc] initWithTitle:@"Serial Port" action:nil keyEquivalent:@""];
    NSMenu *serialMenu = [[NSMenu alloc] initWithTitle:@"Serial Port"];
    serialRoot.submenu = serialMenu;
    [settings addItem:serialRoot];
    _serialNoneItem = [self add:serialMenu title:@"None (Disconnected)" action:@selector(disconnectSerialMenu:) key:@""];
    _serialVirtualItem = [self add:serialMenu title:@"Virtual Port for This Mac" action:@selector(connectSerialMenu:) key:@""];
    [serialMenu addItem:[NSMenuItem separatorItem]];
    _serialCopyItem = [self add:serialMenu title:@"Copy Port Path" action:@selector(copySerialPath:) key:@""];
    [self refreshSerialMenu];

    NSMenuItem *helpItem = [[NSMenuItem alloc] initWithTitle:@"Help" action:nil keyEquivalent:@""];
    [bar addItem:helpItem];
    NSMenu *help = [[NSMenu alloc] initWithTitle:@"Help"];
    helpItem.submenu = help;
    [self add:help title:@"Keyboard Help" action:@selector(showKeys:) key:@""];
    [self add:help title:@"Serial Port Help" action:@selector(showSerialHelp:) key:@""];
    NSApp.mainMenu = bar;
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    SEL action = item.action;
    if (action == @selector(saveMenu:) || action == @selector(exportFiles:) || action == @selector(importFiles:))
        return _unit != NULL && !_loading;
    if (action == @selector(copySerialPath:)) return _serialLink != NULL;
    return YES;
}

- (void)refreshSerialMenu
{
    BOOL connected = _serialLink != NULL;
    _serialNoneItem.state = connected ? NSControlStateValueOff : NSControlStateValueOn;
    _serialVirtualItem.state = connected ? NSControlStateValueOn : NSControlStateValueOff;
    _serialCopyItem.enabled = connected;
    NSString *label = connected ? [NSString stringWithFormat:@"Virtual serial port: %@", _serialPath]
                                : @"Serial port: disconnected";
    _serialInfo.stringValue = label;
    _serialInfo.accessibilityLabel = label;
}

/* Keep a predictable path for terminal and disk programs while the app is open.
   A regular file at that name belongs to the user and is never removed. */
- (NSString *)makeSerialAlias:(NSString *)actual
{
    NSString *folder = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject
                        stringByAppendingPathComponent:@"Blazie"];
    if (![[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES
                                                    attributes:nil error:NULL]) return nil;
    NSString *alias = [folder stringByAppendingPathComponent:@"serial"];
    struct stat st;
    if (lstat(alias.fileSystemRepresentation, &st) == 0) {
        if (!S_ISLNK(st.st_mode)) return nil;
        if (unlink(alias.fileSystemRepresentation) != 0) return nil;
    }
    return symlink(actual.fileSystemRepresentation, alias.fileSystemRepresentation) == 0 ? alias : nil;
}

- (void)removeSerialAlias
{
    if (!_serialAlias) return;
    NSString *target = [[NSFileManager defaultManager] destinationOfSymbolicLinkAtPath:_serialAlias error:NULL];
    if ([target isEqualToString:_serialDevicePath]) unlink(_serialAlias.fileSystemRepresentation);
    _serialAlias = nil;
}

- (BOOL)connectVirtualSerial:(BOOL)announce
{
    if (_serialLink) {
        if (announce) [self status:[NSString stringWithFormat:@"Virtual serial port ready at %@", _serialPath]];
        return YES;
    }
    char path[256], error[512] = {0};
    mac_serial_link *link = mac_serial_open(path, sizeof path, error, sizeof error);
    if (!link) {
        NSString *reason = [NSString stringWithUTF8String:error];
        [self status:@"Could not open the virtual serial port"];
        if (announce) [self alert:@"Could not open the virtual serial port" detail:reason];
        return NO;
    }
    NSString *actual = [NSString stringWithUTF8String:path];
    NSString *alias = [self makeSerialAlias:actual];
    [_lock lock];
    int attached = !_unit || emu_serial_attach(_unit, 1);
    if (attached) _serialLink = link;
    [_lock unlock];
    if (!attached) {
        if (alias) unlink(alias.fileSystemRepresentation);
        mac_serial_close(link);
        [self alert:@"Could not connect the serial port" detail:@"The emulator ran out of memory."];
        return NO;
    }
    _serialAlias = alias;
    _serialDevicePath = actual;
    _serialPath = alias ?: actual;
    _serialWanted = YES;
    [[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"virtualSerialPort"];
    [self refreshSerialMenu];
    if (announce) [self status:[NSString stringWithFormat:@"Virtual serial port ready at %@", _serialPath]];
    return YES;
}

- (void)disconnectVirtualSerial:(BOOL)forget
{
    [_lock lock];
    if (_serialLink && _unit) emu_serial_attach(_unit, 0);
    mac_serial_link *old = _serialLink;
    _serialLink = NULL;
    [_lock unlock];
    mac_serial_close(old);
    [self removeSerialAlias];
    _serialPath = nil;
    _serialDevicePath = nil;
    if (forget) {
        _serialWanted = NO;
        [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"virtualSerialPort"];
    }
    [self refreshSerialMenu];
}

- (void)connectSerialMenu:(id)sender
{
    (void)sender;
    [self connectVirtualSerial:YES];
    [_window makeFirstResponder:_keyboard];
}

- (void)disconnectSerialMenu:(id)sender
{
    (void)sender;
    [self disconnectVirtualSerial:YES];
    [self status:@"Serial port disconnected"];
    [_window makeFirstResponder:_keyboard];
}

- (void)copySerialPath:(id)sender
{
    (void)sender;
    if (!_serialPath) { NSBeep(); return; }
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    [pasteboard setString:_serialPath forType:NSPasteboardTypeString];
    [self status:@"Serial port path copied"];
    [_window makeFirstResponder:_keyboard];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [self makeMenus];
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 620, 330)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Blazie";
    _window.minSize = NSMakeSize(500, 270);
    _window.delegate = self;
    [_window center];

    NSView *content = _window.contentView;
    _keyboard = [[KeyboardView alloc] initWithFrame:NSMakeRect(0, 0, 620, 330)];
    _keyboard.controller = self;
    _keyboard.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:_keyboard];

    _instructions = [NSTextField labelWithString:@"Open a .BNS or .TNS firmware file to start a unit."];
    _instructions.frame = NSMakeRect(24, 235, 570, 70);
    _instructions.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    _instructions.font = [NSFont systemFontOfSize:15];
    _instructions.maximumNumberOfLines = 4;
    [_keyboard addSubview:_instructions];

    _display = [NSTextField labelWithString:@""];
    _display.frame = NSMakeRect(24, 130, 570, 75);
    _display.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    _display.font = [NSFont systemFontOfSize:30];
    _display.accessibilityLabel = @"Braille display";
    [_keyboard addSubview:_display];

    _serialInfo = [NSTextField labelWithString:@"Serial port: disconnected"];
    _serialInfo.frame = NSMakeRect(24, 78, 570, 40);
    _serialInfo.autoresizingMask = NSViewWidthSizable;
    _serialInfo.maximumNumberOfLines = 2;
    [_keyboard addSubview:_serialInfo];

    _status = [NSTextField labelWithString:@"No unit loaded"];
    _status.frame = NSMakeRect(24, 24, 570, 42);
    _status.autoresizingMask = NSViewWidthSizable;
    _status.maximumNumberOfLines = 2;
    _status.accessibilityLabel = @"No unit loaded";
    [_keyboard addSubview:_status];

    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_keyboard];
    if (_serialWanted) [self connectVirtualSerial:NO];
    [NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(refreshDisplay:) userInfo:nil repeats:YES];
    [NSTimer scheduledTimerWithTimeInterval:60 target:self selector:@selector(autosave:) userInfo:nil repeats:YES];
    NSString *previous = [[NSUserDefaults standardUserDefaults] stringForKey:@"lastFirmware"];
    if (previous && [[NSFileManager defaultManager] fileExistsAtPath:previous]) [self loadFirmware:previous];
}

- (void)openFirmware:(id)sender
{
    (void)sender;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Choose a Braille Lite, Braille ’n Speak, or Type ’n Speak firmware update file.";
    if ([panel runModal] == NSModalResponseOK) [self loadFirmware:panel.URL.path];
}

- (void)loadFirmware:(NSString *)path
{
    NSString *ext = path.pathExtension.lowercaseString;
    if (![@[@"bns", @"tns"] containsObject:ext]) {
        [self alert:@"Unsupported firmware" detail:@"Choose a .BNS or .TNS firmware file."];
        return;
    }
    if (_loading) return;
    _loading = YES;
    NSUInteger serial = ++_loadSerial;
    int kind = [ext isEqualToString:@"tns"] ? EMU_TYPE_N_SPEAK : EMU_BRAILLE_LITE;
    NSString *support = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject
                         stringByAppendingPathComponent:@"Blazie"];
    NSError *directoryError = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:support withIntermediateDirectories:YES attributes:nil error:&directoryError]) {
        _loading = NO;
        [self alert:@"Cannot create the memory folder" detail:directoryError.localizedDescription];
        return;
    }
    NSString *state = [support stringByAppendingPathComponent:stateName(path)];
    [self status:@"Preparing the unit…"];
    _instructions.stringValue = @"Preparing firmware and memory. A first Braille Lite start may take several minutes.";
    int idleSound = _idleSound, keepOpen = _keepOpen;
    BOOL popClick = _popClick, tick = _tick, quick = _quick;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        char error[512] = {0};
        if (kind == EMU_BRAILLE_LITE && ![[NSFileManager defaultManager] fileExistsAtPath:state]) {
            NSString *temp = [state stringByAppendingString:@".creating"];
            BOOL spanish = [path.lastPathComponent.uppercaseString containsString:@"SPA"];
            if (!blv_make_state(path.fileSystemRepresentation, spanish ? BLV_STATE_SPANISH : BLV_STATE_ENGLISH,
                                temp.fileSystemRepresentation, NULL, NULL, error, sizeof error) ||
                rename(temp.fileSystemRepresentation, state.fileSystemRepresentation) != 0) {
                unlink(temp.fileSystemRepresentation);
                if (!error[0]) snprintf(error, sizeof error, "Could not write the factory memory");
            }
        }
        if (!error[0] && kind == EMU_BRAILLE_LITE &&
                [[NSFileManager defaultManager] fileExistsAtPath:state] &&
                ![state isEqualToString:self->_statePath])
            repairBasicState(state, error, sizeof error);
        emu_unit *newUnit = NULL;
        if (!error[0]) newUnit = emu_create(kind, path.fileSystemRepresentation,
                                           [[NSFileManager defaultManager] fileExistsAtPath:state] ? state.fileSystemRepresentation : NULL,
                                           44100.0, idleSound, error, sizeof error);
        if (newUnit) {
            if (kind == EMU_BRAILLE_LITE) emu_set_idle(newUnit, idleSound, keepOpen, popClick, tick);
            emu_set_quick(newUnit, quick);
        }
        NSString *reason = error[0] ? [NSString stringWithUTF8String:error] : @"Could not start the unit";
        dispatch_async(dispatch_get_main_queue(), ^{
            if (serial != self->_loadSerial) { if (newUnit) emu_destroy(newUnit); return; }
            self->_loading = NO;
            if (!newUnit) {
                [self status:@"Could not start the unit"];
                [self alert:@"Could not start the unit" detail:reason];
                return;
            }
            [self stopAudio];
            if (![self saveUnit:NO]) {
                emu_destroy(newUnit);
                [self startAudio];
                [self alert:@"Could not switch units" detail:@"The current unit's memory could not be saved."];
                return;
            }
            [self->_lock lock];
            if (self->_serialLink && !emu_serial_attach(newUnit, 1)) {
                [self->_lock unlock];
                emu_destroy(newUnit);
                [self startAudio];
                [self alert:@"Could not switch units" detail:@"The serial connection could not attach to the new unit."];
                return;
            }
            if (self->_unit) emu_destroy(self->_unit);
            self->_unit = newUnit;
            mac_serial_reset(self->_serialLink);
            self->_kind = kind;
            self->_firmwarePath = [path copy];
            self->_statePath = [state copy];
            if (kind == EMU_BRAILLE_LITE)
                emu_set_idle(self->_unit, self->_idleSound, self->_keepOpen, self->_popClick, self->_tick);
            emu_set_quick(self->_unit, self->_quick);
            chord_reset(&self->_chord);
            [self->_tnsDown removeAllObjects];
            [self->_lock unlock];
            [[NSUserDefaults standardUserDefaults] setObject:path forKey:@"lastFirmware"];
            self->_window.title = [NSString stringWithFormat:@"Blazie — %@", path.lastPathComponent];
            self->_instructions.stringValue = kind == EMU_TYPE_N_SPEAK
                ? @"Type ’n Speak: use the keyboard directly. A new unit asks seven setup questions; answer y (Spanish: s)."
                : @"Braille keys: F D S = dots 1–3, J K L = dots 4–6, Space = space, A or ; = advance.";
            [self status:[NSString stringWithFormat:@"Running %@", path.lastPathComponent]];
            [self startAudio];
            [self->_window makeFirstResponder:self->_keyboard];
        });
    });
}

- (void)startAudio
{
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = 44100;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 16;
    format.mBytesPerFrame = 2;
    format.mBytesPerPacket = 2;
    OSStatus result = AudioQueueNewOutput(&format, audioCallback, (__bridge void *)self, NULL, NULL, 0, &_queue);
    if (result != noErr) {
        _queue = NULL;
        [self status:@"Audio device unavailable; unit is paused"];
        return;
    }
    atomic_store(&_audioRunning, true);
    for (int i = 0; i < 4; i++) {
        AudioQueueBufferRef buffer = NULL;
        if (AudioQueueAllocateBuffer(_queue, 1764, &buffer) != noErr) break;
        [self renderAudio:buffer];
    }
    if (AudioQueueStart(_queue, NULL) != noErr) {
        [self stopAudio];
        [self status:@"Could not start audio; unit is paused"];
    }
}

- (void)stopAudio
{
    if (!_queue) return;
    atomic_store(&_audioRunning, false);
    AudioQueueStop(_queue, true);
    AudioQueueDispose(_queue, true);
    _queue = NULL;
}

- (void)renderAudio:(AudioQueueBufferRef)buffer
{
    if (!atomic_load(&_audioRunning) || !_queue) return;
    [_lock lock];
    if (_unit) {
        emu_render(_unit, (short *)buffer->mAudioData, (int)(buffer->mAudioDataBytesCapacity / 2));
        mac_serial_pump(_serialLink, _unit);
    } else memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
    [_lock unlock];
    buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    AudioQueueEnqueueBuffer(_queue, buffer, 0, NULL);
}

- (BOOL)saveUnit:(BOOL)announce
{
    if (!_unit || !_statePath) return YES;
    NSString *temp = [_statePath stringByAppendingString:@".saving"];
    [_lock lock];
    int ok = emu_save(_unit, temp.fileSystemRepresentation);
    [_lock unlock];
    if (ok) ok = rename(temp.fileSystemRepresentation, _statePath.fileSystemRepresentation) == 0;
    if (!ok) unlink(temp.fileSystemRepresentation);
    if (announce) [self status:ok ? @"Unit memory saved" : @"Could not save unit memory"];
    return ok;
}

- (void)saveMenu:(id)sender { (void)sender; [self saveUnit:YES]; }
- (void)autosave:(NSTimer *)timer { (void)timer; [self saveUnit:NO]; }

- (int)fileNameCodepage
{
    return [_firmwarePath.lastPathComponent.uppercaseString isEqualToString:@"BS2SLL.BNS"] ? BLX_CP852 : BLX_CP850;
}

- (void)exportFiles:(id)sender
{
    (void)sender;
    if (!_unit || _loading) { NSBeep(); return; }
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = @[diskImageType()];
    panel.nameFieldStringValue = [NSString stringWithFormat:@"%@ files.img", _firmwarePath.lastPathComponent.stringByDeletingPathExtension];
    panel.message = @"Export the unit's files to a FAT disk image.";
    if ([panel runModal] != NSModalResponseOK) return;
    if (![self saveUnit:NO]) {
        [self alert:@"Could not export files" detail:@"The unit's current memory could not be saved."];
        return;
    }

    char error[512] = {0};
    bls_unit saved = {0};
    if (!bls_load(_statePath.fileSystemRepresentation, &saved, error, sizeof error)) {
        [self alert:@"Could not export files" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    blf_fs *fs = blf_open(saved.model, saved.ram, saved.flash, saved.flash_size, error, sizeof error);
    if (!fs) {
        bls_free(&saved);
        [self alert:@"Could not export files" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    blx_report report;
    blx_report_init(&report);
    unsigned long size = 0;
    unsigned char *bytes = blx_export_cp(fs, saved.model, [self fileNameCodepage], &size,
                                         &report, error, sizeof error);
    blf_close(fs);
    bls_free(&saved);
    if (!bytes) {
        blx_report_free(&report);
        [self alert:@"Could not export files" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    NSData *image = [NSData dataWithBytesNoCopy:bytes length:(NSUInteger)size freeWhenDone:YES];
    NSError *writeError = nil;
    if (![image writeToURL:panel.URL options:NSDataWritingAtomic error:&writeError]) {
        blx_report_free(&report);
        [self alert:@"Could not export files" detail:writeError.localizedDescription];
        return;
    }
    NSString *summary = [NSString stringWithFormat:@"Exported %d files to %@.", report.exported, panel.URL.path];
    NSString *detail = reportText(summary, &report);
    int exported = report.exported;
    blx_report_free(&report);
    [self status:[NSString stringWithFormat:@"Exported %d files", exported]];
    [self alert:@"Files exported" detail:detail];
    [_window makeFirstResponder:_keyboard];
}

- (void)importFiles:(id)sender
{
    (void)sender;
    if (!_unit || _loading) { NSBeep(); return; }
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedContentTypes = @[diskImageType()];
    panel.message = @"Import files from a FAT disk image. Files missing from the image may be deleted from the unit.";
    if ([panel runModal] != NSModalResponseOK) return;

    NSError *readError = nil;
    NSData *image = [NSData dataWithContentsOfURL:panel.URL options:NSDataReadingMappedIfSafe error:&readError];
    if (!image) {
        [self alert:@"Could not import files" detail:readError.localizedDescription];
        return;
    }
    char error[512] = {0};
    fat_volume *volume = fat_read(image.bytes, (unsigned long)image.length, error, sizeof error);
    if (!volume) {
        [self alert:@"Could not import files" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    fat_close(volume);

    [self releaseKeys];
    [self stopAudio];
    [_lock lock];
    int flashBusy = emu_flash(_unit, NULL);
    [_lock unlock];
    if (flashBusy) {
        [self startAudio];
        [self alert:@"Unit is writing flash memory" detail:@"Import when the unit has finished and is quiet."];
        return;
    }
    if (![self saveUnit:NO]) {
        [self startAudio];
        [self alert:@"Could not import files" detail:@"The unit's current memory could not be saved. Nothing was imported."];
        return;
    }

    bls_unit saved = {0};
    if (!bls_load(_statePath.fileSystemRepresentation, &saved, error, sizeof error)) {
        [self startAudio];
        [self alert:@"Could not import files" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    blf_fs *fs = blf_open(saved.model, saved.ram, saved.flash, saved.flash_size, error, sizeof error);
    if (!fs) {
        bls_free(&saved);
        [self startAudio];
        [self alert:@"Could not import files" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    blx_report report;
    blx_report_init(&report);
    int ok = blx_import_cp(fs, [self fileNameCodepage], image.bytes, (unsigned long)image.length,
                           &report, error, sizeof error) && blf_check(fs, error, sizeof error);
    blf_close(fs);
    if (!ok) {
        NSString *detail = reportText([NSString stringWithUTF8String:error], &report);
        blx_report_free(&report);
        bls_free(&saved);
        [self startAudio];
        [self alert:@"Could not import files" detail:detail];
        return;
    }
    if (report.deleted) {
        NSMutableString *detail = [NSMutableString stringWithFormat:
            @"The image is missing %d of the unit's files. Importing it will delete them from the unit.\n\n", report.deleted];
        NSString *log = report.log ? [NSString stringWithUTF8String:report.log] : @"";
        int shown = 0;
        for (NSString *line in [log componentsSeparatedByString:@"\n"])
            if ([line hasPrefix:@"deleted "] && shown++ < 25)
                [detail appendFormat:@"%@\n", [line substringFromIndex:8]];
        if (report.deleted > 25) [detail appendString:@"…and more files.\n"];
        NSAlert *confirm = [NSAlert new];
        confirm.messageText = @"Import and delete missing files?";
        confirm.informativeText = detail;
        [confirm addButtonWithTitle:@"Import and Delete"];
        [confirm addButtonWithTitle:@"Cancel"];
        if ([confirm runModal] != NSAlertFirstButtonReturn) {
            blx_report_free(&report);
            bls_free(&saved);
            [self startAudio];
            [self status:@"Nothing was imported"];
            return;
        }
    }

    NSString *tempState = [_statePath stringByAppendingString:@".importing"];
    NSString *backup = [_statePath stringByAppendingString:@".before-import"];
    NSData *original = [NSData dataWithContentsOfFile:_statePath options:NSDataReadingMappedIfSafe error:&readError];
    if (!original || ![original writeToFile:backup options:NSDataWritingAtomic error:&readError]) {
        blx_report_free(&report);
        bls_free(&saved);
        [self startAudio];
        [self alert:@"Could not back up unit memory" detail:readError.localizedDescription];
        return;
    }
    if (!bls_save(tempState.fileSystemRepresentation, &saved, error, sizeof error)) {
        blx_report_free(&report);
        bls_free(&saved);
        [self startAudio];
        [self alert:@"Could not import files" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    bls_free(&saved);
    emu_unit *newUnit = emu_create(_kind, _firmwarePath.fileSystemRepresentation, tempState.fileSystemRepresentation,
                                   44100.0, _idleSound, error, sizeof error);
    if (!newUnit) {
        unlink(tempState.fileSystemRepresentation);
        blx_report_free(&report);
        [self startAudio];
        [self alert:@"Could not restart the unit" detail:[NSString stringWithUTF8String:error]];
        return;
    }
    if (_kind == EMU_BRAILLE_LITE) emu_set_idle(newUnit, _idleSound, _keepOpen, _popClick, _tick);
    emu_set_quick(newUnit, _quick);
    [_lock lock];
    int serialAttached = !_serialLink || emu_serial_attach(newUnit, 1);
    [_lock unlock];
    if (!serialAttached) {
        emu_destroy(newUnit);
        unlink(tempState.fileSystemRepresentation);
        blx_report_free(&report);
        [self startAudio];
        [self alert:@"Could not restart the unit" detail:@"The serial connection could not attach to it."];
        return;
    }
    if (rename(tempState.fileSystemRepresentation, _statePath.fileSystemRepresentation) != 0) {
        emu_destroy(newUnit);
        unlink(tempState.fileSystemRepresentation);
        blx_report_free(&report);
        [self startAudio];
        [self alert:@"Could not replace unit memory" detail:@"The original unit is still running, and its backup is available beside the state file."];
        return;
    }
    [_lock lock];
    emu_unit *oldUnit = _unit;
    _unit = newUnit;
    mac_serial_reset(_serialLink);
    chord_reset(&_chord);
    [_tnsDown removeAllObjects];
    [_lock unlock];
    emu_destroy(oldUnit);
    [self startAudio];
    NSString *summary = [NSString stringWithFormat:
        @"Imported from %@: %d added, %d rewritten, %d moved, %d deleted, %d unchanged, %d kept, %d skipped. The unit restarted. Its previous memory is saved at %@.",
        panel.URL.path, report.added, report.replaced, report.moved, report.deleted, report.unchanged,
        report.kept, report.skipped, backup];
    NSString *detail = reportText(summary, &report);
    int added = report.added, replaced = report.replaced, deleted = report.deleted;
    blx_report_free(&report);
    [self status:[NSString stringWithFormat:@"Imported: %d added, %d rewritten, %d deleted", added, replaced, deleted]];
    [self alert:@"Files imported" detail:detail];
    [_window makeFirstResponder:_keyboard];
}

- (void)refreshDisplay:(NSTimer *)timer
{
    (void)timer;
    if (!_unit || _kind == EMU_TYPE_N_SPEAK) { _display.stringValue = @""; return; }
    unsigned char cells[40] = {0};
    [_lock lock];
    int count = emu_braille(_unit, cells, 40);
    [_lock unlock];
    NSMutableString *braille = [NSMutableString new];
    for (int i = 0; i < count; i++) [braille appendFormat:@"%C", (unichar)(0x2800 + cells[i])];
    _display.stringValue = braille;
    _display.accessibilityValue = braille;
}

- (void)sendTns:(int)code down:(BOOL)down
{
    if (!code) return;
    NSNumber *number = @(code);
    if (down) {
        if ([_tnsDown containsObject:number]) return;
        [_tnsDown addObject:number];
    } else {
        if (![_tnsDown containsObject:number]) return;
        [_tnsDown removeObject:number];
    }
    [_lock lock];
    if (_unit) emu_key(_unit, down ? code : (code & 0x7f));
    [_lock unlock];
}

- (void)handleKey:(NSEvent *)event down:(BOOL)down
{
    if (!_unit) { if (down) NSBeep(); return; }
    if (down && event.isARepeat) return;
    if (_kind == EMU_TYPE_N_SPEAK) {
        [self sendTns:tnsCode(event) down:down];
        return;
    }
    int bit = blBit(event.keyCode);
    if (!bit) return;
    [_lock lock];
    if (down) chord_down(&_chord, bit);
    else {
        int finished = chord_up(&_chord, bit);
        if (finished) emu_key(_unit, finished);
    }
    emu_keys_down(_unit, _chord.down);
    [_lock unlock];
}

- (void)handleFlags:(NSEvent *)event
{
    if (!_unit || _kind != EMU_TYPE_N_SPEAK) return;
    int code = 0;
    NSEventModifierFlags mask = 0;
    switch (event.keyCode) {
        case kVK_Shift: code = tns_code_named("lshift"); mask = NSEventModifierFlagShift; break;
        case kVK_RightShift: code = tns_code_named("rshift"); mask = NSEventModifierFlagShift; break;
        case kVK_Control: case kVK_RightControl: code = tns_code_named("ctrl"); mask = NSEventModifierFlagControl; break;
        case kVK_Option: code = tns_code_named("lalt"); mask = NSEventModifierFlagOption; break;
        case kVK_RightOption: code = tns_code_named("ralt"); mask = NSEventModifierFlagOption; break;
        default: return;
    }
    // AppKit reports each physical modifier's transition with its key code. The
    // aggregate mask stays set when the opposite Shift or Option key is held.
    (void)mask;
    [self sendTns:code down:![_tnsDown containsObject:@(code)]];
}

- (void)releaseKeys
{
    [_lock lock];
    if (_unit) {
        if (_kind == EMU_TYPE_N_SPEAK) for (NSNumber *number in _tnsDown) emu_key(_unit, number.intValue & 0x7f);
        else emu_keys_down(_unit, 0);
    }
    chord_reset(&_chord);
    [_tnsDown removeAllObjects];
    [_lock unlock];
}

- (void)windowDidResignKey:(NSNotification *)notification { (void)notification; [self releaseKeys]; }
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender; [NSApp terminate:self]; return NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    [self stopAudio];
    if (![self saveUnit:NO]) {
        [self startAudio];
        [self alert:@"Could not quit Blazie" detail:@"The unit's memory could not be saved. The app remains open so you can try again."];
        return NSTerminateCancel;
    }
    [_lock lock];
    if (_unit) { emu_destroy(_unit); _unit = NULL; }
    [_lock unlock];
    [self disconnectVirtualSerial:NO];
    return NSTerminateNow;
}

- (void)toggleQuick:(id)sender
{
    (void)sender;
    _quick = !_quick;
    _quickItem.state = _quick ? NSControlStateValueOn : NSControlStateValueOff;
    [[NSUserDefaults standardUserDefaults] setBool:_quick forKey:@"quickKeys"];
    [_lock lock];
    if (_unit) emu_set_quick(_unit, _quick);
    [_lock unlock];
}

- (void)refreshIdleMenu
{
    for (NSMenuItem *item in _idleMenu.itemArray)
        item.state = item.tag == _idleSound ? NSControlStateValueOn : NSControlStateValueOff;
    for (NSMenuItem *item in _openMenu.itemArray)
        item.state = item.tag == _keepOpen ? NSControlStateValueOn : NSControlStateValueOff;
    _popClickItem.state = _popClick ? NSControlStateValueOn : NSControlStateValueOff;
    _tickItem.state = _tick ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)saveIdleSettings
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:_idleSound forKey:@"idleSound"];
    [defaults setInteger:_keepOpen forKey:@"keepOpen"];
    [defaults setBool:_popClick forKey:@"popClick"];
    [defaults setBool:_tick forKey:@"tick"];
}

- (void)applyIdleSettings
{
    int ok = 1;
    [_lock lock];
    if (_unit && _kind == EMU_BRAILLE_LITE)
        ok = emu_set_idle(_unit, _idleSound, _keepOpen, _popClick, _tick);
    [_lock unlock];
    if (!ok) [self alert:@"Could not change idle channel" detail:@"The emulator ran out of memory."];
}

- (void)chooseIdleSound:(NSMenuItem *)item
{
    _idleSound = (int)item.tag;
    [self refreshIdleMenu];
    [self applyIdleSettings];
    [self saveIdleSettings];
    static NSString *const names[] = {@"no hiss or whine", @"hiss", @"whine", @"as the unit"};
    [self status:[NSString stringWithFormat:@"Idle channel: %@", names[_idleSound]]];
}

- (void)chooseKeepOpen:(NSMenuItem *)item
{
    _keepOpen = (int)item.tag;
    [self refreshIdleMenu];
    [self applyIdleSettings];
    [self saveIdleSettings];
    static NSString *const names[] = {@"only during speech", @"until the unit clicks off", @"always"};
    [self status:[NSString stringWithFormat:@"Keep channel open: %@", names[_keepOpen]]];
}

- (void)togglePopClick:(id)sender
{
    (void)sender;
    _popClick = !_popClick;
    [self refreshIdleMenu];
    [self applyIdleSettings];
    [self saveIdleSettings];
    [self status:[NSString stringWithFormat:@"Pop and click: %@", _popClick ? @"on" : @"off"]];
}

- (void)toggleTick:(id)sender
{
    (void)sender;
    _tick = !_tick;
    [self refreshIdleMenu];
    [self applyIdleSettings];
    [self saveIdleSettings];
    [self status:[NSString stringWithFormat:@"10 Hz tick: %@", _tick ? @"on" : @"off"]];
}

- (void)showKeys:(id)sender
{
    (void)sender;
    [self alert:@"Keyboard"
          detail:@"Braille Lite and Braille ’n Speak: F D S are dots 1–3, J K L are dots 4–6. Hold keys together and release the last key to send a chord. Space is the space bar; A or ; is the Braille Lite advance bar. Type ’n Speak uses the normal keyboard, including its function keys. macOS command shortcuts remain available in the menu bar."];
}

- (void)showSerialHelp:(id)sender
{
    (void)sender;
    [self alert:@"Virtual serial port"
          detail:@"Choose Settings → Serial Port → Virtual Port for This Mac. Copy the displayed path and select it as the serial device in your terminal or disk program. The path remains the same while Blazie is open. The unit controls its own serial settings and must enable its serial port or start a storage command before it sends data. Choose None to disconnect."];
}

- (void)showAbout:(id)sender
{
    (void)sender;
    [self alert:@"Blazie for macOS"
          detail:@"A native macOS shell for the Blazie emulator. It runs user supplied firmware on the project's portable emulation core and saves each unit in Application Support/Blazie. Firmware is not included."];
}
- (void)quit:(id)sender { (void)sender; [NSApp terminate:self]; }
@end

int main(int argc, const char *argv[])
{
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        gController = [BlazieController new];
        app.delegate = gController;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
