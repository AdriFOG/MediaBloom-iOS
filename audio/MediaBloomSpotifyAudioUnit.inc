// BloomAudio Inspector: independent, opt-in, metadata-only diagnostics.
// No sample buffers, microphone, playback mutation, code patching or global hooks.
#import <AudioToolbox/AudioToolbox.h>
#include <mach-o/dyld.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <mach-o/loader.h>
#include <math.h>

static NSString * const kMBInspectorEnabled = @"MediaBloomInspectorV1";
static NSString * const kMBInspectorTrace = @"MediaBloomInspectorTraceV1";
static NSString * const kMBInspectorLog = @"MediaBloomInspectorLogV1";
static NSMutableArray<NSString *> *gMBInspectorLines;
static NSString *gMBInspectorLastState;
static NSString *gMBInspectorLastImages;
static NSString *gMBInspectorTraceState = @"off";
static _Atomic(uint64_t) gMBISet, gMBICallback, gMBIStart, gMBIStop, gMBIQStart, gMBIQStop, gMBIParameter;
static _Atomic(bool) gMBITraceEnabled;
static OSStatus (*gMBIOrigSet)(AudioUnit, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, const void *, UInt32);
static OSStatus (*gMBIOrigStart)(AudioUnit);
static OSStatus (*gMBIOrigStop)(AudioUnit);
static OSStatus (*gMBIOrigQStart)(AudioQueueRef, const AudioTimeStamp *);
static OSStatus (*gMBIOrigQStop)(AudioQueueRef, Boolean);
static OSStatus (*gMBIOrigParameter)(AudioUnit, AudioUnitParameterID, AudioUnitScope, AudioUnitElement, AudioUnitParameterValue, UInt32);
static void MBICount(_Atomic(uint64_t) *counter) {
    if (atomic_load_explicit(&gMBITraceEnabled, memory_order_relaxed))
        atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
}
static OSStatus MBISet(AudioUnit u, AudioUnitPropertyID p, AudioUnitScope s, AudioUnitElement e, const void *v, UInt32 n) {
    OSStatus result = gMBIOrigSet(u,p,s,e,v,n);
    if (result == noErr) {
        MBICount(&gMBISet);
        if (p == kAudioUnitProperty_SetRenderCallback) MBICount(&gMBICallback);
    }
    return result;
}
static OSStatus MBIStart(AudioUnit u) { OSStatus r=gMBIOrigStart(u); if(r==noErr) MBICount(&gMBIStart); return r; }
static OSStatus MBIStop(AudioUnit u) { OSStatus r=gMBIOrigStop(u); if(r==noErr) MBICount(&gMBIStop); return r; }
static OSStatus MBIQStart(AudioQueueRef q,const AudioTimeStamp *t) { OSStatus r=gMBIOrigQStart(q,t); if(r==noErr) MBICount(&gMBIQStart); return r; }
static OSStatus MBIQStop(AudioQueueRef q,Boolean now) { OSStatus r=gMBIOrigQStop(q,now); if(r==noErr) MBICount(&gMBIQStop); return r; }
static OSStatus MBIParameter(AudioUnit u,AudioUnitParameterID p,AudioUnitScope s,AudioUnitElement e,AudioUnitParameterValue v,UInt32 o) {
    OSStatus r=gMBIOrigParameter(u,p,s,e,v,o); if(r==noErr) MBICount(&gMBIParameter); return r;
}
typedef void (*MBIRebind)(const struct mach_header *,void *,void *,bool (*)(const struct mach_header *));
static void MBIInstallTrace(void) {
    // Only imports in the verified guest executable. Never patch shared-cache
    // functions or install a render callback. Unavailable symbols fail closed.
    MBIRebind rebind=(MBIRebind)dlsym(RTLD_DEFAULT,"litehook_rebind_symbol");
    if (!rebind) { gMBInspectorTraceState=@"LiteHook unavailable"; return; }
    NSString *path=NSBundle.mainBundle.executablePath.stringByResolvingSymlinksInPath;
    const struct mach_header *header=NULL;
    if(path.length) for(uint32_t i=0;i<_dyld_image_count();i++) {
        const char *name=_dyld_get_image_name(i);
        if(name && [path isEqualToString:[@(name) stringByResolvingSymlinksInPath]]) {
            header=_dyld_get_image_header(i); break;
        }
    }
    if(!header || [path.lastPathComponent.lowercaseString containsString:@"livecontainer"] ||
       [path.lastPathComponent.lowercaseString containsString:@"liveprocess"]) {
        gMBInspectorTraceState=@"Guest executable not verified"; return;
    }
    void *audio=dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox",RTLD_LAZY|RTLD_LOCAL);
    if(!audio) { gMBInspectorTraceState=@"AudioToolbox unavailable"; return; }
#define MBI_BIND(name, orig, replacement) do { \
    orig=(__typeof__(orig))dlsym(audio,name); \
    if(orig) rebind(header,(void *)orig,(void *)replacement,NULL); \
} while(0)
    MBI_BIND("AudioUnitSetProperty",gMBIOrigSet,MBISet);
    MBI_BIND("AudioOutputUnitStart",gMBIOrigStart,MBIStart);
    MBI_BIND("AudioOutputUnitStop",gMBIOrigStop,MBIStop);
    MBI_BIND("AudioQueueStart",gMBIOrigQStart,MBIQStart);
    MBI_BIND("AudioQueueStop",gMBIOrigQStop,MBIQStop);
    MBI_BIND("AudioUnitSetParameter",gMBIOrigParameter,MBIParameter);
#undef MBI_BIND
    gMBInspectorTraceState=@"Import rebinding attempted; only nonzero counters confirm calls";
}
static void MBIAppend(NSString *line) {
    [gMBInspectorLines addObject:[NSString stringWithFormat:@"%@ | %@",NSDate.date,line]];
    if(gMBInspectorLines.count>160) [gMBInspectorLines removeObjectsInRange:NSMakeRange(0,gMBInspectorLines.count-160)];
}
static id MBIRead(id obj, SEL sel) {
    return [obj respondsToSelector:sel] ? ((id(*)(id,SEL))objc_msgSend)(obj,sel) : nil;
}
static double MBIReadDouble(id obj, SEL sel) {
    return [obj respondsToSelector:sel] ? ((double(*)(id,SEL))objc_msgSend)(obj,sel) : 0;
}
// Read-only adapter for the exact SpotifyAudioProbe binary examined in
// Spotify 9.2.0 (probe 91). LC_UUID, symbol owner and snapshot ABI must match.
// No observer registration, initialization, file export, or audio modification.
typedef void (*MBISAPReadSnapshot)(void *);
static MBISAPReadSnapshot gMBISAPRead;
static void *gMBISAPHandle;
static NSString *gMBISAPStatus;
static NSTimeInterval gMBISAPLastPoll;
static uint64_t gMBISAPPreviousBytes, gMBISAPPreviousNonzero, gMBISAPPreviousCallbacks;
static BOOL gMBISAPHasPrevious;
static const uint8_t kMBISAPUUID[16] = {
    0x9c,0xd1,0xca,0xf0,0xdc,0x6c,0x3b,0x7c,0xa7,0xa6,0x74,0x35,0x6e,0x56,0x47,0xaf
};
enum { MBISAPSnapshotSize = 440 };
static BOOL MBISAPVerifiedImage(const struct mach_header *header) {
    if (!header || header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_ARM64) return NO;
    const struct mach_header_64 *h = (const struct mach_header_64 *)header;
    if (h->filetype != MH_DYLIB || h->sizeofcmds > 1024*1024 ||
        h->ncmds > h->sizeofcmds / sizeof(struct load_command)) return NO;
    const uint8_t *cursor = (const uint8_t *)(h + 1);
    size_t remaining = h->sizeofcmds;
    for (uint32_t i=0; i<h->ncmds; ++i) {
        if (remaining < sizeof(struct load_command)) return NO;
        const struct load_command *cmd = (const struct load_command *)cursor;
        if (cmd->cmdsize < sizeof(*cmd) || cmd->cmdsize > remaining || cmd->cmdsize % 8) return NO;
        if (cmd->cmd == LC_UUID) {
            return cmd->cmdsize == sizeof(struct uuid_command) &&
                memcmp(((const struct uuid_command *)cmd)->uuid,kMBISAPUUID,16)==0;
        }
        cursor += cmd->cmdsize; remaining -= cmd->cmdsize;
    }
    return NO;
}
static void MBISAPResolve(void) {
    if (gMBISAPRead || gMBISAPHandle) return;
    gMBISAPStatus=@"not loaded";
    for (uint32_t i=0; i<_dyld_image_count(); ++i) {
        const char *name=_dyld_get_image_name(i);
        if (!name || ![[@(name) lastPathComponent] isEqualToString:@"SpotifyAudioProbe.dylib"]) continue;
        const struct mach_header *header=_dyld_get_image_header(i);
        if (!MBISAPVerifiedImage(header)) { gMBISAPStatus=@"unsupported probe UUID; no call"; return; }
        void *handle=dlopen(name,RTLD_LAZY|RTLD_NOLOAD|RTLD_FIRST);
        if (!handle) { gMBISAPStatus=@"loaded probe handle unavailable"; return; }
        void *symbol=dlsym(handle,"SpotifyAudioProbeReadSnapshot");
        Dl_info owner={0};
        if (!symbol || !dladdr(symbol,&owner) || owner.dli_fbase!=(const void *)header) {
            dlclose(handle); gMBISAPStatus=@"snapshot symbol owner mismatch; no call"; return;
        }
        gMBISAPHandle=handle;
        gMBISAPRead=(MBISAPReadSnapshot)symbol;
        gMBISAPStatus=@"verified probe 91 / read-only snapshot";
        return;
    }
}
static uint32_t MBISAPU32(const uint8_t *p,size_t offset) {
    uint32_t v=0; memcpy(&v,p+offset,sizeof(v)); return v;
}
static uint64_t MBISAPU64(const uint8_t *p,size_t offset) {
    uint64_t v=0; memcpy(&v,p+offset,sizeof(v)); return v;
}
static double MBISAPDouble(const uint8_t *p,size_t offset) {
    double v=0; memcpy(&v,p+offset,sizeof(v)); return v;
}
static NSString *MBISAPFourCC(uint32_t n) {
    if(!n) return @"unknown";
    char text[5]={0};
    for(int i=0;i<4;i++) {
        uint8_t ch=(uint8_t)(n>>(24-8*i)); text[i]=(ch>=32 && ch<=126)?(char)ch:'?';
    }
    return @(text);
}
static NSString *MBISAPSnapshotLine(void) {
    NSTimeInterval now=NSProcessInfo.processInfo.systemUptime;
    if(gMBISAPLastPoll>0 && now-gMBISAPLastPoll<5.0) return nil;
    gMBISAPLastPoll=now;
    MBISAPResolve();
    if(!gMBISAPRead) return [NSString stringWithFormat:@"Spotify probe: %@",gMBISAPStatus?:@"unavailable"];
    // Verified ARM64 function writes 0x1b8 bytes. Unknown builds are rejected.
    _Alignas(16) uint8_t snapshot[512]={0};
    _Static_assert(MBISAPSnapshotSize==0x1b8,"verified snapshot layout");
    gMBISAPRead(snapshot);
    uint64_t callbacks=MBISAPU64(snapshot,0x58), bytes=MBISAPU64(snapshot,0x68);
    uint64_t nonzero=MBISAPU64(snapshot,0x70);
    BOOL advancing=gMBISAPHasPrevious && bytes>gMBISAPPreviousBytes && nonzero>gMBISAPPreviousNonzero;
    BOOL reset=gMBISAPHasPrevious && (bytes<gMBISAPPreviousBytes ||
         nonzero<gMBISAPPreviousNonzero || callbacks<gMBISAPPreviousCallbacks);
    uint64_t delta=gMBISAPHasPrevious && !reset?bytes-gMBISAPPreviousBytes:0;
    NSString *activity=reset?@"counter reset":!gMBISAPHasPrevious?@"baseline":
        advancing?@"PCM and nonzero counters increased":@"no new nonzero PCM confirmed";
    gMBISAPPreviousBytes=bytes; gMBISAPPreviousNonzero=nonzero;
    gMBISAPPreviousCallbacks=callbacks; gMBISAPHasPrevious=YES;
    double rate=MBISAPDouble(snapshot,0x90), age=MBISAPDouble(snapshot,0x98);
    if (!isfinite(rate) || rate<0 || rate>768000) rate=0;
    if (!isfinite(age) || age<0) age=-1;
    // Exclude raw addresses, audio buffers, track export state, account data and full paths.
    NSString *image=[[NSString alloc] initWithBytes:snapshot+0xb8
        length:strnlen((const char *)snapshot+0xb8,256) encoding:NSUTF8StringEncoding];
    image=image.lastPathComponent?:@"unknown";
    BOOL diagnosticsPreference=[NSUserDefaults.standardUserDefaults boolForKey:@"SAPDiagnosticsEnabled"];
    return [NSString stringWithFormat:
        @"Spotify probe 91: backend_ready=%d; diagnostics_preference=%d; pcm_confirmed=%d; retired=%d; au_set_calls=%u; au_callback_attempts=%u; last_scope=%u; last_element=%u; type=%@; subtype=%@; last_status=%d; objc_hooks=%u; sample_buffer_calls=%llu; player_buffer_calls=%llu; compressed_buffers=%llu; extraction_errors=%llu; registrations=%u; output_starts=%u; callbacks=%llu; frames=%llu; pcm_bytes=%llu; delta_bytes=%llu; nonzero_callbacks=%llu; callback_errors=%llu; sample_rate=%.0f; channels=%u; format=%@; flags=0x%x; bits=%u; last_callback_age=%.2fs; source_image=%@; activity=%@",
        snapshot[0]!=0,diagnosticsPreference,snapshot[2]!=0,snapshot[3]!=0,
        MBISAPU32(snapshot,0x4),MBISAPU32(snapshot,0x8),MBISAPU32(snapshot,0xc),MBISAPU32(snapshot,0x14),
        MBISAPFourCC(MBISAPU32(snapshot,0x10)),MBISAPFourCC(MBISAPU32(snapshot,0x18)),(int32_t)MBISAPU32(snapshot,0x1c),
        MBISAPU32(snapshot,0x20),(unsigned long long)MBISAPU64(snapshot,0x28),(unsigned long long)MBISAPU64(snapshot,0x30),
        (unsigned long long)MBISAPU64(snapshot,0x38),(unsigned long long)MBISAPU64(snapshot,0x40),
        MBISAPU32(snapshot,0x4c),MBISAPU32(snapshot,0x50),(unsigned long long)callbacks,
        (unsigned long long)MBISAPU64(snapshot,0x60),(unsigned long long)bytes,(unsigned long long)delta,
        (unsigned long long)nonzero,(unsigned long long)MBISAPU64(snapshot,0x78),
        rate,MBISAPU32(snapshot,0xa0),MBISAPFourCC(MBISAPU32(snapshot,0xa4)),
        MBISAPU32(snapshot,0xa8),MBISAPU32(snapshot,0xac),age,image,activity];
}

static void MBICollect(void) {
    NSUserDefaults *shared=NSUserDefaults.lcSharedDefaults;
    if(![shared boolForKey:kMBInspectorEnabled]) return;
    atomic_store_explicit(&gMBITraceEnabled,[shared boolForKey:kMBInspectorTrace],memory_order_relaxed);
    Class sessionClass=NSClassFromString(@"AVAudioSession");
    id session=MBIRead(sessionClass,@selector(sharedInstance));
    id route=MBIRead(session,@selector(currentRoute));
    NSMutableArray *outputs=[NSMutableArray array];
    for(id port in MBIRead(route,@selector(outputs))) {
        NSString *type=MBIRead(port,@selector(portType)); if(type) [outputs addObject:type];
    }
    id center=MBIRead(NSClassFromString(@"MPNowPlayingInfoCenter"),@selector(defaultCenter));
    id candidate=MBIRead(center,@selector(nowPlayingInfo));
    NSDictionary *info=[candidate isKindOfClass:NSDictionary.class]?candidate:@{};
    NSString * const *rateKey=(NSString * const *)dlsym(RTLD_DEFAULT,"MPNowPlayingInfoPropertyPlaybackRate");
    NSString * const *titleKey=(NSString * const *)dlsym(RTLD_DEFAULT,"MPMediaItemPropertyTitle");
    id rate=rateKey && *rateKey ? info[*rateKey] : nil;
    id title=titleKey && *titleKey ? info[*titleKey] : nil;
    if(![title isKindOfClass:NSString.class]) title=@"(unknown)";
    NSString *state=[NSString stringWithFormat:@"Now Playing=%@; rate=%@; route=%@; category=%@; sampleRate=%.0f; buffer=%.4fs; trace=%@; AUSet=%llu; callback registrations=%llu; AUStart=%llu; AUStop=%llu; QueueStart=%llu; QueueStop=%llu; AUSetParameter=%llu",
        title,rate?:@"unknown",[outputs componentsJoinedByString:@","],MBIRead(session,@selector(category))?:@"unknown",
        MBIReadDouble(session,@selector(sampleRate)),MBIReadDouble(session,@selector(IOBufferDuration)),gMBInspectorTraceState,
        (unsigned long long)atomic_load_explicit(&gMBISet,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&gMBICallback,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&gMBIStart,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&gMBIStop,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&gMBIQStart,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&gMBIQStop,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&gMBIParameter,memory_order_relaxed)];
    NSMutableOrderedSet *images=[NSMutableOrderedSet orderedSet];
    for(uint32_t i=0;i<_dyld_image_count();i++) {
        const char *name=_dyld_get_image_name(i); if(!name) continue;
        NSString *base=[@(name) lastPathComponent], *lower=base.lowercaseString;
        if([lower containsString:@"audio"]||[lower containsString:@"spotify"]||[lower containsString:@"media"]||
           [lower containsString:@"sound"]||[lower containsString:@"player"]||[lower containsString:@"codec"])
            [images addObject:base];
        if(images.count>=80) break;
    }
    NSString *imageList=[[images array] componentsJoinedByString:@", "];
    BOOL changed=NO;
    if(![state isEqualToString:gMBInspectorLastState]) { MBIAppend(state); gMBInspectorLastState=[state copy]; changed=YES; }
    if(![imageList isEqualToString:gMBInspectorLastImages]) { MBIAppend([@"Loaded image names (presence only): " stringByAppendingString:imageList]); gMBInspectorLastImages=[imageList copy]; changed=YES; }
    if (MBIsSpotifyGuest(NSUserDefaults.lcGuestAppId)) {
        NSString *probeLine=MBISAPSnapshotLine();
        if (probeLine.length) { MBIAppend(probeLine); changed=YES; }
    }
    if(changed) { [shared setObject:[gMBInspectorLines componentsJoinedByString:@"\n"] forKey:kMBInspectorLog]; [shared synchronize]; }
}
static void MBInstallAudioInspector(void) {
    if(!MBIsMediaHost() || !MBIsSupportedGuest(NSUserDefaults.lcGuestAppId)) return;
    NSUserDefaults *shared=NSUserDefaults.lcSharedDefaults;
    [shared synchronize];
    if(![shared boolForKey:kMBInspectorEnabled]) return;
    // Trace is optional and applied at launch, not toggled in a running guest.
    if (MBIsSpotifyGuest(NSUserDefaults.lcGuestAppId)) {
        // Spotify uses its own AudioToolbox interposer. Do not rebind imports
        // even if an old trace preference is still enabled.
        gMBInspectorTraceState=@"Spotify: read-only probe; no rebinding";
    } else if([shared boolForKey:kMBInspectorTrace]) {
        atomic_store_explicit(&gMBITraceEnabled,true,memory_order_relaxed);
        MBIInstallTrace();
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        gMBInspectorLines=[NSMutableArray array];
        MBIAppend([NSString stringWithFormat:@"Inspector session: %@; trace=%@; no PCM captured",NSUserDefaults.lcGuestAppId,gMBInspectorTraceState]);
        [shared setObject:[gMBInspectorLines componentsJoinedByString:@"\n"] forKey:kMBInspectorLog];
        [shared synchronize];
        MBICollect();
        [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer *timer) {
            [shared synchronize];
            if(![shared boolForKey:kMBInspectorEnabled]) {
                atomic_store_explicit(&gMBITraceEnabled,false,memory_order_relaxed);
                [timer invalidate]; return;
            }
            MBICollect();
        }];
    });
}
