// Opt-in, read-only YouTube Music guest telemetry for MediaBloom only.
// Injected into the existing TweakLoader after BloomAudio. No player mutation,
// audio recording, network requests, private Google method hooks or ISRC spoofing.
#import <Foundation/Foundation.h>
#import <objc/message.h>
#include <dlfcn.h>

static NSString * const kMBGuestProbeEnabled = @"MediaBloomHapticsGuestCaptureV1";
static NSString * const kMBGuestProbeSamples = @"MediaBloomHapticsGuestSamplesV1";
static NSString * const kMBGuestProbeUpdated = @"MediaBloomHapticsGuestUpdatedV1";
static NSString * const kMBGuestProbeEvents = @"MediaBloomHapticsGuestEventsV1";
static NSString *gMBGuestSongSignature = nil;
static NSInteger gMBGuestSampleCount = 0;
static BOOL gMBGuestProbeReportedEmpty = NO;

// An opt-in official haptics bridge is linked after this read-only probe.
static void MBMHInspectCurrentSong(NSDictionary *info, NSString *title,
                                 NSString *artist, NSString *existingISRC);
static void MBMHStartGuestObserver(void);

static NSString *MBGuestStringSymbol(const char *symbolName) {
    NSString * const *address = (NSString * const *)dlsym(RTLD_DEFAULT, symbolName);
    return address ? *address : nil;
}

static NSString *MBGuestString(NSDictionary *info, NSString *key) {
    id value = key.length > 0 ? info[key] : nil;
    return [value isKindOfClass:NSString.class]
        ? [value stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]
        : @"";
}

static void MBGuestAppendHapticEvent(NSString *event) {
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    NSArray *oldEvents = [shared arrayForKey:kMBGuestProbeEvents] ?: @[];
    NSMutableArray *events = [NSMutableArray arrayWithArray:oldEvents];
    NSString *time = [NSDateFormatter localizedStringFromDate:NSDate.date
                                                    dateStyle:NSDateFormatterNoStyle
                                                    timeStyle:NSDateFormatterMediumStyle];
    [events addObject:[NSString stringWithFormat:@"%@ · %@", time, event]];
    if (events.count > 24) {
        [events removeObjectsInRange:NSMakeRange(0, events.count - 24)];
    }
    [shared setObject:events forKey:kMBGuestProbeEvents];
    [shared synchronize];
}

static void MBGuestInspectNowPlaying(void) {
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    [shared synchronize];
    if (![shared boolForKey:kMBGuestProbeEnabled]) return;

    gMBGuestSampleCount++;
    Class centerClass = NSClassFromString(@"MPNowPlayingInfoCenter");
    NSDictionary *info = nil;
    SEL defaultCenterSel = NSSelectorFromString(@"defaultCenter");
    SEL nowPlayingSel = NSSelectorFromString(@"nowPlayingInfo");
    if (centerClass && [centerClass respondsToSelector:defaultCenterSel]) {
        id center = ((id (*)(id, SEL))objc_msgSend)(centerClass, defaultCenterSel);
        if (center && [center respondsToSelector:nowPlayingSel]) {
            id candidate = ((id (*)(id, SEL))objc_msgSend)(center, nowPlayingSel);
            if ([candidate isKindOfClass:NSDictionary.class]) info = candidate;
        }
    }
    NSString *title = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyTitle"));
    NSString *artist = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyArtist"));
    NSString *isrc = @"";
    if (@available(iOS 18.0, *)) {
        isrc = MBGuestString(info,
            MBGuestStringSymbol("MPNowPlayingInfoPropertyInternationalStandardRecordingCode"));
    }
    if (!gMBGuestProbeReportedEmpty && info.count == 0) {
        gMBGuestProbeReportedEmpty = YES;
        MBGuestAppendHapticEvent(@"Guest: Now Playing dictionary is empty/unavailable");
    }

    NSString *signature = [NSString stringWithFormat:@"%@|%@|%@", title, artist, isrc];
    if (title.length > 0 || isrc.length > 0) {
        if (![signature isEqualToString:gMBGuestSongSignature]) {
            gMBGuestSongSignature = [signature copy];
            NSString *description = title.length ? title : @"(unknown title)";
            if (artist.length) description = [description stringByAppendingFormat:@" — %@", artist];
            MBGuestAppendHapticEvent([NSString stringWithFormat:
                @"Guest song: %@ | ISRC: %@", description,
                isrc.length ? isrc : @"not provided"]);
        }
    }
    // The official experiment only runs when explicitly opted in.
    MBMHInspectCurrentSong(info, title, artist, isrc);

    // Heartbeat proves guest code executed; absence of metadata does not mean
    // the player stopped or that a song has no haptic track.
    [shared setInteger:gMBGuestSampleCount forKey:kMBGuestProbeSamples];
    [shared setObject:NSDate.date forKey:kMBGuestProbeUpdated];
    [shared synchronize];
}

static void MBInstallGuestHapticsProbe(void) {
    if (!MBIsMediaHost()) return; // Never activate in original BloomBox.
    NSString *guest = NSUserDefaults.lcGuestAppId.lowercaseString;
    if (![guest isEqualToString:@"com.google.ios.youtubemusic"]) return;

    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    [shared synchronize];
    if (![shared boolForKey:kMBGuestProbeEnabled]) return;

    dispatch_async(dispatch_get_main_queue(), ^{
        dlopen("/System/Library/Frameworks/MediaPlayer.framework/MediaPlayer", RTLD_LAZY);
        gMBGuestSampleCount = 0;
        gMBGuestProbeReportedEmpty = NO;
        gMBGuestSongSignature = nil;
        MBGuestAppendHapticEvent(@"Guest probe loaded in YouTube Music");
        MBMHStartGuestObserver();
        MBGuestInspectNowPlaying();
        [NSTimer scheduledTimerWithTimeInterval:2.0 repeats:YES block:^(NSTimer *timer) {
            [shared synchronize];
            if (![shared boolForKey:kMBGuestProbeEnabled]) {
                [timer invalidate];
                return;
            }
            MBGuestInspectNowPlaying();
        }];
    });
}
