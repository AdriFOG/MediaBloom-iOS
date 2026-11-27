// MediaBloom experimental Music Haptics bridge. Read-only MusicBrainz lookup,
// then a narrowly scoped Now Playing ISRC update inside the active music guest.
// Only when user explicitly opts in; never edits audio, playback, or song files.
// Music Haptics is Apple-controlled, so an injected ISRC is NOT proof of success.
#import <Foundation/Foundation.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <math.h>
#include <float.h>

static NSString * const kMBMHEnabledKey = @"MediaBloomHapticsAutoISRCV1";
static NSString * const kMBMHCacheKey = @"MediaBloomHapticsVerifiedISRCV1";
static NSString * const kMBMHRejectedCodesKey = @"MediaBloomHapticsRejectedISRCV1";
static NSString * const kMBMHStatusKey = @"MediaBloomHapticsStatusV1";
static NSString * const kMBMHStatusDetailKey = @"MediaBloomHapticsStatusDetailV1";
static NSString * const kMBMHStatusSongKey = @"MediaBloomHapticsStatusSongV1";
static NSString * const kMBMHStatusDateKey = @"MediaBloomHapticsStatusDateV1";
static NSString *gMBMHSongSignature = nil;
static NSString *gMBMHVerifiedISRC = nil;
static NSTimeInterval gMBMHLastWriteAt = 0;
static NSTimeInterval gMBMHLastInsertionLogAt = 0;
static NSTimeInterval gMBMHLastResyncRequestAt = 0;
static NSTimeInterval gMBMHLastAutoResyncAt = 0;
static NSTimeInterval gMBMHNextRequestAt = 0;
static id gMBMHObserverToken = nil;
static NSString *gMBMHLastHapticStatus = nil;
static NSString *gMBMHCurrentTitle = nil;
static NSString *gMBMHCurrentArtist = nil;
static NSString *gMBMHCurrentAlbum = nil;
static NSTimeInterval gMBMHCurrentDuration = 0;
static NSString *gMBMHDeezerAttemptedSignature = nil;
static NSTimeInterval gMBMHMusicBrainzCooldownUntil = 0;
static void MBMHDeezerFallback(NSString *signature, NSString *reason);

static BOOL MBMHEnabled(void) {
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    [shared synchronize];
    return MBGuestAutoHapticsWanted();
}

// Shared, persistent *last observed* status; the user closes the guest to
// return to MediaBloom. Do not label old samples as "currently playing".
static void MBMHSetStatus(NSString *state, NSString *detail, NSString *signature) {
    if (signature.length && ![signature isEqualToString:gMBMHSongSignature]) return;
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    NSString *song = gMBMHCurrentTitle.length ? gMBMHCurrentTitle : @"";
    NSString *actualState = state ?: @"unknown";
    NSString *actualDetail = detail ?: @"";
    if ([[shared stringForKey:kMBMHStatusKey] isEqualToString:actualState] &&
        [[shared stringForKey:kMBMHStatusDetailKey] isEqualToString:actualDetail] &&
        [[shared stringForKey:kMBMHStatusSongKey] isEqualToString:song]) return;
    [shared setObject:actualState forKey:kMBMHStatusKey];
    [shared setObject:actualDetail forKey:kMBMHStatusDetailKey];
    [shared setObject:song forKey:kMBMHStatusSongKey];
    [shared setObject:NSDate.date forKey:kMBMHStatusDateKey];
    [shared synchronize];
    MBMHUpdateGuestOverlay();
}

// The Apple catalog can change. A rejection is temporary (12 hours), scoped
// strictly to a real ISRC, and never means all versions of a song are rejected.
static BOOL MBMHIsRejectedISRC(NSString *code) {
    if (!code.length) return NO;
    NSDictionary *all = [NSUserDefaults.lcSharedDefaults
        dictionaryForKey:kMBMHRejectedCodesKey] ?: @{};
    id timestamp = all[code.uppercaseString];
    if (![timestamp respondsToSelector:@selector(doubleValue)]) return NO;
    NSTimeInterval age = NSDate.timeIntervalSinceReferenceDate -
                         [timestamp doubleValue];
    return age >= 0 && age < (12.0 * 3600.0);
}

static void MBMHRememberRejectedISRC(NSString *code) {
    if (!code.length) return;
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    NSMutableDictionary *all = [[shared dictionaryForKey:kMBMHRejectedCodesKey]
                                 ?: @{} mutableCopy];
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    for (NSString *item in [all.allKeys copy]) {
        id date = all[item];
        if (![date respondsToSelector:@selector(doubleValue)] ||
            now - [date doubleValue] >= 12.0 * 3600.0) {
            [all removeObjectForKey:item];
        }
    }
    if (all.count >= 120 && !all[code.uppercaseString]) {
        NSString *oldest = nil;
        NSTimeInterval oldestTime = DBL_MAX;
        for (NSString *item in all) {
            NSTimeInterval time = [all[item] doubleValue];
            if (time < oldestTime) { oldestTime = time; oldest = item; }
        }
        if (oldest) [all removeObjectForKey:oldest];
    }
    all[code.uppercaseString] = @(now);
    [shared setObject:all forKey:kMBMHRejectedCodesKey];
    [shared synchronize];
}

static NSString *MBMHNormalized(NSString *value) {
    if (![value isKindOfClass:NSString.class]) return @"";
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
    // YouTube Music can update playback duration by several seconds while
    // a track starts. Duration is useful for matching, but NOT as a song key:
    // using it here sent repeated MusicBrainz queries for a single song.
    NSString *album = MBGuestString(info,
        MBGuestStringSymbol("MPMediaItemPropertyAlbumTitle"));
    return [NSString stringWithFormat:@"%@|%@|%@",
        MBMHNormalized(title), MBMHNormalized(artist), MBMHNormalized(album)];
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
    if (@available(iOS 18.0, *)) { /* supported */ } else { return nil; }
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
    if (@available(iOS 18.0, *)) { /* supported */ } else { return; }
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
            // Only attribute playback to the current matching recording.
            if (gMBMHVerifiedISRC.length && [code isEqualToString:gMBMHVerifiedISRC]) {
                MBMHSetStatus(playing ? @"playing" : @"ready",
                             playing ? @"iOS confirmed synchronized playback" :
                                       @"Apple playback stopped; track is compatible",
                             gMBMHSongSignature);
                if (!playing) {
                    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
                    // A network stall can stop the haptic stream while audio
                    // continues. Retry only if player still reports playback.
                    if (now - gMBMHLastAutoResyncAt >= 20.0) {
                        gMBMHLastAutoResyncAt = now;
                        NSString *expected = [gMBMHSongSignature copy];
                        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                            (int64_t)(2.0 * NSEC_PER_SEC)),
                            dispatch_get_main_queue(), ^{
                            if (!MBMHStillCurrent(expected)) return;
                            NSDictionary *playingInfo = MBMHNowPlayingInfo();
                            NSString *rateKey = MBGuestStringSymbol(
                                "MPNowPlayingInfoPropertyPlaybackRate");
                            id rate = rateKey.length ? playingInfo[rateKey] : nil;
                            if (![rate respondsToSelector:@selector(doubleValue)] ||
                                [rate doubleValue] <= 0) return;
                            if ([[NSUserDefaults.lcSharedDefaults stringForKey:kMBMHStatusKey]
                                 isEqualToString:@"playing"]) return;
                            MBMHForceResync();
                        });
                    }
                }
            }
        });
    };
    gMBMHObserverToken = ((id (*)(id, SEL, id))objc_msgSend)(manager, sel, callback);
    MBGuestAppendHapticEvent(@"Apple Music Haptics observer installed");
}

// MusicBrainz requires both a descriptive user agent and <=1 request/second.
// Reserve each slot on the main queue BEFORE starting a request.
static void MBMHRequestJSONAttempt(NSURL *url, NSUInteger attempt,
                                   void (^completion)(NSDictionary *)) {
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    NSTimeInterval next = MAX(now, gMBMHNextRequestAt);
    gMBMHNextRequestAt = next + 1.3; // Reserve globally before any task starts.
    int64_t delay = (int64_t)((next - now) * NSEC_PER_SEC);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay), dispatch_get_main_queue(), ^{
        if (!MBMHEnabled()) return;
        if (!url || ![url.scheme isEqualToString:@"https"] ||
            ![url.host isEqualToString:@"musicbrainz.org"]) return;
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
        request.timeoutInterval = 15;
        request.HTTPMethod = @"GET";
        [request setValue:@"MediaBloomMusicHaptics/0.2 (https://github.com/AdriFOG/MediaBloom-iOS)"
       forHTTPHeaderField:@"User-Agent"];
        [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
        [[NSURLSession.sharedSession dataTaskWithRequest:request
            completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class]
                ? [(NSHTTPURLResponse *)response statusCode] : 0;
            NSDictionary *json = nil;
            if (!error && status == 200 && data.length > 0 && data.length < 600000) {
                id object = [NSJSONSerialization JSONObjectWithData:data
                                            options:0 error:nil];
                if ([object isKindOfClass:NSDictionary.class]) json = object;
            }
            const BOOL transientHTTP = status == 429 || status == 500 ||
                status == 502 || status == 503 || status == 504;
            const BOOL transientNetwork = error && (
                error.code == NSURLErrorTimedOut ||
                error.code == NSURLErrorNetworkConnectionLost ||
                error.code == NSURLErrorCannotConnectToHost);
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!MBMHEnabled()) return;
                if (json) {
                    if (completion) completion(json);
                    return;
                }
                // MusicBrainz sometimes responds 503 for an entire session.
                // Switch to Deezer rather than flooding it with retries.
                if (status == 503) {
                    gMBMHMusicBrainzCooldownUntil = NSDate.timeIntervalSinceReferenceDate + 300.0;
                    MBGuestAppendHapticEvent(@"MusicBrainz HTTP 503; using Deezer for 5 minutes");
                    if (completion) completion(nil);
                    return;
                }
                NSString *reason = error ?
                    [NSString stringWithFormat:@"network error %ld", (long)error.code] :
                    [NSString stringWithFormat:@"HTTP %ld", (long)status];
                if ((transientHTTP || transientNetwork) && attempt < 2) {
                    NSTimeInterval backoff = attempt == 0 ? 3.0 : 6.0;
                    MBGuestAppendHapticEvent([NSString stringWithFormat:
                        @"MusicBrainz %@, retry %lu/2", reason, (unsigned long)(attempt+1)]);
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                        (int64_t)(backoff * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                        if (MBMHEnabled()) MBMHRequestJSONAttempt(url, attempt + 1, completion);
                    });
                } else {
                    MBGuestAppendHapticEvent([NSString stringWithFormat:
                        @"MusicBrainz request failed: %@ (no ISRC applied)", reason]);
                    if (completion) completion(nil);
                }
            });
        }] resume];
    });
}

static void MBMHRequestJSON(NSURL *url, void (^completion)(NSDictionary *)) {
    MBMHRequestJSONAttempt(url, 0, completion);
}

static void MBMHApplyVerifiedISRC(NSString *code, NSString *signature) {
    if (@available(iOS 18.0, *)) { /* supported */ } else { return; }
    if (!MBMHStillCurrent(signature) || !MBMHSystemEnabled()) return;
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    // The old 12-second lockout caused gaps whenever YouTube Music erased
    // the injected ISRC. Re-check after 1 second; never change the original
    // recording code or any playback controls.
    if (now - gMBMHLastWriteAt < 1.0) return;
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
    // Frequent guest metadata updates can require repeated restoration.
    // Avoid filling the saved log every time the same ISRC is restored.
    if (gMBMHLastInsertionLogAt == 0 || now - gMBMHLastInsertionLogAt >= 20.0) {
        gMBMHLastInsertionLogAt = now;
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Fast Music Haptics: ISRC %@ supplied to Now Playing (Apple confirmation pending)",
            code]);
    }
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
        MBGuestAppendHapticEvent(@"Apple has no confirmed haptic track for matched ISRCs");
        MBMHSetStatus(@"unavailable", @"No Apple haptic track confirmed for matched ISRCs",
                      signature);
        MBMHDeezerFallback(signature, @"No Apple track for the MusicBrainz codes");
        return;
    }
    if (!MBMHSystemEnabled()) {
        MBGuestAppendHapticEvent(@"Apple Music Haptics is disabled in iOS Settings");
        MBMHSetStatus(@"disabled", @"Enable Music Haptics in iOS Accessibility settings",
                      signature);
        return;
    }
    id manager = MBMHManager();
    SEL sel = NSSelectorFromString(@"checkHapticTrackAvailabilityForMediaMatchingCode:completionHandler:");
    if (!manager || ![manager respondsToSelector:sel]) {
        MBGuestAppendHapticEvent(@"Apple haptic availability API unavailable");
        MBMHSetStatus(@"error", @"Apple haptic availability API unavailable", signature);
        return;
    }
    NSString *code = codes[index];
    if (MBMHIsRejectedISRC(code)) {
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Skipped recently rejected Apple ISRC %@", code]);
        MBMHCheckISRCs(codes, index + 1, signature);
        return;
    }
    MBMHSetStatus(@"checking", [NSString stringWithFormat:
        @"Checking Apple's haptic catalog: %@", code], signature);
    void (^completion)(BOOL) = ^(BOOL available) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!MBMHStillCurrent(signature)) return;
            if (!available) {
                MBMHRememberRejectedISRC(code);
                MBMHCheckISRCs(codes, index + 1, signature);
                return;
            }
            gMBMHVerifiedISRC = [code copy];
            MBMHSetStatus(@"ready", [NSString stringWithFormat:
                @"Apple has a Music Haptics track for %@", code], signature);
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

static NSArray<NSString *> *MBMHValidCodesFromRecording(NSDictionary *detail) {
    NSArray *raw = [detail isKindOfClass:NSDictionary.class] ? detail[@"isrcs"] : nil;
    if (![raw isKindOfClass:NSArray.class]) return @[];
    NSMutableOrderedSet<NSString *> *codes = [NSMutableOrderedSet orderedSet];
    for (id item in raw) {
        if (![item isKindOfClass:NSString.class]) continue;
        NSString *code = [item uppercaseString];
        if (MBMHValidISRC(code)) [codes addObject:code];
    }
    return [codes.array sortedArrayUsingSelector:@selector(compare:)];
}

static void MBMHLookupISRC(NSString *recordingID, NSString *signature) {
    MBMHRequestJSON(MBMHRecordingLookupURL(recordingID), ^(NSDictionary *detail) {
        if (!MBMHStillCurrent(signature)) return;
        NSArray<NSString *> *codes = MBMHValidCodesFromRecording(detail);
        if (codes.count == 0) {
            MBGuestAppendHapticEvent(@"No ISRC listed for exact MusicBrainz recording");
            MBMHDeezerFallback(signature, @"MusicBrainz exact recording has no ISRC");
            return;
        }
        NSArray<NSString *> *limited = [codes subarrayWithRange:
            NSMakeRange(0, MIN((NSUInteger)3, codes.count))];
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Matched unique recording; checking %lu ISRC(s) with Apple",
            (unsigned long)limited.count]);
        MBMHCheckISRCs(limited, 0, signature);
    });
}

// Some releases have distinct MusicBrainz recording IDs for the same actual
// audio. Do not arbitrarily pick the first MBID when title/artist are equal:
// use a code shared by EVERY plausible matching recording, or abort safely.
static void MBMHLookupSharedISRCs(NSArray<NSString *> *ids, NSUInteger index,
                                 NSSet<NSString *> *intersection, NSString *signature) {
    if (!MBMHStillCurrent(signature)) return;
    if (index == ids.count) {
        NSArray<NSString *> *common = [[intersection allObjects]
            sortedArrayUsingSelector:@selector(compare:)];
        if (common.count == 0) {
            MBGuestAppendHapticEvent(
                @"MusicBrainz: recordings disagree on ISRC; checking Deezer");
            MBMHDeezerFallback(signature, @"MusicBrainz recordings have different ISRCs");
            return;
        }
        NSArray<NSString *> *limited = [common subarrayWithRange:
            NSMakeRange(0, MIN((NSUInteger)3, common.count))];
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"%lu MusicBrainz recordings share %lu ISRC(s); checking Apple",
            (unsigned long)ids.count, (unsigned long)limited.count]);
        MBMHCheckISRCs(limited, 0, signature);
        return;
    }
    MBMHRequestJSON(MBMHRecordingLookupURL(ids[index]), ^(NSDictionary *detail) {
        if (!MBMHStillCurrent(signature)) return;
        NSArray<NSString *> *codes = MBMHValidCodesFromRecording(detail);
        if (codes.count == 0) {
            MBGuestAppendHapticEvent(
                @"MusicBrainz recording has no ISRC; checking Deezer");
            MBMHDeezerFallback(signature, @"A MusicBrainz candidate has no ISRC");
            return;
        }
        NSSet<NSString *> *available = [NSSet setWithArray:codes];
        NSMutableSet<NSString *> *common = intersection ?
            [intersection mutableCopy] : [available mutableCopy];
        if (intersection) [common intersectSet:available];
        if (common.count == 0) {
            MBGuestAppendHapticEvent(
                @"MusicBrainz candidate ISRCs disagree; checking Deezer");
            MBMHDeezerFallback(signature, @"MusicBrainz codes do not agree");
            return;
        }
        MBMHLookupSharedISRCs(ids, index + 1, common, signature);
    });
}

static BOOL MBMHRecordingMatchesAlbum(NSDictionary *entry, NSString *album) {
    if (album.length == 0) return NO;
    NSArray *releases = entry[@"releases"];
    if (![releases isKindOfClass:NSArray.class]) return NO;
    NSString *expected = MBMHNormalized(album);
    for (id candidate in releases) {
        if (![candidate isKindOfClass:NSDictionary.class]) continue;
        if ([MBMHNormalized(candidate[@"title"]) isEqualToString:expected]) return YES;
    }
    return NO;
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
                              NSTimeInterval duration, NSString *album,
                              NSString *signature) {
    if (NSDate.timeIntervalSinceReferenceDate < gMBMHMusicBrainzCooldownUntil) {
        MBMHDeezerFallback(signature, @"MusicBrainz temporarily unavailable");
        return;
    }
    NSURL *url = MBMHSearchURL(title, artist);
    MBGuestAppendHapticEvent([NSString stringWithFormat:
        @"MusicBrainz: searching %@ — %@%@", title, artist,
        duration > 30 ? [NSString stringWithFormat:@" (%.0fs)", duration] : @""]);
    MBMHSetStatus(@"searching", @"Searching MusicBrainz for the exact recording",
                  signature);
    MBMHRequestJSON(url, ^(NSDictionary *response) {
        if (!MBMHStillCurrent(signature)) return;
        NSArray *recordings = response[@"recordings"];
        if (![recordings isKindOfClass:NSArray.class]) {
            MBGuestAppendHapticEvent(@"MusicBrainz unavailable; checking Deezer");
            MBMHDeezerFallback(signature, @"MusicBrainz search unavailable");
            return;
        }
        NSMutableArray<NSDictionary *> *matches = [NSMutableArray array];
        NSString *expectedTitle = MBMHNormalized(title);
        NSString *expectedArtist = MBMHNormalized(artist);
        for (id obj in recordings) {
            if (![obj isKindOfClass:NSDictionary.class]) continue;
            NSDictionary *entry = obj;
            if ([entry[@"score"] integerValue] < 98) continue;
            if (![MBMHNormalized(entry[@"title"]) isEqualToString:expectedTitle]) continue;
            if (![MBMHNormalized(MBMHArtistCredit(entry)) isEqualToString:expectedArtist]) continue;
            NSNumber *milliseconds = entry[@"length"];
            BOOL hasLength = [milliseconds respondsToSelector:@selector(doubleValue)] &&
                             milliseconds.doubleValue > 0;
            NSTimeInterval difference = hasLength && duration > 30
                ? fabs(milliseconds.doubleValue / 1000.0 - duration) : 100000.0;
            if (duration > 30 && hasLength && difference > 8.0) continue;
            NSString *uuid = entry[@"id"];
            if (![uuid isKindOfClass:NSString.class] ||
                ![[NSUUID alloc] initWithUUIDString:uuid]) continue;
            BOOL duplicate = NO;
            for (NSDictionary *seen in matches) {
                if ([seen[@"id"] isEqualToString:uuid]) { duplicate = YES; break; }
            }
            if (duplicate) continue;
            [matches addObject:@{
                @"id": uuid,
                @"durationDifference": @(difference),
                @"hasLength": @(hasLength),
                @"albumMatch": @(MBMHRecordingMatchesAlbum(entry, album))
            }];
        }
        if (matches.count == 0) {
            MBGuestAppendHapticEvent(@"MusicBrainz: no exact recording; checking Deezer");
            MBMHDeezerFallback(signature, @"No exact MusicBrainz recording");
            return;
        }
        NSMutableArray<NSDictionary *> *candidates = [matches mutableCopy];
        if (album.length > 0) {
            NSMutableArray<NSDictionary *> *albumMatches = [NSMutableArray array];
            for (NSDictionary *item in candidates) {
                if ([item[@"albumMatch"] boolValue]) [albumMatches addObject:item];
            }
            if (albumMatches.count > 0) {
                candidates = albumMatches;
                MBGuestAppendHapticEvent([NSString stringWithFormat:
                    @"MusicBrainz: album narrowed %lu to %lu recording(s)",
                    (unsigned long)matches.count, (unsigned long)candidates.count]);
            }
        }
        if (candidates.count == 1) {
            MBMHLookupISRC(candidates[0][@"id"], signature);
            return;
        }
        // Only use a duration to choose a particular recording when EVERY
        // candidate has a measured duration and the nearest is clearly unique.
        if (duration > 30) {
            BOOL allHaveLength = YES;
            for (NSDictionary *item in candidates) {
                if (![item[@"hasLength"] boolValue]) { allHaveLength = NO; break; }
            }
            if (allHaveLength) {
                NSArray<NSDictionary *> *ranked = [candidates sortedArrayUsingComparator:
                    ^NSComparisonResult(NSDictionary *first, NSDictionary *second) {
                        return [first[@"durationDifference"]
                            compare:second[@"durationDifference"]];
                    }];
                double nearest = [ranked[0][@"durationDifference"] doubleValue];
                double second = [ranked[1][@"durationDifference"] doubleValue];
                if (nearest <= 1.5 && second - nearest >= 2.0) {
                    MBGuestAppendHapticEvent([NSString stringWithFormat:
                        @"MusicBrainz: unique duration match (%.1fs closer); checking Apple",
                        second - nearest]);
                    MBMHLookupISRC(ranked[0][@"id"], signature);
                    return;
                }
            }
        }
        if (candidates.count > 5) {
            MBGuestAppendHapticEvent([NSString stringWithFormat:
                @"MusicBrainz: %lu ambiguous recordings; checking Deezer",
                (unsigned long)candidates.count]);
            MBMHDeezerFallback(signature, @"Too many ambiguous MusicBrainz recordings");
            return;
        }
        NSMutableArray<NSString *> *ids = [NSMutableArray array];
        for (NSDictionary *item in candidates) [ids addObject:item[@"id"]];
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"MusicBrainz: comparing ISRCs from %lu candidate recordings",
            (unsigned long)ids.count]);
        MBMHLookupSharedISRCs(ids, 0, nil, signature);
    });
}

static void MBMHInspectCurrentSong(NSDictionary *info, NSString *title,
                                 NSString *artist, NSString *existingISRC) {
    if (!MBMHEnabled()) return;
    if (@available(iOS 18.0, *)) { /* supported */ } else { return; }
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
    gMBMHLastInsertionLogAt = 0;
    gMBMHCurrentTitle = [title copy];
    gMBMHCurrentArtist = [artist copy];
    gMBMHCurrentAlbum = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyAlbumTitle"));
    gMBMHCurrentDuration = MBMHDuration(info);
    gMBMHDeezerAttemptedSignature = nil;
    MBMHSetStatus(@"searching", @"Identifying exact recording and ISRC", signature);
    if (existingISRC.length) {
        MBGuestAppendHapticEvent(@"Player supplied an ISRC; no override necessary");
        MBMHSetStatus(@"ready", @"Player supplied its own ISRC", signature);
        return;
    }
    NSDictionary *cache = [NSUserDefaults.lcSharedDefaults dictionaryForKey:kMBMHCacheKey];
    NSString *cachedCode = cache[signature];
    if (MBMHValidISRC(cachedCode)) {
        gMBMHVerifiedISRC = cachedCode;
        MBMHSetStatus(@"ready", [NSString stringWithFormat:
            @"Cached Apple-compatible ISRC %@", cachedCode], signature);
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Verified ISRC cache hit: %@", cachedCode]);
        MBMHApplyVerifiedISRC(cachedCode, signature);
        return;
    }
    NSString *album = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyAlbumTitle"));
    MBMHSearchRecording(title, artist, MBMHDuration(info), album, signature);
}


// Lightweight fast path: no polling of MusicBrainz/Deezer here. An already
// verified ISRC is restored promptly if YouTube Music republishes metadata.
// Missing new songs still enter the existing bounded lookup only once.
static void MBMHFastRefreshNowPlaying(void) {
    if (!MBMHEnabled()) return;
    NSDictionary *info = MBMHNowPlayingInfo();
    if (![info isKindOfClass:NSDictionary.class] || info.count == 0) return;
    NSString *title = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyTitle"));
    NSString *artist = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyArtist"));
    if (title.length == 0 || artist.length == 0) return;
    NSString *isrc = MBGuestString(info,
        MBGuestStringSymbol("MPNowPlayingInfoPropertyInternationalStandardRecordingCode"));
    MBMHInspectCurrentSong(info, title, artist, isrc);
}


// Safe best-effort resync: republish ONLY the current song metadata (including
// the original elapsed time/rate) so iOS can re-evaluate its Music Haptics
// session. Apple's API does not support preloading or controlling the haptic
// download, so this does not claim the audio and haptics are forcibly aligned.
static void MBMHForceResync(void) {
    if (!MBMHEnabled()) return;
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    if (now - gMBMHLastResyncRequestAt < 5.0) return;
    gMBMHLastResyncRequestAt = now;
    MBMHFastRefreshNowPlaying();
    NSString *code = gMBMHVerifiedISRC;
    NSString *signature = [gMBMHSongSignature copy];
    if (!MBMHValidISRC(code) || !MBMHStillCurrent(signature)) {
        MBMHSetStatus(@"searching", @"Identificando pista antes de resincronizar",
                      signature);
        MBGuestAppendHapticEvent(@"Resync requested: waiting for verified ISRC");
        return;
    }
    NSDictionary *info = MBMHNowPlayingInfo();
    if (!info || ![MBMHSignature(MBGuestString(info,
                    MBGuestStringSymbol("MPMediaItemPropertyTitle")),
                    MBGuestString(info,
                    MBGuestStringSymbol("MPMediaItemPropertyArtist")),
                    info) isEqualToString:signature]) return;
    NSString *rateKey = MBGuestStringSymbol("MPNowPlayingInfoPropertyPlaybackRate");
    id rate = rateKey.length ? info[rateKey] : nil;
    if ([rate respondsToSelector:@selector(doubleValue)] &&
        [rate doubleValue] <= 0) {
        MBMHSetStatus(@"ready", @"Reproducción pausada; no es necesario resincronizar",
                      signature);
        return;
    }
    NSString *codeKey = MBGuestStringSymbol(
        "MPNowPlayingInfoPropertyInternationalStandardRecordingCode");
    if (!codeKey.length) return;
    NSString *existing = MBGuestString(info, codeKey);
    if (existing.length > 0 && ![existing isEqualToString:code]) return;
    id center = MBMHNowPlayingCenter();
    SEL setter = NSSelectorFromString(@"setNowPlayingInfo:");
    if (!center || ![center respondsToSelector:setter]) return;
    NSMutableDictionary *copy = [info mutableCopy];
    copy[codeKey] = code;
    // Keep MPNowPlayingInfoPropertyElapsedPlaybackTime and PlaybackRate
    // unchanged. Never seek, restart, pause, or call playback commands.
    ((void (*)(id, SEL, id))objc_msgSend)(center, setter, copy);
    gMBMHLastWriteAt = now;
    MBMHSetStatus(@"resyncing", @"Revalidando vibraciones con iOS…", signature);
    MBGuestAppendHapticEvent([NSString stringWithFormat:
        @"Resync requested for ISRC %@ (playback position preserved)", code]);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
        (int64_t)(8.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (!MBMHStillCurrent(signature)) return;
        if ([[NSUserDefaults.lcSharedDefaults stringForKey:kMBMHStatusKey]
             isEqualToString:@"resyncing"]) {
            MBMHSetStatus(@"ready",
                @"iOS no confirmó la nueva sincronización; pulsa ↻ para reintentar",
                signature);
        }
    });
}
