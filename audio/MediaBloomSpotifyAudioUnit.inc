// BloomAudio Spotify-only AudioUnit output probe.
// No DRM/key access, no media export, no AudioQueue interception, no master volume.
// Hook only the Spotify guest through the already installed LiveContainer substrate.
// Render callback: zero allocations, zero ObjC, no locks or I/O.
#import <AudioToolbox/AudioToolbox.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

typedef OSStatus (*MBSpotifyAUSetPropertyFn)(AudioUnit, AudioUnitPropertyID,
    AudioUnitScope, AudioUnitElement, const void *, UInt32);
typedef OSStatus (*MBSpotifyAUStartFn)(AudioUnit);
typedef OSStatus (*MBSpotifyAUGetPropertyFn)(AudioUnit, AudioUnitPropertyID,
    AudioUnitScope, AudioUnitElement, void *, UInt32 *);
typedef AudioComponent (*MBSpotifyAUComponentFn)(AudioComponentInstance);
typedef OSStatus (*MBSpotifyAUDescriptionFn)(AudioComponent, AudioComponentDescription *);

typedef struct {
    AudioUnit unit;
    AudioUnitElement element;
    AURenderCallback original;
    void *originalRefCon;
    _Atomic(int) pcmKind;  // 0 unknown, 1 float32, 2 signed int16, 3 float64
    _Atomic(bool) ready;  // publish immutable context before other configuration hooks read it
} MBSpotifyAUCallbackContext;

#define MB_SPOTIFY_AU_CONTEXT_CAP 128u
static MBSpotifyAUCallbackContext gMBSpotifyAUContexts[MB_SPOTIFY_AU_CONTEXT_CAP];
static _Atomic(unsigned) gMBSpotifyAUContextCount;
static _Atomic(uint64_t) gMBSpotifyAURenderCount;
static _Atomic(uint64_t) gMBSpotifyAUNonzeroPCMCount;
static _Atomic(uint64_t) gMBSpotifyAUAttenuatedCount;
static _Atomic(uint64_t) gMBSpotifyAUStartCount;
static _Atomic(float) gMBSpotifyAUGain = 1.0f;
static BOOL gMBSpotifyAUHookInstalled;
static NSTimeInterval gMBSpotifyAULastStatusAt;
static MBSpotifyAUSetPropertyFn gMBSpotifyAUOriginalSetProperty;
static MBSpotifyAUStartFn gMBSpotifyAUOriginalStart;
static MBSpotifyAUGetPropertyFn gMBSpotifyAUOriginalGetProperty;
static MBSpotifyAUComponentFn gMBSpotifyAUGetComponent;
static MBSpotifyAUDescriptionFn gMBSpotifyAUGetDescription;

static BOOL MBSpotifyAUIsPlaybackOutput(AudioUnit unit) {
    if (!unit || !gMBSpotifyAUGetComponent || !gMBSpotifyAUGetDescription) return NO;
    AudioComponent component = gMBSpotifyAUGetComponent(unit);
    AudioComponentDescription desc = {0};
    return component && gMBSpotifyAUGetDescription(component, &desc) == noErr &&
        desc.componentType == kAudioUnitType_Output &&
        desc.componentSubType == kAudioUnitSubType_RemoteIO;
}

static int MBSpotifyAUFormatKind(const AudioStreamBasicDescription *format) {
    if (!format || format->mFormatID != kAudioFormatLinearPCM ||
        format->mBytesPerFrame == 0 ||
        !(format->mFormatFlags & kAudioFormatFlagIsPacked) ||
        (format->mFormatFlags & kAudioFormatFlagIsBigEndian) ||
        (format->mFormatFlags & kAudioFormatFlagIsNonMixable)) return 0;
    // Only packed canonical PCM; compressed/planar byte layouts are never
    // guessed. Non-interleaved PCM still contains packed per-channel samples.
    int kind = 0;
    if (format->mFormatFlags & kAudioFormatFlagIsFloat) {
        kind = format->mBitsPerChannel == 32 ? 1 :
               format->mBitsPerChannel == 64 ? 3 : 0;
    } else if (format->mFormatFlags & kAudioFormatFlagIsSignedInteger) {
        kind = format->mBitsPerChannel == 16 ? 2 : 0;
    }
    if (!kind || format->mChannelsPerFrame == 0 || format->mChannelsPerFrame > 32 ||
        format->mFramesPerPacket != 1 || format->mBytesPerPacket != format->mBytesPerFrame)
        return 0;
    UInt32 width = kind == 3 ? 8 : kind == 2 ? 2 : 4;
    UInt32 channels = (format->mFormatFlags & kAudioFormatFlagIsNonInterleaved)
        ? 1 : format->mChannelsPerFrame;
    return format->mBytesPerFrame == width * channels ? kind : 0;
}

static void MBSpotifyAUUpdateFormat(MBSpotifyAUCallbackContext *context,
                                    const AudioStreamBasicDescription *format) {
    if (!context || !format) return;
    atomic_store_explicit(&context->pcmKind,
                          MBSpotifyAUFormatKind(format), memory_order_relaxed);
}

static void MBSpotifyAUProbeFormat(MBSpotifyAUCallbackContext *context) {
    if (!context || !gMBSpotifyAUOriginalGetProperty) return;
    AudioStreamBasicDescription format = {0};
    UInt32 length = sizeof(format);
    OSStatus status = gMBSpotifyAUOriginalGetProperty(context->unit,
        kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
        context->element, &format, &length);
    if (status == noErr && length == sizeof(format))
        MBSpotifyAUUpdateFormat(context, &format);
    else
        atomic_store_explicit(&context->pcmKind, 0, memory_order_relaxed);
}

static OSStatus MBSpotifyAURenderCallback(void *opaque,
        AudioUnitRenderActionFlags *flags, const AudioTimeStamp *stamp,
        UInt32 bus, UInt32 frames, AudioBufferList *data) {
    MBSpotifyAUCallbackContext *context = (MBSpotifyAUCallbackContext *)opaque;
    if (!context || !context->original) return -50;
    OSStatus status = context->original(context->originalRefCon,
                                        flags, stamp, bus, frames, data);
    if (status != noErr || bus != 0 || !data || frames == 0 ||
        (flags && (*flags & kAudioUnitRenderAction_OutputIsSilence))) return status;
    atomic_fetch_add_explicit(&gMBSpotifyAURenderCount, 1, memory_order_relaxed);

    int kind = atomic_load_explicit(&context->pcmKind, memory_order_relaxed);
    if (!kind || data->mNumberBuffers == 0 || data->mNumberBuffers > 8)
        return status;
    UInt32 width = kind == 3 ? 8 : kind == 2 ? 2 : 4;
    // Validate the complete list before modifying anything. mDataByteSize may
    // describe capacity rather than the actual frame count; never touch its tail.
    for (UInt32 i = 0; i < data->mNumberBuffers; ++i) {
        const AudioBuffer *buffer = &data->mBuffers[i];
        uint64_t bytes = (uint64_t)frames * buffer->mNumberChannels * width;
        if (!buffer->mData || !buffer->mNumberChannels || buffer->mNumberChannels > 32 ||
            !bytes || bytes > 262144 || bytes > buffer->mDataByteSize ||
            ((uintptr_t)buffer->mData % width) != 0) return status;
    }
    float gain = atomic_load_explicit(&gMBSpotifyAUGain, memory_order_relaxed);
    BOOL hasSignal = NO;
    BOOL attenuated = NO;
    for (UInt32 bufferIndex = 0; bufferIndex < data->mNumberBuffers; ++bufferIndex) {
        AudioBuffer *buffer = &data->mBuffers[bufferIndex];
        size_t samples = (size_t)frames * buffer->mNumberChannels;
        if (kind == 1) {
            float *pcm = (float *)buffer->mData;
            for (size_t i = 0; i < samples; ++i) {
                float sample = pcm[i];
                if (isfinite(sample) && fabsf(sample) > 0.00001f)
                    hasSignal = YES;
                if (gain < 0.999f && isfinite(sample)) {
                    pcm[i] = sample * gain;
                    attenuated = YES;
                }
            }
        } else if (kind == 2) {
            int16_t *pcm = (int16_t *)buffer->mData;
            for (size_t i = 0; i < samples; ++i) {
                int16_t sample = pcm[i];
                if (sample > 1 || sample < -1) hasSignal = YES;
                if (gain < 0.999f) {
                    pcm[i] = (int16_t)lrintf((float)sample * gain);
                    attenuated = YES;
                }
            }
        } else if (kind == 3) {
            double *pcm = (double *)buffer->mData;
            for (size_t i = 0; i < samples; ++i) {
                double sample = pcm[i];
                if (isfinite(sample) && fabs(sample) > 0.00001)
                    hasSignal = YES;
                if (gain < 0.999f && isfinite(sample)) {
                    pcm[i] = sample * (double)gain;
                    attenuated = YES;
                }
            }
        }
    }
    if (hasSignal) atomic_fetch_add_explicit(&gMBSpotifyAUNonzeroPCMCount,
                                              1, memory_order_relaxed);
    if (attenuated && hasSignal)
        atomic_fetch_add_explicit(&gMBSpotifyAUAttenuatedCount,
                                  1, memory_order_relaxed);
    return status;
}

static OSStatus MBSpotifyAUHookSetProperty(AudioUnit unit,
        AudioUnitPropertyID property, AudioUnitScope scope,
        AudioUnitElement element, const void *value, UInt32 size) {
    if (!gMBSpotifyAUOriginalSetProperty) return -50;
    // Only the RemoteIO playback input bus (0), never recording input or
    // intermediate processing units that may be mixed again downstream.
    if (property == kAudioUnitProperty_SetRenderCallback &&
        scope == kAudioUnitScope_Input && element == 0 &&
        value && size == sizeof(AURenderCallbackStruct) &&
        MBSpotifyAUIsPlaybackOutput(unit)) {
        const AURenderCallbackStruct *source = (const AURenderCallbackStruct *)value;
        if (source->inputProc && source->inputProc != MBSpotifyAURenderCallback) {
            unsigned idx = atomic_fetch_add_explicit(&gMBSpotifyAUContextCount,
                                                       1, memory_order_relaxed);
            if (idx < MB_SPOTIFY_AU_CONTEXT_CAP) {
                MBSpotifyAUCallbackContext *ctx = &gMBSpotifyAUContexts[idx];
                ctx->unit = unit;
                ctx->element = element;
                ctx->original = source->inputProc;
                ctx->originalRefCon = source->inputProcRefCon;
                MBSpotifyAUProbeFormat(ctx);
                atomic_store_explicit(&ctx->ready, true, memory_order_release);
                AURenderCallbackStruct replacement = {
                    .inputProc = MBSpotifyAURenderCallback,
                    .inputProcRefCon = ctx
                };
                OSStatus result = gMBSpotifyAUOriginalSetProperty(unit, property,
                    scope, element, &replacement, sizeof(replacement));
                if (result == noErr) return result;
                // Never break a Spotify callback if wrapping is rejected.
            }
        }
    }
    OSStatus status = gMBSpotifyAUOriginalSetProperty(unit, property,
                                                       scope, element, value, size);
    if (status == noErr && property == kAudioUnitProperty_StreamFormat &&
        scope == kAudioUnitScope_Input && element == 0 &&
        value && size == sizeof(AudioStreamBasicDescription)) {
        unsigned used = atomic_load_explicit(&gMBSpotifyAUContextCount,
                                              memory_order_relaxed);
        if (used > MB_SPOTIFY_AU_CONTEXT_CAP) used = MB_SPOTIFY_AU_CONTEXT_CAP;
        for (unsigned i = 0; i < used; ++i) {
            MBSpotifyAUCallbackContext *ctx = &gMBSpotifyAUContexts[i];
            if (atomic_load_explicit(&ctx->ready, memory_order_acquire) &&
                ctx->unit == unit && ctx->element == element)
                MBSpotifyAUUpdateFormat(ctx,
                    (const AudioStreamBasicDescription *)value);
        }
    }
    return status;
}

static OSStatus MBSpotifyAUHookStart(AudioUnit unit) {
    if (!gMBSpotifyAUOriginalStart) return -50;
    unsigned used = atomic_load_explicit(&gMBSpotifyAUContextCount, memory_order_relaxed);
    if (used > MB_SPOTIFY_AU_CONTEXT_CAP) used = MB_SPOTIFY_AU_CONTEXT_CAP;
    for (unsigned i = 0; i < used; ++i) {
        MBSpotifyAUCallbackContext *ctx = &gMBSpotifyAUContexts[i];
        if (atomic_load_explicit(&ctx->ready, memory_order_acquire) && ctx->unit == unit)
            MBSpotifyAUProbeFormat(ctx);
    }
    OSStatus result = gMBSpotifyAUOriginalStart(unit);
    if (result == noErr)
        atomic_fetch_add_explicit(&gMBSpotifyAUStartCount, 1,
                                  memory_order_relaxed);
    return result;
}

static void MBSpotifyAUUpdateGain(float gain) {
    if (!isfinite(gain)) gain = 1.0f;
    atomic_store_explicit(&gMBSpotifyAUGain,
                          fmaxf(0.0f, fminf(gain, 1.0f)),
                          memory_order_relaxed);
}

static BOOL MBSpotifyAUHasConfirmedPCM(void) {
    return atomic_load_explicit(&gMBSpotifyAUNonzeroPCMCount,
                                memory_order_relaxed) > 0;
}

static void MBSpotifyAUInstallHooks(void) {
    // Called only inside the opted-in MediaBloom Spotify guest, after
    // LiveContainer has loaded its CydiaSubstrate runtime.
    MBSpotifyAUUpdateGain(MBCurrentGain());
    typedef void (*HookFunction)(void *, void *, void **);
    HookFunction hook = (HookFunction)dlsym(RTLD_DEFAULT, "MSHookFunction");
    void *toolbox = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox",
                           RTLD_LAZY);
    if (!hook || !toolbox) {
        gMBSpotifyAUHookInstalled = NO;
        NSLog(@"[BloomAudio] Spotify AudioUnit probe unavailable: substrate=%d toolbox=%d",
              hook != NULL, toolbox != NULL);
        return;
    }
    void *set = dlsym(toolbox, "AudioUnitSetProperty");
    void *start = dlsym(toolbox, "AudioOutputUnitStart");
    gMBSpotifyAUOriginalGetProperty =
        (MBSpotifyAUGetPropertyFn)dlsym(toolbox, "AudioUnitGetProperty");
    gMBSpotifyAUGetComponent = (MBSpotifyAUComponentFn)dlsym(toolbox, "AudioComponentInstanceGetComponent");
    gMBSpotifyAUGetDescription = (MBSpotifyAUDescriptionFn)dlsym(toolbox, "AudioComponentGetDescription");
    if (!gMBSpotifyAUGetComponent || !gMBSpotifyAUGetDescription ||
        !gMBSpotifyAUOriginalGetProperty) {
        NSLog(@"[BloomAudio] Spotify AudioUnit probe skipped: output or format validation unavailable");
        return;
    }
    if (set) hook(set, (void *)MBSpotifyAUHookSetProperty,
                  (void **)&gMBSpotifyAUOriginalSetProperty);
    if (start) hook(start, (void *)MBSpotifyAUHookStart,
                    (void **)&gMBSpotifyAUOriginalStart);
    gMBSpotifyAUHookInstalled = gMBSpotifyAUOriginalSetProperty != NULL;
    NSLog(@"[BloomAudio] Spotify AudioUnit: hook=%d outputStart=%d",
          gMBSpotifyAUHookInstalled, gMBSpotifyAUOriginalStart != NULL);
}

static void MBSpotifyAUPublishStatus(void) {
    if (!MBIsSpotifyGuest(gMBAudioGuest)) return;
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    // Never force a cross-process defaults flush on every 300 ms heartbeat.
    if (now - gMBSpotifyAULastStatusAt < 1.5) return;
    gMBSpotifyAULastStatusAt = now;
    uint64_t renders = atomic_load_explicit(&gMBSpotifyAURenderCount,
                                            memory_order_relaxed);
    uint64_t pcm = atomic_load_explicit(&gMBSpotifyAUNonzeroPCMCount,
                                        memory_order_relaxed);
    uint64_t muted = atomic_load_explicit(&gMBSpotifyAUAttenuatedCount,
                                          memory_order_relaxed);
    uint64_t starts = atomic_load_explicit(&gMBSpotifyAUStartCount,
                                           memory_order_relaxed);
    unsigned callbacks = atomic_load_explicit(&gMBSpotifyAUContextCount,
                                               memory_order_relaxed);
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    [shared setBool:gMBSpotifyAUHookInstalled
             forKey:@"MediaBloomSpotifyAudioUnitHooksV1"];
    [shared setObject:[NSString stringWithFormat:
        @"AudioUnit hooks %@; salidas iniciadas %llu; callbacks registrados %u; renders %llu; PCM con señal %llu; audio atenuado %llu.",
        gMBSpotifyAUHookInstalled ? @"sí" : @"no",
        starts, callbacks, renders, pcm, muted]
            forKey:@"MediaBloomSpotifyAudioUnitStatusV1"];
    [shared synchronize];
}
