// BloomAudio v0.2 experimental, compiled ONLY into the MediaBloom edition.
// It does not touch iOS system volume, output routes, or audio buffers.
// Supported initial guests: YouTube and YouTube Music only.
// The runtime hooks are opt-in and default OFF until enabled in MediaBloom.
// This is a narrow AVFoundation proof of concept, not universal audio capture.

#include <math.h>
#include <objc/runtime.h>
#include <dlfcn.h>

static const char kMBAudioBaseVolumeKey = 0;
static NSHashTable *gMBAudioObjects;
static NSString *gMBAudioGuest;
static float gMBAudioLastAppliedGain = 1.0f;
static void MBStartVolumeOverlay(void);
static IMP gMBAudioAVPlayerSetVolume;
static IMP gMBAudioAVPlayerPlay;
static IMP gMBAudioAVPlayerSetRate;
static IMP gMBAudioAVAudioPlayerSetVolume;
static IMP gMBAudioAVAudioPlayerPlay;
static IMP gMBAudioAVRendererSetVolume;

static float MBClampVolume(float value) {
    if (!isfinite(value)) return 1.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static BOOL MBIsSupportedGuest(NSString *identifier) {
    NSString *name = identifier.lowercaseString;
    return [name isEqualToString:@"com.google.ios.youtube"] ||
           [name isEqualToString:@"com.google.ios.youtubemusic"];
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
    // The floating slider runs in the guest process. Native player setters
    // are called directly, not through our hooks, so changes apply immediately.
    gMBAudioLastAppliedGain = MBCurrentGain();
    for (id player in gMBAudioObjects.allObjects) {
        MBSetOriginalVolume(player, MBRequestedBaseVolume(player));
    }
}

static void MBTrackPlayer(id player) {
    // Players must not be retained by the tweak. All timers and tracking
    // stay on the main queue; UIKit media playback must never be blocked.
    if ([NSThread isMainThread]) {
        [gMBAudioObjects addObject:player];
    } else {
        __weak id weakPlayer = player;
        dispatch_async(dispatch_get_main_queue(), ^{
            id strongPlayer = weakPlayer;
            if (strongPlayer) [gMBAudioObjects addObject:strongPlayer];
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
