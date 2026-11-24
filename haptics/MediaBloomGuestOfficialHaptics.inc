// MediaBloom experimental Music Haptics bridge. Read-only MusicBrainz lookup,
// then a narrowly scoped Now Playing ISRC update inside the active music guest.
// Only when user explicitly opts in; never edits audio, playback, or song files.
// Music Haptics is Apple-controlled, so an injected ISRC is NOT proof of success.
#import <Foundation/Foundation.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <math.h>

static NSString * const kMBMHEnabledKey = @"MediaBloomHapticsAutoISRCV1";
static NSString * const kMBMHCacheKey = @"MediaBloomHapticsVerifiedISRCV1";
static NSString *gMBMHSongSignature = nil;
static NSString *gMBMHVerifiedISRC = nil;
static NSTimeInterval gMBMHLastWriteAt = 0;
static NSTimeInterval gMBMHNextRequestAt = 0;
static id gMBMHObserverToken = nil;
static NSString *gMBMHLastHapticStatus = nil;

static BOOL MBMHEnabled(void) {
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    [shared synchronize];
    return [shared boolForKey:kMBGuestProbeEnabled] &&
           [shared boolForKey:kMBMHEnabledKey];
}

static NSString *MBMHNormalized(NSString *value) {
    NSString *folded = [[value ?: @"" stringByFoldingWithOptions:
        NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch
        locale:NSLocale.currentLocale] lowercaseString];
    NSMutableString *output = [NSMutableString string];
    NSCharacterSet *allowed = NSCharacterSet.alphanumericCharacterSet;
    for (NSUInteger i = 0; i < folded.length; i++) {
        unichar ch = [folded characterAtIndex:i];
        if ([allowed characterIsMember:ch]) [output appendFormat:@"%C", ch];
    }
    return output;
}

static NSString *MBMHArtistCredit(NSDictionary *recording) {
    NSArray *credits = recording[@"artist-credit"];
    if (![credits isKindOfClass:NSArray.class]) return @"";
    NSMutableString *artist = [NSMutableString string];
    for (id entry in credits) {
        if ([entry isKindOfClass:NSString.class]) {
            [artist appendString:entry];
        } else if ([entry isKindOfClass:NSDictionary.class]) {
            NSString *name = entry[@"name"];
            if (![name isKindOfClass:NSString.class]) {
                name = [entry[@"artist"] isKindOfClass:NSDictionary.class] ?
                    entry[@"artist"][@"name"] : @"";
            }
            if (name.length > 0) [artist appendString:name];
            NSString *join = entry[@"joinphrase"];
            if ([join isKindOfClass:NSString.class]) [artist appendString:join];
        }
    }
    return artist;
}

static NSTimeInterval MBMHDuration(NSDictionary *info) {
    NSString *key = MBGuestStringSymbol("MPMediaItemPropertyPlaybackDuration");
    id duration = key.length > 0 ? info[key] : nil;
    return [duration respondsToSelector:@selector(doubleValue)] ?
        [duration doubleValue] : 0;
}

static NSString *MBMHSignature(NSString *title, NSString *artist, NSDictionary *info) {
    // The duration distinguishes studio originals from remixes/live recordings.
    NSInteger seconds = (NSInteger)llround(MBMHDuration(info));
    return [NSString stringWithFormat:@"%@|%@|%ld",
        MBMHNormalized(title), MBMHNormalized(artist), (long)seconds];
}

static BOOL MBMHStillCurrent(NSString *signature) {
    return MBMHEnabled() && [signature isEqualToString:gMBMHSongSignature];
}

static id MBMHNowPlayingCenter(void) {
    Class cls = NSClassFromString(@"MPNowPlayingInfoCenter");
    SEL sel = NSSelectorFromString(@"defaultCenter");
    return cls && [cls respondsToSelector:sel] ?
        ((id (*)(id, SEL))objc_msgSend)(cls, sel) : nil;
}

static NSDictionary *MBMHNowPlayingInfo(void) {
    id center = MBMHNowPlayingCenter();
    SEL sel = NSSelectorFromString(@"nowPlayingInfo");
    id info = center && [center respondsToSelector:sel] ?
        ((id (*)(id, SEL))objc_msgSend)(center, sel) : nil;
    return [info isKindOfClass:NSDictionary.class] ? info : nil;
}

static id MBMHManager(void) {
    if (!@available(iOS 18.0, *)) return nil;
    dlopen("/System/Library/Frameworks/MediaAccessibility.framework/MediaAccessibility",
           RTLD_LAZY | RTLD_GLOBAL);
    Class cls = NSClassFromString(@"MAMusicHapticsManager");
    SEL shared = NSSelectorFromString(@"sharedManager");
    if (![cls respondsToSelector:shared]) shared = NSSelectorFromString(@"shared");
    return cls && [cls respondsToSelector:shared] ?
        ((id (*)(id, SEL))objc_msgSend)(cls, shared) : nil;
}

static BOOL MBMHSystemEnabled(void) {
    id manager = MBMHManager();
    SEL sel = NSSelectorFromString(@"isActive");
    return manager && [manager respondsToSelector:sel] &&
        ((BOOL (*)(id, SEL))objc_msgSend)(manager, sel);
}

static void MBMHStartGuestObserver(void) {
    if (!@available(iOS 18.0, *)) return;
    if (gMBMHObserverToken) return;
    id manager = MBMHManager();
    SEL sel = NSSelectorFromString(@"addStatusObserver:");
    if (!manager || ![manager respondsToSelector:sel]) {
        MBGuestAppendHapticEvent(@"Apple Music Haptics status observer unavailable");
        return;
    }
    void (^callback)(NSString *, BOOL) = ^(NSString *code, BOOL playing) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!MBMHEnabled()) return;
            NSString *state = [NSString stringWithFormat:@"%@:%@",
                code ?: @"", playing ? @"on" : @"off"];
            if ([state isEqualToString:gMBMHLastHapticStatus]) return;
            gMBMHLastHapticStatus = state;
            MBGuestAppendHapticEvent(playing ?
                [NSString stringWithFormat:@"Apple Music Haptics PLAYING — %@", code ?: @"?"] :
                [NSString stringWithFormat:@"Apple Music Haptics STOPPED — %@", code ?: @"?"]);
        });
    };
    gMBMHObserverToken = ((id (*)(id, SEL, id))objc_msgSend)(manager, sel, callback);
    MBGuestAppendHapticEvent(@"Apple Music Haptics observer installed");
}

// MusicBrainz requires both a descriptive user agent and <=1 request/second.
// Reserve each slot on the main queue BEFORE starting a request.
static void MBMHRequestJSON(NSURL *url, void (^completion)(NSDictionary *)) {
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    NSTimeInterval next = MAX(now, gMBMHNextRequestAt);
    gMBMHNextRequestAt = next + 1.2;
    int64_t delay = (int64_t)((next - now) * NSEC_PER_SEC);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay), dispatch_get_main_queue(), ^{
        if (!MBMHEnabled()) return;
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
        request.timeoutInterval = 12;
        request.HTTPMethod = @"GET";
        [request setValue:@"MediaBloomMusicHaptics/0.1 (https://github.com/AdriFOG/MediaBloom-iOS)"
       forHTTPHeaderField:@"User-Agent"];
        [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
        [[NSURLSession.sharedSession dataTaskWithRequest:request
            completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            NSDictionary *json = nil;
            if (!error && data.length > 0 && data.length < 400000 &&
                [(NSHTTPURLResponse *)response statusCode] == 200) {
                id object = [NSJSONSerialization JSONObjectWithData:data
                                            options:0 error:nil];
                if ([object isKindOfClass:NSDictionary.class]) json = object;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                if (MBMHEnabled() && completion) completion(json);
            });
        }] resume];
    });
}

static void MBMHApplyVerifiedISRC(NSString *code, NSString *signature) {
    if (!@available(iOS 18.0, *)) return;
    if (!MBMHStillCurrent(signature) || !MBMHSystemEnabled()) return;
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    if (now - gMBMHLastWriteAt < 12.0) return;
    id center = MBMHNowPlayingCenter();
    NSDictionary *info = MBMHNowPlayingInfo();
    NSString *title = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyTitle"));
    NSString *artist = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyArtist"));
    if (!info || ![MBMHSignature(title, artist, info) isEqualToString:signature]) return;
    NSString *key = MBGuestStringSymbol(
        "MPNowPlayingInfoPropertyInternationalStandardRecordingCode");
    if (key.length == 0) return;
    NSString *existing = MBGuestString(info, key);
    if (existing.length > 0) return; // Never overwrite the original player's ISRC.
    NSString *rateKey = MBGuestStringSymbol("MPNowPlayingInfoPropertyPlaybackRate");
    id rate = rateKey.length ? info[rateKey] : nil;
    if ([rate respondsToSelector:@selector(doubleValue)] && [rate doubleValue] <= 0) return;
    SEL setter = NSSelectorFromString(@"setNowPlayingInfo:");
    if (![center respondsToSelector:setter]) return;
    NSMutableDictionary *modified = [info mutableCopy];
    modified[key] = code; // Preserve ALL other YouTube Music Now Playing fields.
    gMBMHLastWriteAt = now;
    ((void (*)(id, SEL, id))objc_msgSend)(center, setter, modified);
    MBGuestAppendHapticEvent([NSString stringWithFormat:
        @"Official Music Haptics test: ISRC %@ added to active Now Playing (not yet confirmed)",
        code]);
}

static void MBMHSaveVerifiedISRC(NSString *signature, NSString *code) {
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    NSMutableDictionary *cache = [[shared dictionaryForKey:kMBMHCacheKey] ?: @{} mutableCopy];
    if (cache.count >= 40 && cache[signature] == nil) {
        NSString *oldest = [[cache.allKeys sortedArrayUsingSelector:@selector(compare:)] firstObject];
        if (oldest) [cache removeObjectForKey:oldest];
    }
    cache[signature] = code;
    [shared setObject:cache forKey:kMBMHCacheKey];
    [shared synchronize];
}

static BOOL MBMHValidISRC(NSString *code) {
    if (![code isKindOfClass:NSString.class] || code.length != 12) return NO;
    NSString *upper = code.uppercaseString;
    NSRegularExpression *re = [NSRegularExpression
        regularExpressionWithPattern:@"^[A-Z]{2}[A-Z0-9]{3}[0-9]{7}$"
        options:0 error:nil];
    return [re numberOfMatchesInString:upper options:0
                                range:NSMakeRange(0, upper.length)] == 1;
}

static void MBMHCheckISRCs(NSArray<NSString *> *codes, NSUInteger index,
                          NSString *signature) {
    if (!MBMHStillCurrent(signature)) return;
    if (index >= codes.count) {
        MBGuestAppendHapticEvent(@"Apple has no confirmed haptic track for the matched ISRCs");
        return;
    }
    if (!MBMHSystemEnabled()) {
        MBGuestAppendHapticEvent(@"Apple Music Haptics is disabled in iOS Settings");
        return;
    }
    id manager = MBMHManager();
    SEL sel = NSSelectorFromString(@"checkHapticTrackAvailabilityForMedia:completionHandler:");
    if (!manager || ![manager respondsToSelector:sel]) {
        MBGuestAppendHapticEvent(@"Apple haptic availability API unavailable");
        return;
    }
    NSString *code = codes[index];
    void (^completion)(BOOL) = ^(BOOL available) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!MBMHStillCurrent(signature)) return;
            if (!available) {
                MBMHCheckISRCs(codes, index + 1, signature);
                return;
            }
            gMBMHVerifiedISRC = [code copy];
            MBMHSaveVerifiedISRC(signature, code);
            MBGuestAppendHapticEvent([NSString stringWithFormat:
                @"Apple confirms haptic track for ISRC %@", code]);
            MBMHApplyVerifiedISRC(code, signature);
        });
    };
    ((void (*)(id, SEL, id, id))objc_msgSend)(manager, sel, code, completion);
}

static NSURL *MBMHRecordingLookupURL(NSString *uuid) {
    NSString *escaped = [[uuid ?: @"" stringByAddingPercentEncodingWithAllowedCharacters:
                            NSCharacterSet.URLPathAllowedCharacterSet] copy];
    return [NSURL URLWithString:[NSString stringWithFormat:
        @"https://musicbrainz.org/ws/2/recording/%@?inc=isrcs&fmt=json", escaped]];
}

static void MBMHLookupISRC(NSString *recordingID, NSString *signature) {
    NSURL *url = MBMHRecordingLookupURL(recordingID);
    MBMHRequestJSON(url, ^(NSDictionary *detail) {
        if (!MBMHStillCurrent(signature)) return;
        NSArray *raw = detail[@"isrcs"];
        if (![raw isKindOfClass:NSArray.class]) {
            MBGuestAppendHapticEvent(@"No ISRC listed for the exact MusicBrainz recording");
            return;
        }
        NSMutableArray<NSString *> *codes = [NSMutableArray array];
        for (id item in raw) {
            if (![item isKindOfClass:NSString.class]) continue;
            NSString *code = [item uppercaseString];
            if (!MBMHValidISRC(code) || [codes containsObject:code]) continue;
            [codes addObject:code];
            if (codes.count == 3) break; // Bounded Apple availability calls.
        }
        if (codes.count == 0) {
            MBGuestAppendHapticEvent(@"No usable ISRC in MusicBrainz recording");
            return;
        }
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Matched recording; checking %lu ISRC(s) with Apple",
            (unsigned long)codes.count]);
        MBMHCheckISRCs(codes, 0, signature);
    });
}

static NSURL *MBMHSearchURL(NSString *title, NSString *artist) {
    NSString *cleanTitle = [[title stringByReplacingOccurrencesOfString:@"\"" withString:@" "]
                          stringByReplacingOccurrencesOfString:@"\\" withString:@" "];
    NSString *cleanArtist = [[artist stringByReplacingOccurrencesOfString:@"\"" withString:@" "]
                           stringByReplacingOccurrencesOfString:@"\\" withString:@" "];
    NSString *query = [NSString stringWithFormat:
        @"recording:\"%@\" AND artistname:\"%@\"", cleanTitle, cleanArtist];
    NSURLComponents *components = [NSURLComponents
        componentsWithString:@"https://musicbrainz.org/ws/2/recording/"];
    components.queryItems = @[
        [NSURLQueryItem queryItemWithName:@"query" value:query],
        [NSURLQueryItem queryItemWithName:@"fmt" value:@"json"],
        [NSURLQueryItem queryItemWithName:@"limit" value:@"8"]
    ];
    return components.URL;
}

static void MBMHSearchRecording(NSString *title, NSString *artist,
                              NSTimeInterval duration, NSString *signature) {
    NSURL *url = MBMHSearchURL(title, artist);
    MBGuestAppendHapticEvent([NSString stringWithFormat:
        @"MusicBrainz: searching exact %@ — %@", title, artist]);
    MBMHRequestJSON(url, ^(NSDictionary *response) {
        if (!MBMHStillCurrent(signature)) return;
        NSArray *recordings = response[@"recordings"];
        if (![recordings isKindOfClass:NSArray.class]) {
            MBGuestAppendHapticEvent(@"MusicBrainz search unavailable (no ISRC applied)");
            return;
        }
        NSMutableArray<NSString *> *matches = [NSMutableArray array];
        NSString *expectedTitle = MBMHNormalized(title);
        NSString *expectedArtist = MBMHNormalized(artist);
        for (id obj in recordings) {
            if (![obj isKindOfClass:NSDictionary.class]) continue;
            NSDictionary *entry = obj;
            if ([entry[@"score"] integerValue] < 98) continue;
            if (![MBMHNormalized(entry[@"title"]) isEqualToString:expectedTitle]) continue;
            if (![MBMHNormalized(MBMHArtistCredit(entry)) isEqualToString:expectedArtist]) continue;
            NSNumber *milliseconds = entry[@"length"];
            if (duration > 30 && [milliseconds respondsToSelector:@selector(doubleValue)] &&
                milliseconds.doubleValue > 0 &&
                fabs(milliseconds.doubleValue / 1000.0 - duration) > 8.0) continue;
            NSString *uuid = entry[@"id"];
            if (![uuid isKindOfClass:NSString.class] || ![NSUUID.alloc initWithUUIDString:uuid]) continue;
            if (![matches containsObject:uuid]) [matches addObject:uuid];
        }
        if (matches.count != 1) {
            MBGuestAppendHapticEvent([NSString stringWithFormat:
                @"MusicBrainz match rejected: %lu exact recording(s); no ISRC inserted",
                (unsigned long)matches.count]);
            return;
        }
        MBMHLookupISRC(matches.firstObject, signature);
    });
}

static void MBMHInspectCurrentSong(NSDictionary *info, NSString *title,
                                 NSString *artist, NSString *existingISRC) {
    if (!MBMHEnabled() || !@available(iOS 18.0, *)) return;
    if (!title.length || !artist.length) return;
    if (title.length > 150 || artist.length > 150) return;
    NSString *signature = MBMHSignature(title, artist, info);
    if ([signature isEqualToString:gMBMHSongSignature]) {
        if (!existingISRC.length && gMBMHVerifiedISRC.length) {
            MBMHApplyVerifiedISRC(gMBMHVerifiedISRC, signature);
        }
        return;
    }
    gMBMHSongSignature = [signature copy];
    gMBMHVerifiedISRC = nil;
    gMBMHLastWriteAt = 0;
    if (existingISRC.length) {
        MBGuestAppendHapticEvent(@"YouTube Music supplied an ISRC; no override necessary");
        return;
    }
    NSDictionary *cache = [NSUserDefaults.lcSharedDefaults dictionaryForKey:kMBMHCacheKey];
    NSString *cachedCode = cache[signature];
    if (MBMHValidISRC(cachedCode)) {
        gMBMHVerifiedISRC = cachedCode;
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Verified ISRC cache hit: %@", cachedCode]);
        MBMHApplyVerifiedISRC(cachedCode, signature);
        return;
    }
    MBMHSearchRecording(title, artist, MBMHDuration(info), signature);
}
