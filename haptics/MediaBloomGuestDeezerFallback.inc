// Optional Deezer metadata fallback for MediaBloom's *official* Music Haptics
// experiment. No Deezer login, no audio access, no track playback or downloads.
// Never publishes a guessed ISRC: all candidate tracks must agree on a valid
// code, and Apple must independently report a haptic track for that code.
#import <Foundation/Foundation.h>

static NSURL *MBMHDZSearchURL(NSString *title, NSString *artist) {
    NSURLComponents *components = [NSURLComponents componentsWithString:
        @"https://api.deezer.com/search"];
    components.queryItems = @[
        [NSURLQueryItem queryItemWithName:@"q"
                   value:[NSString stringWithFormat:@"%@ %@", title ?: @"", artist ?: @""]],
        [NSURLQueryItem queryItemWithName:@"limit" value:@"50"]
    ];
    return components.URL;
}

static NSURL *MBMHDZTrackURL(id identifier) {
    if (![identifier respondsToSelector:@selector(longLongValue)]) return nil;
    long long trackID = [identifier longLongValue];
    if (trackID <= 0) return nil;
    return [NSURL URLWithString:
        [NSString stringWithFormat:@"https://api.deezer.com/track/%lld", trackID]];
}

static void MBMHDZRequest(NSURL *url, NSString *signature,
                          void (^completion)(NSDictionary *)) {
    if (!MBMHStillCurrent(signature)) return;
    if (!url || ![url.scheme isEqualToString:@"https"] ||
        ![url.host isEqualToString:@"api.deezer.com"]) return;
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
    request.timeoutInterval = 12;
    [request setValue:@"MediaBloomMusicHaptics/0.3 (https://github.com/AdriFOG/MediaBloom-iOS)"
   forHTTPHeaderField:@"User-Agent"];
    [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
    [[NSURLSession.sharedSession dataTaskWithRequest:request
        completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class]
            ? [(NSHTTPURLResponse *)response statusCode] : 0;
        NSDictionary *json = nil;
        if (!error && status == 200 && data.length > 0 && data.length < 700000) {
            id parsed = [NSJSONSerialization JSONObjectWithData:data
                                               options:0 error:nil];
            if ([parsed isKindOfClass:NSDictionary.class]) json = parsed;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!MBMHStillCurrent(signature)) return;
            if (!json || json[@"error"]) {
                NSString *why = error ?
                    [NSString stringWithFormat:@"network %ld", (long)error.code] :
                    [NSString stringWithFormat:@"HTTP %ld", (long)status];
                MBGuestAppendHapticEvent([NSString stringWithFormat:
                    @"Deezer metadata unavailable: %@", why]);
                if (completion) completion(nil);
                return;
            }
            if (completion) completion(json);
        });
    }] resume];
}

static BOOL MBMHDZArtistMatch(NSString *expected, NSString *actual) {
    NSString *actualName = MBMHNormalized(actual);
    if (actualName.length == 0) return NO;
    if ([actualName isEqualToString:MBMHNormalized(expected)]) return YES;
    NSString *separated = [expected stringByReplacingOccurrencesOfString:@" feat. "
                                                              withString:@" & "];
    separated = [separated stringByReplacingOccurrencesOfString:@" featuring "
                                                     withString:@" & "];
    separated = [separated stringByReplacingOccurrencesOfString:@", "
                                                     withString:@" & "];
    for (NSString *part in [separated componentsSeparatedByString:@" & "]) {
        if ([MBMHNormalized(part) isEqualToString:actualName]) return YES;
    }
    return NO;
}

// Alternate releases only qualify when title (INCLUDING remix/radio/live
// qualifiers), artist, and duration match. Never silently drop title suffixes.
static BOOL MBMHDZTrackMatchesMode(NSDictionary *track, BOOL alternateRelease) {
    if (![track isKindOfClass:NSDictionary.class]) return NO;
    NSString *trackTitle = track[@"title"];
    if (![trackTitle isKindOfClass:NSString.class]) return NO;
    if (![MBMHNormalized(trackTitle) isEqualToString:
          MBMHNormalized(gMBMHCurrentTitle)]) return NO;
    NSDictionary *artist = track[@"artist"];
    NSString *artistName = [artist isKindOfClass:NSDictionary.class] ?
        artist[@"name"] : nil;
    if (![artistName isKindOfClass:NSString.class] ||
        !MBMHDZArtistMatch(gMBMHCurrentArtist, artistName)) return NO;
    NSNumber *duration = track[@"duration"];
    if (alternateRelease) {
        // Without a measured duration, cross-album matches are unsafe.
        if (gMBMHCurrentDuration <= 30 ||
            ![duration respondsToSelector:@selector(doubleValue)] ||
            fabs([duration doubleValue] - gMBMHCurrentDuration) > 2.0) return NO;
    } else if (gMBMHCurrentDuration > 30) {
        if (![duration respondsToSelector:@selector(doubleValue)] ||
            [duration doubleValue] <= 30 ||
            fabs([duration doubleValue] - gMBMHCurrentDuration) > 15.0) return NO;
    }
    if (!alternateRelease && gMBMHCurrentAlbum.length) {
        NSDictionary *album = track[@"album"];
        NSString *albumName = [album isKindOfClass:NSDictionary.class] ?
            album[@"title"] : nil;
        if (![MBMHNormalized(albumName) isEqualToString:
              MBMHNormalized(gMBMHCurrentAlbum)]) return NO;
    }
    return YES;
}

static BOOL MBMHDZTrackMatches(NSDictionary *track) {
    return MBMHDZTrackMatchesMode(track, NO);
}

static BOOL MBMHDZCollaboratorsMatch(NSDictionary *track) {
    NSString *name = [track[@"artist"] isKindOfClass:NSDictionary.class] ?
        track[@"artist"][@"name"] : nil;
    if ([MBMHNormalized(name) isEqualToString:
         MBMHNormalized(gMBMHCurrentArtist)]) return YES;
    NSString *expected = [gMBMHCurrentArtist ?: @""
        stringByReplacingOccurrencesOfString:@" featuring " withString:@" & "];
    NSArray *parts = [expected componentsSeparatedByString:@" & "];
    if (parts.count <= 1) return MBMHDZArtistMatch(gMBMHCurrentArtist, name);
    NSMutableSet<NSString *> *credits = [NSMutableSet set];
    if (name.length) [credits addObject:MBMHNormalized(name)];
    NSArray *contributors = track[@"contributors"];
    if ([contributors isKindOfClass:NSArray.class]) {
        for (id item in contributors) {
            if (![item isKindOfClass:NSDictionary.class]) continue;
            NSString *contributorName = item[@"name"];
            if ([contributorName isKindOfClass:NSString.class])
                [credits addObject:MBMHNormalized(contributorName)];
        }
    }
    for (NSString *part in parts) {
        if (![credits containsObject:MBMHNormalized(part)]) return NO;
    }
    return YES;
}

static void MBMHDZCheckCandidates(NSArray<NSDictionary *> *candidates,
                                  NSUInteger index, NSString *commonCode,
                                  NSString *signature, BOOL alternateRelease) {
    if (!MBMHStillCurrent(signature)) return;
    if (index >= candidates.count) {
        if (!MBMHValidISRC(commonCode)) {
            MBGuestAppendHapticEvent(
                @"Deezer: could not verify one shared ISRC; no code inserted");
            MBMHSetStatus(@"unavailable", @"No trustworthy Deezer ISRC found", signature);
            return;
        }
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Deezer confirms ISRC %@ for %lu matching track(s); checking Apple",
            commonCode, (unsigned long)candidates.count]);
        MBMHSetStatus(@"checking", [NSString stringWithFormat:
            @"Deezer matched ISRC %@; checking Apple's catalog", commonCode], signature);
        MBMHCheckISRCs(@[commonCode], 0, signature);
        return;
    }
    NSURL *url = MBMHDZTrackURL(candidates[index][@"id"]);
    if (!url) {
        MBGuestAppendHapticEvent(@"Deezer returned invalid track ID");
        MBMHSetStatus(@"unavailable", @"Invalid Deezer track ID", signature);
        return;
    }
    MBMHDZRequest(url, signature, ^(NSDictionary *detail) {
        if (!MBMHStillCurrent(signature)) return;
        if (!detail || !MBMHDZTrackMatchesMode(detail, alternateRelease) ||
            !MBMHDZCollaboratorsMatch(detail)) {
            MBGuestAppendHapticEvent(
                @"Deezer track details did not match title/artist/version/duration");
            MBMHSetStatus(@"unavailable", @"Deezer version or credits did not match", signature);
            return;
        }
        NSString *code = detail[@"isrc"];
        if (!MBMHValidISRC(code)) {
            MBGuestAppendHapticEvent(@"Deezer recording has no valid ISRC");
            MBMHSetStatus(@"unavailable", @"Deezer recording has no valid ISRC", signature);
            return;
        }
        code = code.uppercaseString;
        if (commonCode.length && ![commonCode isEqualToString:code]) {
            MBGuestAppendHapticEvent(
                @"Deezer candidates have different ISRCs; nothing inserted");
            MBMHSetStatus(@"unavailable", @"Different releases have different ISRCs", signature);
            return;
        }
        MBMHDZCheckCandidates(candidates, index + 1, code, signature, alternateRelease);
    });
}

static void MBMHDeezerFallback(NSString *signature, NSString *reason) {
    if (!MBMHStillCurrent(signature)) return;
    if ([gMBMHDeezerAttemptedSignature isEqualToString:signature]) return;
    gMBMHDeezerAttemptedSignature = [signature copy];
    if (!gMBMHCurrentTitle.length || !gMBMHCurrentArtist.length) return;
    MBGuestAppendHapticEvent([NSString stringWithFormat:
        @"Deezer fallback: %@", reason ?: @"MusicBrainz incomplete"]);
    MBMHSetStatus(@"searching", @"Searching exact versions in Deezer", signature);
    NSURL *url = MBMHDZSearchURL(gMBMHCurrentTitle, gMBMHCurrentArtist);
    MBMHDZRequest(url, signature, ^(NSDictionary *results) {
        if (!MBMHStillCurrent(signature)) return;
        NSArray *raw = results[@"data"];
        if (![raw isKindOfClass:NSArray.class]) {
            MBGuestAppendHapticEvent(@"Deezer search returned no track list");
            MBMHSetStatus(@"unavailable", @"Deezer search not available", signature);
            return;
        }
        NSMutableArray<NSDictionary *> *exact = [NSMutableArray array];
        NSMutableArray<NSDictionary *> *otherReleases = [NSMutableArray array];
        NSMutableSet *seenIDs = [NSMutableSet set];
        for (id item in raw) {
            if (![item isKindOfClass:NSDictionary.class]) continue;
            NSDictionary *track = item;
            BOOL sameRelease = MBMHDZTrackMatches(track);
            BOOL alternative = !sameRelease && MBMHDZTrackMatchesMode(track, YES);
            if (!sameRelease && !alternative) continue;
            id identifier = track[@"id"];
            if (![identifier respondsToSelector:@selector(longLongValue)] ||
                [identifier longLongValue] <= 0) continue;
            NSNumber *trackID = @([identifier longLongValue]);
            if ([seenIDs containsObject:trackID]) continue;
            [seenIDs addObject:trackID];
            if (sameRelease) [exact addObject:track];
            else [otherReleases addObject:track];
        }
        BOOL alternateRelease = exact.count == 0;
        NSArray<NSDictionary *> *candidates = alternateRelease ? otherReleases : exact;
        // Cross-album results must independently AGREE on their code.
        // A single otherwise-similar copy is not enough evidence.
        if (alternateRelease && candidates.count < 2) {
            MBGuestAppendHapticEvent(
                @"Deezer: no trustworthy same-version recording; alternate releases unverified");
            MBMHSetStatus(@"unavailable", @"No safe matching recording version", signature);
            return;
        }
        if (candidates.count == 0) {
            MBGuestAppendHapticEvent(
                @"Deezer: no exact track matching title, artist, album and duration");
            MBMHSetStatus(@"unavailable", @"No exact track matching version and credits",
                          signature);
            return;
        }
        if (candidates.count > 5) {
            MBGuestAppendHapticEvent([NSString stringWithFormat:
                @"Deezer: %lu possible tracks, too ambiguous to choose an ISRC",
                (unsigned long)candidates.count]);
            MBMHSetStatus(@"unavailable", @"Too many different Deezer versions", signature);
            return;
        }
        MBGuestAppendHapticEvent([NSString stringWithFormat:
            @"Deezer: verifying ISRC for %lu %@ track(s)",
            (unsigned long)candidates.count,
            alternateRelease ? @"same-version alternate-release" : @"exact"]);
        MBMHSetStatus(@"checking", @"Verifying same recording across releases",
                      signature);
        MBMHDZCheckCandidates(candidates, 0, nil, signature, alternateRelease);
    });
}
