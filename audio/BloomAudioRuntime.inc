// BloomAudio v0.2 experimental, compiled ONLY into the MediaBloom edition.
// It does not touch iOS system volume, output routes, or audio buffers.
// Stable guests: YouTube and YouTube Music. Spotify is an AVFoundation-only
// opt-in prototype; its native output path may not use these player classes.
// The runtime hooks are opt-in and default OFF until enabled in MediaBloom.
// This is a narrow AVFoundation proof of concept, not universal audio capture.

#include <math.h>
#include <objc/runtime.h>
#include <dlfcn.h>

static const char kMBAudioBaseVolumeKey = 0;
static NSHashTable *gMBAudioObjects;
static NSHashTable *gMBSpotifyEngines;   // Weak: no retention of Spotify's player graph.
static NSString *gMBAudioGuest;
static NSTimeInterval gMBSpotifyLastDiagnosticAt = 0;
static NSString *gMBSpotifyLastDiagnosticSignature = nil;
static const char kMBSpotifyBaseMixerVolumeKey = 0;
static BOOL MBSpotifyEngineIsRunning(id engine);
static NSInteger MBSpotifyActiveAVPlayers(void);
static NSInteger MBSpotifyRunningEngines(void);
static void MBSpotifyReportAudioState(BOOL force);
static void MBApplySpotifyEngineGain(void);
static float gMBAudioLastAppliedGain = 1.0f;
static void MBStartVolumeOverlay(void);
static void MBRefreshVolumeOverlayControls(void);
static IMP gMBAudioAVPlayerSetVolume;
static IMP gMBAudioAVPlayerPlay;
static IMP gMBAudioAVPlayerSetRate;
static IMP gMBAudioAVAudioPlayerSetVolume;
static IMP gMBAudioAVAudioPlayerPlay;
static IMP gMBAudioAVRendererSetVolume;
static IMP gMBSpotifyEngineStart;
static IMP gMBSpotifyEngineStop;

static float MBClampVolume(float value) {
    if (!isfinite(value)) return 1.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static BOOL MBIsSpotifyGuest(NSString *identifier) {
    return [identifier.lowercaseString isEqualToString:@"com.spotify.client"];
}

static BOOL MBIsSupportedGuest(NSString *identifier) {
    NSString *name = identifier.lowercaseString;
    return [name isEqualToString:@"com.google.ios.youtube"] ||
           [name isEqualToString:@"com.google.ios.youtubemusic"] ||
           MBIsSpotifyGuest(name);
}

// Presence is not playback. The original 3 detected players were a false
// positive: Spotify's real stream did not respond to changing their volume.
// Only expose volume controls when an observed AV player is playing or
// a Spotify-hosted AVAudioEngine is running. Neither guarantees audibility.
static BOOL MBSpotifyAudioAvailable(void) {
    return MBSpotifyActiveAVPlayers() > 0 || MBSpotifyRunningEngines() > 0;
}

static BOOL MBIsMediaHost(void) {
    // lcMainBundle may be LiveProcess.appex; its identifier retains our
    // distinct host prefix, while the primary BloomBox never has it.
    NSString *idString = NSUserDefaults.lcMainBundle.bundleIdentifier.lowercaseString;
    return [idString hasPrefix:@"com.kdt.bloomboxmedia"];
}

static float MBCurrentGain(void) {
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    if (![shared boolForKey:@"MediaBloomAudioProbeEnabledV1"]) return 1.0f;
    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:gMBAudioGuest ?: @""];
    id value = [shared objectForKey:key];
    float slider = [value respondsToSelector:@selector(floatValue)]
        ? MBClampVolume([value floatValue]) : 1.0f;
    // Human hearing does not interpret raw PCM/AVPlayer amplitude linearly.
    // A 50% control used to mean 0.50 signal (-6 dB) and felt too weak.
    // The exponent creates a stronger but smooth 0..100% attenuation curve.
    // 100% is unity, and 0% remains true silence; never amplify > 1.0.
    if (slider < 0.0001f) return 0.0f;
    return powf(slider, 2.6f);
}

static float MBRequestedBaseVolume(id player) {
    NSNumber *stored = objc_getAssociatedObject(player, &kMBAudioBaseVolumeKey);
    return stored ? MBClampVolume(stored.floatValue) : 1.0f;
}

static void MBSetOriginalVolume(id player, float requestedVolume) {
    float volume = MBClampVolume(requestedVolume) * MBCurrentGain();
    Class avPlayer = NSClassFromString(@"AVPlayer");
    Class avAudioPlayer = NSClassFromString(@"AVAudioPlayer");
    Class avRenderer = NSClassFromString(@"AVSampleBufferAudioRenderer");
    if (gMBAudioAVPlayerSetVolume && [player isKindOfClass:avPlayer]) {
        ((void(*)(id, SEL, float))gMBAudioAVPlayerSetVolume)(player, @selector(setVolume:), volume);
    } else if (gMBAudioAVAudioPlayerSetVolume && [player isKindOfClass:avAudioPlayer]) {
        ((void(*)(id, SEL, float))gMBAudioAVAudioPlayerSetVolume)(player, @selector(setVolume:), volume);
    } else if (gMBAudioAVRendererSetVolume && [player isKindOfClass:avRenderer]) {
        ((void(*)(id, SEL, float))gMBAudioAVRendererSetVolume)(player, @selector(setVolume:), volume);
    }
}

static void MBApplyLiveGain(void) {
    // This never touches iOS device volume or a system-wide audio session.
    gMBAudioLastAppliedGain = MBCurrentGain();
    for (id player in gMBAudioObjects.allObjects) {
        MBSetOriginalVolume(player, MBRequestedBaseVolume(player));
    }
    if (MBIsSpotifyGuest(gMBAudioGuest)) MBApplySpotifyEngineGain();
}

// Spotify audio diagnosis: distinguish real playback from unrelated UI/ad
// players. Public AVFoundation APIs are inspected on the guest's main queue.
static BOOL MBSpotifyEngineIsRunning(id engine) {
    return engine && [engine respondsToSelector:@selector(isRunning)] &&
        ((BOOL(*)(id, SEL))objc_msgSend)(engine, @selector(isRunning));
}

static NSInteger MBSpotifyActiveAVPlayers(void) {
    NSInteger count = 0;
    Class videoPlayer = NSClassFromString(@"AVPlayer");
    Class audioPlayer = NSClassFromString(@"AVAudioPlayer");
    for (id player in gMBAudioObjects.allObjects) {
        if (videoPlayer && [player isKindOfClass:videoPlayer]) {
            id item = ((id(*)(id, SEL))objc_msgSend)(player, @selector(currentItem));
            float rate = ((float(*)(id, SEL))objc_msgSend)(player, @selector(rate));
            if (item && rate > 0.01f) count++;
        } else if (audioPlayer && [player isKindOfClass:audioPlayer]) {
            if (((BOOL(*)(id, SEL))objc_msgSend)(player, @selector(isPlaying))) count++;
        }
        // AVSampleBufferAudioRenderer is not evidence of audible playback:
        // ready/status alone does not imply the output path is running.
    }
    return count;
}

static NSInteger MBSpotifyRunningEngines(void) {
    NSInteger count = 0;
    for (id engine in gMBSpotifyEngines.allObjects) {
        if (MBSpotifyEngineIsRunning(engine)) count++;
    }
    return count;
}

// The main mixer affects only its own AVAudioEngine graph, never the
// hardware/system volume. We do not replace the AudioUnit render callback.
static id MBSpotifyMainMixer(id engine) {
    return engine && [engine respondsToSelector:@selector(mainMixerNode)]
        ? ((id(*)(id, SEL))objc_msgSend)(engine, @selector(mainMixerNode)) : nil;
}
static void MBSpotifySetMixerGain(id engine, float gain) {
    if (!MBSpotifyEngineIsRunning(engine)) return;
    id mixer = MBSpotifyMainMixer(engine);
    if (!mixer || ![mixer respondsToSelector:@selector(setOutputVolume:)]) return;
    NSNumber *original = objc_getAssociatedObject(mixer, &kMBSpotifyBaseMixerVolumeKey);
    if (!original) {
        float base = [mixer respondsToSelector:@selector(outputVolume)]
            ? ((float(*)(id, SEL))objc_msgSend)(mixer, @selector(outputVolume)) : 1.0f;
        original = @(MBClampVolume(base));
        objc_setAssociatedObject(mixer, &kMBSpotifyBaseMixerVolumeKey,
                                 original, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    float adjusted = MBClampVolume(original.floatValue) * gain;
    ((void(*)(id, SEL, float))objc_msgSend)(mixer, @selector(setOutputVolume:), adjusted);
}
static void MBApplySpotifyEngineGain(void) {
    for (id engine in gMBSpotifyEngines.allObjects) {
        MBSpotifySetMixerGain(engine, MBCurrentGain());
    }
}

static void MBSpotifyTrackEngine(id engine) {
    if (!engine) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!gMBSpotifyEngines || !MBSpotifyEngineIsRunning(engine)) return;
        [gMBSpotifyEngines addObject:engine];
        MBSpotifySetMixerGain(engine, MBCurrentGain());
        MBSpotifyReportAudioState(YES);
    });
}
static BOOL MBSpotifyEngineStart(id engine, SEL cmd, NSError **error) {
    BOOL success = ((BOOL(*)(id, SEL, NSError **))gMBSpotifyEngineStart)(engine, cmd, error);
    if (success) MBSpotifyTrackEngine(engine);
    return success;
}
static void MBSpotifyEngineStop(id engine, SEL cmd) {
    dispatch_async(dispatch_get_main_queue(), ^{
        // Never hold an engine after stopping or reconfigure its audio route.
        [gMBSpotifyEngines removeObject:engine];
        MBSpotifyReportAudioState(YES);
    });
    ((void(*)(id, SEL))gMBSpotifyEngineStop)(engine, cmd);
}

static void MBSpotifyReportAudioState(BOOL force) {
    if (!MBIsSpotifyGuest(gMBAudioGuest)) return;
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    if (!force && now - gMBSpotifyLastDiagnosticAt < 1.4) return;
    gMBSpotifyLastDiagnosticAt = now;

    NSInteger detected = gMBAudioObjects.allObjects.count;
    NSInteger playing = MBSpotifyActiveAVPlayers();
    NSInteger engines = MBSpotifyRunningEngines();
    NSString *route = @"desconocida";
    Class sessionClass = NSClassFromString(@"AVAudioSession");
    if (sessionClass && [sessionClass respondsToSelector:@selector(sharedInstance)]) {
        id session = ((id(*)(id, SEL))objc_msgSend)(sessionClass, @selector(sharedInstance));
        if ([session respondsToSelector:@selector(currentRoute)]) {
            id routeObj = ((id(*)(id, SEL))objc_msgSend)(session, @selector(currentRoute));
            if ([routeObj respondsToSelector:@selector(outputs)]) {
                NSArray *outputs = ((id(*)(id, SEL))objc_msgSend)(routeObj, @selector(outputs));
                id output = outputs.firstObject;
                if ([output respondsToSelector:@selector(portType)]) {
                    route = ((id(*)(id, SEL))objc_msgSend)(output, @selector(portType)) ?: route;
                }
            }
        }
    }

    NSString *signature = [NSString stringWithFormat:@"%ld/%ld/%ld/%@",
        (long)detected, (long)playing, (long)engines, route];
    if (!force && [signature isEqualToString:gMBSpotifyLastDiagnosticSignature]) return;
    gMBSpotifyLastDiagnosticSignature = signature;

    NSMutableArray<NSString *> *details = [NSMutableArray array];
    Class avPlayer = NSClassFromString(@"AVPlayer");
    Class avAudioPlayer = NSClassFromString(@"AVAudioPlayer");
    for (id p in gMBAudioObjects.allObjects) {
        NSString *activity = @"sin señal activa";
        if (avPlayer && [p isKindOfClass:avPlayer]) {
            float rate = ((float(*)(id, SEL))objc_msgSend)(p, @selector(rate));
            BOOL item = ((id(*)(id, SEL))objc_msgSend)(p, @selector(currentItem)) != nil;
            activity = [NSString stringWithFormat:@"rate %.2f, item %@", rate, item ? @"sí":@"no"];
        } else if (avAudioPlayer && [p isKindOfClass:avAudioPlayer]) {
            activity = ((BOOL(*)(id, SEL))objc_msgSend)(p, @selector(isPlaying))
                ? @"isPlaying sí" : @"isPlaying no";
        }
        [details addObject:[NSString stringWithFormat:@"%@: %@",
                           NSStringFromClass([p class]), activity]];
    }
    NSString *summary = [NSString stringWithFormat:
       @"AV reproduciendo %ld/%ld; AVAudioEngine activos %ld; salida %@. %@",
       (long)playing, (long)detected, (long)engines, route,
       (playing || engines)
         ? @"Hay posibles rutas AV, pero falta confirmar que generan la música."
         : @"Si Spotify está sonando, probablemente usa AudioUnit/AudioQueue u otra ruta no controlada."];
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    [shared setInteger:playing forKey:@"MediaBloomSpotifyActivePlayersV2"];
    [shared setInteger:engines forKey:@"MediaBloomSpotifyActiveEnginesV2"];
    [shared setObject:summary forKey:@"MediaBloomSpotifyAudioDiagnosticV2"];

    NSString *line = [NSString stringWithFormat:@"%@ · %@",
                      [NSDateFormatter localizedStringFromDate:[NSDate date]
                          dateStyle:NSDateFormatterNoStyle timeStyle:NSDateFormatterMediumStyle],
                      [@([details count]) description]];
    (void)line; // Summary and per-player state are shared for a copyable report.
    NSString *report = [NSString stringWithFormat:@"%@\n%@",
                         summary, [details componentsJoinedByString:@"\n"]];
    [shared setObject:report forKey:@"MediaBloomSpotifyAudioDetailsV2"];
    [shared synchronize];
    NSLog(@"[BloomAudio] Spotify playback route: %@", summary);
}

static void MBTrackPlayerOnMain(id player) {
    if (!player || !gMBAudioObjects) return;
    if ([gMBAudioObjects containsObject:player]) return;
    [gMBAudioObjects addObject:player];
    if (MBIsSpotifyGuest(gMBAudioGuest)) {
        // Count only concrete, weakly held AVFoundation players. This makes
        // Spotify support observable without touching decoded audio or
        // claiming its proprietary playback pipeline is controllable.
        NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
        [shared setInteger:gMBAudioObjects.allObjects.count
                   forKey:@"MediaBloomSpotifyAVPlayersObservedV1"];
        [shared synchronize];
        NSLog(@"[BloomAudio] Spotify AVFoundation player observed: %@",
              NSStringFromClass([player class]));
    }
}

static void MBTrackPlayer(id player) {
    // Players must not be retained by the tweak. All timers and tracking
    // stay on the main queue; UIKit media playback must never be blocked.
    if ([NSThread isMainThread]) {
        MBTrackPlayerOnMain(player);
    } else {
        __weak id weakPlayer = player;
        dispatch_async(dispatch_get_main_queue(), ^{
            MBTrackPlayerOnMain(weakPlayer);
        });
    }
}

static void MBAVPlayerSetVolume(id player, SEL selector, float volume) {
    float base = MBClampVolume(volume);
    objc_setAssociatedObject(player, &kMBAudioBaseVolumeKey, @(base), OBJC_ASSOCIATION_RETAIN);
    MBTrackPlayer(player);
    ((void(*)(id,SEL,float))gMBAudioAVPlayerSetVolume)(
        player, selector, base * MBCurrentGain());
}
static void MBAVAudioPlayerSetVolume(id player, SEL selector, float volume) {
    float base = MBClampVolume(volume);
    objc_setAssociatedObject(player, &kMBAudioBaseVolumeKey, @(base), OBJC_ASSOCIATION_RETAIN);
    MBTrackPlayer(player);
    ((void(*)(id,SEL,float))gMBAudioAVAudioPlayerSetVolume)(
        player, selector, base * MBCurrentGain());
}
static void MBAVRendererSetVolume(id player, SEL selector, float volume) {
    float base = MBClampVolume(volume);
    objc_setAssociatedObject(player, &kMBAudioBaseVolumeKey, @(base), OBJC_ASSOCIATION_RETAIN);
    MBTrackPlayer(player);
    ((void(*)(id,SEL,float))gMBAudioAVRendererSetVolume)(
        player, selector, base * MBCurrentGain());
}

static void MBAVPlayerPlay(id player, SEL selector) {
    MBTrackPlayer(player);
    MBSetOriginalVolume(player, MBRequestedBaseVolume(player));
    ((void(*)(id,SEL))gMBAudioAVPlayerPlay)(player, selector);
}
static void MBAVPlayerSetRate(id player, SEL selector, float rate) {
    MBTrackPlayer(player);
    MBSetOriginalVolume(player, MBRequestedBaseVolume(player));
    ((void(*)(id,SEL,float))gMBAudioAVPlayerSetRate)(player, selector, rate);
}
static BOOL MBAVAudioPlayerPlay(id player, SEL selector) {
    MBTrackPlayer(player);
    MBSetOriginalVolume(player, MBRequestedBaseVolume(player));
    return ((BOOL(*)(id,SEL))gMBAudioAVAudioPlayerPlay)(player, selector);
}

static IMP MBHookInstanceMethod(NSString *className, SEL selector, IMP replacement) {
    Class cls = NSClassFromString(className);
    if (!cls) return NULL;
    Method m = class_getInstanceMethod(cls, selector);
    if (!m) return NULL;
    return method_setImplementation(m, replacement);
}

static void MBInstallAudioHooks(void) {
    if (!MBIsMediaHost()) return;
    NSString *guest = NSUserDefaults.lcGuestAppId;
    if (!MBIsSupportedGuest(guest)) return;
    if (![NSUserDefaults.lcSharedDefaults boolForKey:@"MediaBloomAudioProbeEnabledV1"]) {
        NSLog(@"[BloomAudio] disabled for %@", guest);
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        // Explicitly load AVFoundation only in supported opted-in Media guests.
        // On a future framework API change, missing selectors are simply skipped.
        dlopen("/System/Library/Frameworks/AVFoundation.framework/AVFoundation", RTLD_LAZY);
        gMBAudioGuest = [guest.lowercaseString copy];
        gMBAudioObjects = [NSHashTable weakObjectsHashTable];
        if (MBIsSpotifyGuest(gMBAudioGuest)) {
            [NSUserDefaults.lcSharedDefaults setInteger:0
                         forKey:@"MediaBloomSpotifyAVPlayersObservedV1"];
            [NSUserDefaults.lcSharedDefaults synchronize];
        }
        gMBAudioLastAppliedGain = MBCurrentGain();
        gMBAudioAVPlayerSetVolume = MBHookInstanceMethod(
            @"AVPlayer", @selector(setVolume:), (IMP)MBAVPlayerSetVolume);
        gMBAudioAVPlayerPlay = MBHookInstanceMethod(
            @"AVPlayer", @selector(play), (IMP)MBAVPlayerPlay);
        gMBAudioAVPlayerSetRate = MBHookInstanceMethod(
            @"AVPlayer", @selector(setRate:), (IMP)MBAVPlayerSetRate);
        gMBAudioAVAudioPlayerSetVolume = MBHookInstanceMethod(
            @"AVAudioPlayer", @selector(setVolume:), (IMP)MBAVAudioPlayerSetVolume);
        gMBAudioAVAudioPlayerPlay = MBHookInstanceMethod(
            @"AVAudioPlayer", @selector(play), (IMP)MBAVAudioPlayerPlay);
        gMBAudioAVRendererSetVolume = MBHookInstanceMethod(
            @"AVSampleBufferAudioRenderer", @selector(setVolume:), (IMP)MBAVRendererSetVolume);

        // Shared defaults may be changed by MediaBloom while a guest is playing.
        // Reapply through original setters, never recursively through hooks.
        [NSTimer scheduledTimerWithTimeInterval:0.3 repeats:YES block:^(NSTimer *timer) {
            // A Control Center App Intent runs in a DIFFERENT process.
            // Refresh the shared suite before sampling its new gain value.
            [NSUserDefaults.lcSharedDefaults synchronize];
            float gain = MBCurrentGain();
            if (MBIsSpotifyGuest(gMBAudioGuest)) {
                // Update the Spotify overlay only if a tracked AVFoundation
                // player has become available since the previous tick.
                MBRefreshVolumeOverlayControls();
            }
            if (fabsf(gain - gMBAudioLastAppliedGain) < 0.001f) return;
            gMBAudioLastAppliedGain = gain;
            for (id player in gMBAudioObjects.allObjects) {
                MBSetOriginalVolume(player, MBRequestedBaseVolume(player));
            }
        }];
        // UIKit volume bubble lives in the same guest scene. Unlike the
        // launcher slider, it stays reachable while content is playing.
        MBStartVolumeOverlay();
        NSLog(@"[BloomAudio] installed for %@ (AVPlayer=%d, AudioPlayer=%d, Renderer=%d)",
              guest, gMBAudioAVPlayerSetVolume != NULL,
              gMBAudioAVAudioPlayerSetVolume != NULL,
              gMBAudioAVRendererSetVolume != NULL);
    });
}
