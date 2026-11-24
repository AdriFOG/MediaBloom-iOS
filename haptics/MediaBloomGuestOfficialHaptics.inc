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
    SEL sel = NSSelectorFromString(@"checkHapticTrackAvailabilityForMediaMatchingCode:completionHandler:");
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
            MBGuestAppendHapticEvent(@"No verified ISRC found for exact MusicBrainz recording");
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
                @"MusicBrainz: different recordings have no common ISRC; nothing injected");
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
                @"Ambiguous MusicBrainz recordings: one lacks an ISRC; nothing injected");
            return;
        }
        NSSet<NSString *> *available = [NSSet setWithArray:codes];
        NSMutableSet<NSString *> *common = intersection ?
            [intersection mutableCopy] : [available mutableCopy];
        if (intersection) [common intersectSet:available];
        if (common.count == 0) {
            MBGuestAppendHapticEvent(
                @"Ambiguous MusicBrainz recordings: ISRCs disagree; nothing injected");
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
    NSURL *url = MBMHSearchURL(title, artist);
    MBGuestAppendHapticEvent([NSString stringWithFormat:
        @"MusicBrainz: searching %@ — %@%@", title, artist,
        duration > 30 ? [NSString stringWithFormat:@" (%.0fs)", duration] : @""]);
    MBMHRequestJSON(url, ^(NSDictionary *response) {
        if (!MBMHStillCurrent(signature)) return;
        NSArray *recordings = response[@"recordings"];
        if (![recordings isKindOfClass:NSArray.class]) {
            MBGuestAppendHapticEvent(@"MusicBrainz unavailable; request details logged above");
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
            MBGuestAppendHapticEvent(@"MusicBrainz: no exact recording match; no ISRC inserted");
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
                @"MusicBrainz: %lu ambiguous recordings; no unsafe ISRC guess",
                (unsigned long)candidates.count]);
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
    NSString *album = MBGuestString(info, MBGuestStringSymbol("MPMediaItemPropertyAlbumTitle"));
    MBMHSearchRecording(title, artist, MBMHDuration(info), album, signature);
}
