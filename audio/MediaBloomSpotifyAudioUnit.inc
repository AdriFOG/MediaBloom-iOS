// BloomAudio Inspector: independent, opt-in, metadata-only diagnostics.
// No sample buffers, microphone, playback mutation, code patching or global hooks.
#import <AudioToolbox/AudioToolbox.h>
#include <mach-o/dyld.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

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
    if(changed) { [shared setObject:[gMBInspectorLines componentsJoinedByString:@"\n"] forKey:kMBInspectorLog]; [shared synchronize]; }
}
static void MBInstallAudioInspector(void) {
    if(!MBIsMediaHost() || !MBIsSupportedGuest(NSUserDefaults.lcGuestAppId)) return;
    NSUserDefaults *shared=NSUserDefaults.lcSharedDefaults;
    [shared synchronize];
    if(![shared boolForKey:kMBInspectorEnabled]) return;
    // Trace is optional and applied at launch, not toggled in a running guest.
    if([shared boolForKey:kMBInspectorTrace]) {
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
