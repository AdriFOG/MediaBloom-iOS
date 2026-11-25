// MediaBloom only: native in-guest Edge menu. No changes to primary BloomBox.
#import <UIKit/UIKit.h>
#import <UIKit/UIGestureRecognizerSubclass.h>
#import <QuartzCore/QuartzCore.h>

static BOOL MBEdgeEnabled(void) {
    id choice = [NSUserDefaults.lcSharedDefaults objectForKey:@"MediaBloomEdgeEnabledV1"];
    return choice == nil || [choice boolValue];
}
static NSArray<NSDictionary *> *MBEdgeApps(void) {
    Class cls = NSClassFromString(@"LCSharedUtils");
    SEL sel = NSSelectorFromString(@"appGroupPath");
    if (![cls respondsToSelector:sel]) return @[];
    NSURL *group = ((id(*)(id,SEL))objc_msgSend)(cls,sel);
    NSURL *folder = [group URLByAppendingPathComponent:@"LiveContainer/Applications" isDirectory:YES];
    if (!folder) return @[];
    NSArray *entries = [NSFileManager.defaultManager contentsOfDirectoryAtURL:folder
                     includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles error:nil];
    NSArray *ids = @[@"com.google.ios.youtubemusic",@"com.spotify.client",@"org.videolan.vlc-ios",@"com.apple.music",
      @"com.google.ios.youtube",@"com.amazon.mp3",@"com.soundcloud.touchapp",@"com.deezer.deezer",@"com.tidal",
      @"com.netflix.netflix",@"com.amazon.aiv",@"com.twitch.twitch",@"tv.twitch",@"com.plexapp.plex",
      @"com.firecore.infuse",@"com.disney.disneyplus"];
    NSMutableArray *result = [NSMutableArray array];
    for (NSURL *entry in entries) {
        if (![entry.pathExtension.lowercaseString isEqualToString:@"app"]) continue;
        NSDictionary *info = [NSDictionary dictionaryWithContentsOfURL:[entry URLByAppendingPathComponent:@"Info.plist"]];
        NSDictionary *lc = [NSDictionary dictionaryWithContentsOfURL:[entry URLByAppendingPathComponent:@"LCAppInfo.plist"]];
        if ([lc[@"isHidden"] boolValue] || [lc[@"isLocked"] boolValue] || [lc[@"LCHidden"] boolValue] || [lc[@"LCLocked"] boolValue]) continue;
        NSString *bid = [(info[@"LCBundleIdentifier"] ?: info[@"CFBundleIdentifier"]) lowercaseString];
        NSString *name = info[@"CFBundleDisplayName"] ?: info[@"CFBundleName"] ?: entry.lastPathComponent;
        if (!bid.length || !name.length) continue;
        NSString *tag = [NSUserDefaults.lcSharedDefaults stringForKey:[@"BloomBoxMediaTagV1." stringByAppendingString:bid]];
        if ([tag isEqualToString:@"none"]) continue;
        if (![tag isEqualToString:@"music"] && ![tag isEqualToString:@"video"] && ![ids containsObject:bid]) continue;
        [result addObject:@{@"id":bid,@"name":name,@"path":entry.lastPathComponent,
                            @"folder":lc[@"LCDataUUID"] ?: @"", @"bundle":entry.path}];
    }
    [result sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
        NSUInteger ai = [ids indexOfObject:a[@"id"]], bi = [ids indexOfObject:b[@"id"]];
        if (ai < bi) return NSOrderedAscending;
        if (ai > bi) return NSOrderedDescending;
        return [a[@"name"] localizedStandardCompare:b[@"name"]];
    }];
    return result;
}

// Use the same cached icon images as the MediaBloom launcher. For a newly
// installed app with no cache, read the actual icon declared in its plist.
static UIImage *MBEdgeAppIcon(NSString *path, BOOL dark) {
    NSArray *caches = dark ? @[@"LCAppIconDark.png", @"LCAppIconLight.png"] : @[@"LCAppIconLight.png", @"LCAppIconDark.png"];
    for (NSString *file in caches) {
        UIImage *image = [UIImage imageWithContentsOfFile:[path stringByAppendingPathComponent:file]];
        if (image) return image;
    }
    NSBundle *bundle = [NSBundle bundleWithPath:path];
    NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:[path stringByAppendingPathComponent:@"Info.plist"]];
    NSDictionary *primary = info[@"CFBundleIcons"][@"CFBundlePrimaryIcon"];
    NSArray *files = primary[@"CFBundleIconFiles"] ?: info[@"CFBundleIconFiles"] ?: @[];
    NSMutableArray *names = [NSMutableArray arrayWithArray:files];
    if ([primary[@"CFBundleIconName"] isKindOfClass:NSString.class]) [names addObject:primary[@"CFBundleIconName"]];
    if ([info[@"CFBundleIconFile"] isKindOfClass:NSString.class]) [names addObject:info[@"CFBundleIconFile"]];
    for (NSString *name in [names reverseObjectEnumerator]) {
        NSString *base = [name.stringByDeletingPathExtension stringByDeletingPathExtension];
        for (NSString *suffix in @[@"@3x.png", @"@2x.png", @".png", @""]) {
            UIImage *image = [UIImage imageWithContentsOfFile:[path stringByAppendingPathComponent:[base stringByAppendingString:suffix]]];
            if (image) return image;
        }
        UIImage *image = bundle ? [UIImage imageNamed:base inBundle:bundle compatibleWithTraitCollection:nil] : nil;
        if (image) return image;
    }
    return nil;
}
// Ordinary UIKit buttons: no custom pointInside, presentation-layer conversion,
// or full-screen UIControl can interfere with a standard touch-up action.
// A subview of the actual guest window, not a second transparent UIWindow.
// UIKit resolves touches normally within the guest's own responder hierarchy.
@interface MBEdgeTouchView : UIView
@property(nonatomic,copy) void (^boundsDidChange)(void);
@property(nonatomic,assign) CGRect lastLayoutBounds;
@property(nonatomic,weak) UIButton *edgeHandle;
@property(nonatomic,assign) BOOL handleExpanded;
@end
@implementation MBEdgeTouchView
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    if (self.hidden || !self.userInteractionEnabled || self.alpha < .01 ||
        ![self pointInside:point withEvent:event]) return nil;
    // Route touches to the closed button while the rest remains pass-through.
    if (!self.handleExpanded && self.edgeHandle && self.edgeHandle.enabled &&
        self.edgeHandle.userInteractionEnabled) {
        CGPoint local = [self convertPoint:point toView:self.edgeHandle];
        if ([self.edgeHandle pointInside:local withEvent:event]) return self.edgeHandle;
    }
    UIView *hit = [super hitTest:point withEvent:event];
    return hit == self ? nil : hit;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    if (!CGRectEqualToRect(self.lastLayoutBounds,self.bounds)) {
        self.lastLayoutBounds = self.bounds;
        if (self.boundsDidChange) self.boundsDidChange();
    }
}
@end

static BOOL MBEdgeLocalGainAvailable(void) {
    return gMBAudioGuest.length && gMBAudioObjects != nil &&
           [gMBAudioGuest isEqualToString:NSUserDefaults.lcGuestAppId.lowercaseString] &&
           [NSUserDefaults.lcSharedDefaults boolForKey:@"MediaBloomAudioProbeEnabledV1"] &&
           (gMBAudioAVPlayerSetVolume || gMBAudioAVAudioPlayerSetVolume || gMBAudioAVRendererSetVolume) &&
           (!MBIsSpotifyGuest(gMBAudioGuest) || MBSpotifyAudioAvailable());
}

// Only observe outside taps while the panel is open. The opening button
// never competes with a window-wide gesture recognizer.
@interface MBEdgeController : UIViewController <UIGestureRecognizerDelegate>
@property(nonatomic,strong) CAShapeLayer *rail;
@property(nonatomic,strong) UIButton *tab;
@property(nonatomic,strong) UIView *tabMark;
@property(nonatomic,strong) UIButton *sync;
@property(nonatomic,strong) UIButton *speaker;
@property(nonatomic,strong) UIView *volumeCapsule;
@property(nonatomic,strong) UISlider *slider;
@property(nonatomic,strong) UILabel *volumeUnavailable;
@property(nonatomic,strong) NSMutableArray<UIButton *> *shortcuts;
@property(nonatomic,strong) NSArray<NSDictionary *> *apps;
@property(nonatomic,copy) NSArray<NSString *> *selectionSnapshot;
@property(nonatomic,assign) BOOL expanded;
@property(nonatomic,assign) BOOL volumeExpanded;
@property(nonatomic,assign) BOOL right;
@property(nonatomic,assign) CGFloat anchor;
@property(nonatomic,strong) NSTimer *idleTimer;
@property(nonatomic,strong) UITapGestureRecognizer *outsideTap;
@property(nonatomic,weak) UIWindow *activityWindow;
@property(nonatomic,assign) BOOL controlIsTouching;
@property(nonatomic,assign) BOOL lastDrawnExpanded;
- (void)layoutEdge:(BOOL)animated;
- (void)closeEdge;
- (void)restartIdleTimer;
- (void)attachActivityObserverToWindow:(UIWindow *)window;
@end

@implementation MBEdgeController
- (UIButton *)button:(NSString *)symbol color:(UIColor *)color label:(NSString *)label action:(SEL)action {
    UIButton *b = [[UIButton alloc] initWithFrame:CGRectZero];
    b.tintColor = UIColor.whiteColor;
    b.backgroundColor = color;
    b.layer.cornerRadius = 22.5;
    [b setImage:[UIImage systemImageNamed:symbol] forState:UIControlStateNormal];
    b.accessibilityLabel = label;
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    [b addTarget:self action:@selector(controlTouchStarted:) forControlEvents:UIControlEventTouchDown];
    [b addTarget:self action:@selector(controlTouchFinished:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
    return b;
}
- (void)loadView {
    self.view = [[MBEdgeTouchView alloc] initWithFrame:CGRectZero];
    self.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view.backgroundColor = UIColor.clearColor;
    self.anchor = .54;
    self.rail = [CAShapeLayer layer];
    self.rail.fillColor = UIColor.blackColor.CGColor;
    [self.view.layer addSublayer:self.rail];
    self.tab = [self button:@"" color:UIColor.clearColor label:@"MediaBloom Edge" action:@selector(toggle:)];
    self.tab.layer.cornerRadius = 0;
    [self.tab setImage:nil forState:UIControlStateNormal];
    // Guest edge recognizers can cancel a release; open directly on touch down.
    [self.tab removeTarget:self action:@selector(toggle:) forControlEvents:UIControlEventTouchUpInside];
    [self.tab addTarget:self action:@selector(toggle:) forControlEvents:UIControlEventTouchDown];
    ((MBEdgeTouchView *)self.view).edgeHandle = self.tab;
    self.tabMark = [[UIView alloc] initWithFrame:CGRectZero];
    self.tabMark.backgroundColor = [UIColor colorWithRed:.60 green:.43 blue:.93 alpha:1];
    self.tabMark.layer.cornerRadius = 1.5;
    self.tabMark.layer.shadowColor = UIColor.whiteColor.CGColor;
    self.tabMark.layer.shadowOpacity = .65;
    self.tabMark.layer.shadowRadius = 2;
    self.tabMark.layer.shadowOffset = CGSizeZero;
    self.tabMark.userInteractionEnabled = NO;
    [self.view addSubview:self.tab];
    [self.tab addSubview:self.tabMark];
    self.sync = [self button:@"arrow.triangle.2.circlepath" color:[UIColor colorWithRed:.60 green:.43 blue:.93 alpha:1] label:@"Resincronizar Music Haptics" action:@selector(resync:)];
    [self.view addSubview:self.sync];
    self.volumeCapsule = [[UIView alloc] initWithFrame:CGRectZero];
    self.volumeCapsule.backgroundColor = [UIColor colorWithWhite:.18 alpha:1];
    self.volumeCapsule.layer.cornerRadius = 22.5;
    self.volumeCapsule.clipsToBounds = YES;
    [self.view addSubview:self.volumeCapsule];
    self.speaker = [self button:@"speaker.wave.2.fill" color:UIColor.clearColor label:@"Volumen" action:@selector(toggleVolume:)];
    [self.volumeCapsule addSubview:self.speaker];
    self.slider = [[UISlider alloc] initWithFrame:CGRectZero];
    self.slider.minimumValue = 0;
    self.slider.maximumValue = 1;
    self.slider.continuous = YES;
    self.slider.minimumTrackTintColor = UIColor.systemPurpleColor;
    self.slider.maximumTrackTintColor = [UIColor colorWithWhite:1 alpha:.24];
    self.slider.accessibilityLabel = @"Volumen individual de MediaBloom";
    [self.slider addTarget:self action:@selector(changeGuestGain:) forControlEvents:UIControlEventValueChanged];
    [self.slider addTarget:self action:@selector(controlTouchStarted:) forControlEvents:UIControlEventTouchDown];
    [self.slider addTarget:self action:@selector(controlTouchFinished:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
    [self.volumeCapsule insertSubview:self.slider belowSubview:self.speaker];
    self.volumeUnavailable = [[UILabel alloc] initWithFrame:CGRectZero];
    self.volumeUnavailable.font = [UIFont systemFontOfSize:11 weight:UIFontWeightMedium];
    self.volumeUnavailable.textColor = UIColor.whiteColor;
    self.volumeUnavailable.textAlignment = NSTextAlignmentCenter;
    self.volumeUnavailable.adjustsFontSizeToFitWidth = YES;
    self.volumeUnavailable.userInteractionEnabled = NO;
    [self.volumeCapsule insertSubview:self.volumeUnavailable belowSubview:self.speaker];
    self.shortcuts = [NSMutableArray array];
    self.apps = @[];
    __weak MBEdgeController *weakSelf = self;
    ((MBEdgeTouchView *)self.view).boundsDidChange = ^{ [weakSelf layoutEdge:NO]; };
    // No competing long-press or drag recognizer on the opening button.
}
- (void)cancelIdleTimer {
    [self.idleTimer invalidate];
    self.idleTimer = nil;
}
- (void)restartIdleTimer {
    [self cancelIdleTimer];
    if (!self.expanded || self.slider.tracking) return;
    __weak MBEdgeController *weakSelf = self;
    NSTimer *timer = [NSTimer timerWithTimeInterval:3.0 repeats:NO block:^(NSTimer *fired) {
        MBEdgeController *strongSelf = weakSelf;
        if (!strongSelf || strongSelf.idleTimer != fired) return;
        strongSelf.idleTimer = nil;
        if (strongSelf.slider.tracking) return; // touch-up restarts the timer.
        [strongSelf closeEdge];
    }];
    self.idleTimer = timer;
    [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
}
- (void)closeEdge {
    [self cancelIdleTimer];
    if (!self.expanded) return;
    self.expanded = NO;
    self.volumeExpanded = NO;
    self.outsideTap.enabled = NO;
    [self animate];
}
- (void)attachActivityObserverToWindow:(UIWindow *)window {
    if (self.activityWindow == window && self.outsideTap) return;
    if (self.outsideTap) [self.activityWindow removeGestureRecognizer:self.outsideTap];
    self.outsideTap = nil;
    self.activityWindow = window;
    [self cancelIdleTimer];
    if (!window) { [self closeEdge]; return; }
    UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(outsideTapped:)];
    tap.cancelsTouchesInView = NO;
    tap.delaysTouchesBegan = NO;
    tap.delaysTouchesEnded = NO;
    tap.delegate = self;
    tap.enabled = self.expanded;
    [window addGestureRecognizer:tap];
    self.outsideTap = tap;
    [self restartIdleTimer];
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)recognizer shouldReceiveTouch:(UITouch *)touch {
    if (recognizer != self.outsideTap || !self.expanded) return NO;
    UIView *target = touch.view;
    return target && target != self.view && ![target isDescendantOfView:self.view];
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)recognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)other { return YES; }
- (void)outsideTapped:(UITapGestureRecognizer *)tap {
    if (tap.state == UIGestureRecognizerStateRecognized) [self closeEdge];
}
- (void)controlTouchStarted:(UIControl *)control {
    self.controlIsTouching = YES;
    [self cancelIdleTimer];
}
- (void)controlTouchFinished:(UIControl *)control {
    self.controlIsTouching = NO;
    dispatch_async(dispatch_get_main_queue(), ^{ [self restartIdleTimer]; });
}
- (void)refreshGuestGain {
    NSString *guest = NSUserDefaults.lcGuestAppId.lowercaseString;
    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:guest ?: @""];
    id saved = [NSUserDefaults.lcSharedDefaults objectForKey:key];
    self.slider.value = [saved respondsToSelector:@selector(floatValue)] ? MBClampVolume([saved floatValue]) : 1;
    BOOL available = MBEdgeLocalGainAvailable();
    self.slider.enabled = available;
    self.volumeUnavailable.text = MBIsSpotifyGuest(guest)
        ? (gMBAudioObjects ? @"Spotify: sin ruta AV" : @"Activa BloomAudio")
        : (MBIsSupportedGuest(guest) ? @"Activa BloomAudio" : @"No compatible");
    [self updateSpeakerIcon];
}
- (void)updateSpeakerIcon {
    float v=self.slider.value;
    NSString *symbol=v<.001?@"speaker.slash.fill":v<.34?@"speaker.wave.1.fill":v<.67?@"speaker.wave.2.fill":@"speaker.wave.3.fill";
    [self.speaker setImage:[UIImage systemImageNamed:symbol] forState:UIControlStateNormal];
    self.slider.accessibilityValue = [NSString stringWithFormat:@"%d%%",(int)lroundf(v*100)];
}
- (void)changeGuestGain:(UISlider *)slider {
    if (!MBEdgeLocalGainAvailable()) return;
    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:gMBAudioGuest];
    [NSUserDefaults.lcSharedDefaults setFloat:MBClampVolume(slider.value) forKey:key];
    // Never write the master gain or iOS output volume from Edge.
    MBApplyLiveGain();
    [self updateSpeakerIcon];
    [self restartIdleTimer];
}
- (void)refreshApps {
    NSArray *all = MBEdgeApps();
    id saved = [NSUserDefaults.lcSharedDefaults objectForKey:@"MediaBloomEdgeAppIDsV1"];
    NSArray *selection = [saved isKindOfClass:NSArray.class] ? saved : nil;
    self.selectionSnapshot = selection;
    NSArray *next;
    if (selection) {
        // Empty selection means no app shortcuts, not auto-fill.
        NSMutableArray<NSDictionary *> *ordered = [NSMutableArray arrayWithCapacity:4];
        for (id identifier in selection) {
            if (![identifier isKindOfClass:NSString.class]) continue;
            for (NSDictionary *app in all) {
                if ([app[@"id"] isEqualToString:identifier] &&
                    ![ordered containsObject:app]) {
                    [ordered addObject:app];
                    break;
                }
            }
            if (ordered.count == 4) break;
        }
        next = ordered;
    } else {
        // Keep the first-four behavior until the user customizes it.
        next = [all subarrayWithRange:NSMakeRange(0,MIN((NSUInteger)4,all.count))];
    }
    if ([next isEqualToArray:self.apps]) return;
    for (UIButton *b in self.shortcuts) [b removeFromSuperview];
    [self.shortcuts removeAllObjects];
    self.apps = next;
    BOOL dark = self.traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark;
    for (NSUInteger i=0;i<next.count;i++) {
        NSDictionary *app = next[i];
        UIButton *b = [self button:@"" color:UIColor.clearColor label:app[@"name"] action:@selector(openApp:)];
        UIImage *icon = MBEdgeAppIcon(app[@"bundle"], dark);
        if (icon) {
            [b setImage:[icon imageWithRenderingMode:UIImageRenderingModeAlwaysOriginal] forState:UIControlStateNormal];
            b.imageView.contentMode = UIViewContentModeScaleAspectFit;
            b.clipsToBounds = YES;
        } else {
            // Never substitute a made-up service logo. Show the actual app name.
            b.backgroundColor = [UIColor colorWithWhite:.20 alpha:1];
            [b setTitle:app[@"name"] forState:UIControlStateNormal];
            b.titleLabel.font = [UIFont systemFontOfSize:9 weight:UIFontWeightSemibold];
            b.titleLabel.numberOfLines = 2;
            b.titleLabel.textAlignment = NSTextAlignmentCenter;
            b.titleLabel.adjustsFontSizeToFitWidth = YES;
            [b setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
        }
        b.tag = i;
        [self.view addSubview:b];
        [self.shortcuts addObject:b];
    }
}
- (UIBezierPath *)pathWidth:(CGFloat)w height:(CGFloat)h center:(CGFloat)cy {
    CGFloat edge = self.right ? CGRectGetMaxX(self.rail.bounds) : CGRectGetMinX(self.rail.bounds);
    CGFloat sign = self.right ? -1 : 1, top = cy-h/2;
    CGPoint (^pt)(CGFloat,CGFloat) = ^CGPoint(CGFloat x,CGFloat y){return CGPointMake(edge+sign*x*w,top+y*h);};
    UIBezierPath *p = [UIBezierPath bezierPath];
    [p moveToPoint:pt(0,0)];
    [p addCurveToPoint:pt(.97,.16) controlPoint1:pt(0,.075) controlPoint2:pt(.97,.09)];
    [p addCurveToPoint:pt(1,.5) controlPoint1:pt(1,.24) controlPoint2:pt(1,.39)];
    [p addCurveToPoint:pt(.97,.84) controlPoint1:pt(1,.61) controlPoint2:pt(1,.76)];
    [p addCurveToPoint:pt(0,1) controlPoint1:pt(.97,.91) controlPoint2:pt(0,.925)];
    [p closePath]; return p;
}
- (void)layoutEdge:(BOOL)animated {
    CGRect bounds = self.view.bounds;
    CGFloat w=bounds.size.width,h=bounds.size.height;
    if (w<1 || h<1) return;
    NSUserDefaults *d = NSUserDefaults.lcSharedDefaults;
    id side=[d objectForKey:@"MediaBloomEdgeRightV1"];
    self.right=side==nil || [side boolValue];
    id opacity=[d objectForKey:@"MediaBloomEdgeOpacityV1"];
    CGFloat alpha=opacity ? MAX(0,MIN(1,[opacity doubleValue])) : 1;
    self.rail.fillColor=[UIColor colorWithWhite:0 alpha:alpha].CGColor;
    CGFloat fullHeight=MIN(426,h-16);
    CGFloat scale=MIN(1,fullHeight/426), cy=MAX(fullHeight/2+8,MIN(h-fullHeight/2-8,h*self.anchor));
    CGFloat height=self.expanded?fullHeight:80, width=self.expanded?79:12;
    // Draw in root-view coordinates. The native button is laid out from the
    // exact same path's bounding box, with no layer-space hit testing.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.rail.frame = bounds;
    [CATransaction commit];
    UIBezierPath *p=[self pathWidth:width height:height center:cy];
    // Elastic contour morph: native GPU path keyframes, with a restrained
    // overshoot / recoil / settle. Shapes share control points, so they
    // interpolate smoothly without rasterized snapshots or repeated timers.
    BOOL stateChanged = self.expanded != self.lastDrawnExpanded;
    if (animated && stateChanged && self.rail.path &&
        !UIAccessibilityIsReduceMotionEnabled()) {
        CAShapeLayer *presentation=(CAShapeLayer *)self.rail.presentationLayer;
        CGPathRef current = presentation.path ?: self.rail.path;
        CAKeyframeAnimation *m=[CAKeyframeAnimation animationWithKeyPath:@"path"];
        if (self.expanded) {
            CGFloat overshootHeight=MIN(h-12,height*1.045);
            m.values=@[
                (__bridge id)current,
                (__bridge id)[self pathWidth:width*.72 height:height*.72 center:cy].CGPath,
                (__bridge id)[self pathWidth:width*1.16 height:overshootHeight center:cy].CGPath,
                (__bridge id)[self pathWidth:width*.94 height:height*.975 center:cy].CGPath,
                (__bridge id)[self pathWidth:width*1.025 height:height center:cy].CGPath,
                (__bridge id)p.CGPath
            ];
            m.duration=.68;
        } else {
            m.values=@[
                (__bridge id)current,
                (__bridge id)[self pathWidth:35 height:fullHeight*.58 center:cy].CGPath,
                (__bridge id)[self pathWidth:7 height:69 center:cy].CGPath,
                (__bridge id)[self pathWidth:16 height:86 center:cy].CGPath,
                (__bridge id)[self pathWidth:11 height:78 center:cy].CGPath,
                (__bridge id)p.CGPath
            ];
            m.duration=.54;
        }
        m.keyTimes=@[@0,@.34,@.59,@.76,@.90,@1];
        m.calculationMode=kCAAnimationCubic;
        m.timingFunctions=@[
            [CAMediaTimingFunction functionWithControlPoints:.20 :.65 :.30 :1],
            [CAMediaTimingFunction functionWithControlPoints:.20 :0 :.32 :1],
            [CAMediaTimingFunction functionWithControlPoints:.25 :0 :.35 :1],
            [CAMediaTimingFunction functionWithControlPoints:.25 :0 :.35 :1],
            [CAMediaTimingFunction functionWithControlPoints:.25 :0 :.35 :1]
        ];
        [self.rail addAnimation:m forKey:@"edgeMorph"];
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.rail.path=p.CGPath;
    [CATransaction commit];
    self.lastDrawnExpanded = self.expanded;
    CGFloat x=self.right?w-38:38, size=45*scale;
    CGFloat row=55*scale, first=cy-145*scale;
    CGRect contour = CGPathGetPathBoundingBox(p.CGPath);
    // Larger tap target extends inward; the visible black rail stays slim.
    CGRect target = self.expanded ? contour :
        CGRectMake(self.right ? w - MIN(60,w) : 0, cy - 55, MIN(60,w), 110);
    target = CGRectIntersection(bounds, target);
    self.tab.frame = target;
    self.tabMark.frame = CGRectMake((self.right ? w-7 : 4)-CGRectGetMinX(target),
                                    cy-16-CGRectGetMinY(target), 3, 32);
    self.tabMark.hidden = self.expanded;
    ((MBEdgeTouchView *)self.view).handleExpanded = self.expanded;
    self.tab.accessibilityLabel = self.expanded ? @"Cerrar menú lateral" : @"Abrir menú lateral";
    self.sync.frame=CGRectMake(x-size/2,first-size/2,size,size);
    CGFloat volY=first+row;
    CGFloat capWidth=self.volumeExpanded?MIN(215,w-32):size;
    self.volumeCapsule.frame=CGRectMake(self.right?x+size/2-capWidth:x-size/2,volY-size/2,capWidth,size);
    self.volumeCapsule.layer.cornerRadius=size/2;
    self.speaker.frame=CGRectMake(self.right?capWidth-size:0,0,size,size);
    self.slider.frame=CGRectMake(self.right?12:size+4,0,MAX(0,capWidth-size-16),size);
    self.slider.alpha=self.volumeExpanded && self.slider.enabled?1:0;
    self.slider.userInteractionEnabled=self.volumeExpanded && self.slider.enabled;
    self.volumeUnavailable.frame=self.slider.frame;
    self.volumeUnavailable.alpha=self.volumeExpanded && !self.slider.enabled?1:0;
    self.sync.alpha=self.expanded?1:0; self.sync.userInteractionEnabled=self.expanded;
    self.volumeCapsule.alpha=self.expanded?1:0; self.volumeCapsule.userInteractionEnabled=self.expanded;
    for (NSUInteger i=0;i<self.shortcuts.count;i++) {
        UIButton *b=self.shortcuts[i];
        b.frame=CGRectMake(x-size/2,volY+row*(i+1)+15*scale-size/2,size,size);
        b.layer.cornerRadius=size*.23; b.alpha=self.expanded?1:0; b.userInteractionEnabled=self.expanded;
    }
    self.sync.layer.cornerRadius=size/2;
    [self.view sendSubviewToBack:self.tab];
    // The rail must stay BEHIND the button layer, or it hides the purple mark.
    [self.view.layer insertSublayer:self.rail below:self.tab.layer];

}
- (void)viewDidLayoutSubviews { [super viewDidLayoutSubviews]; [self layoutEdge:NO]; }
// The menu contents follow the expanding contour in a short cascade. Each
// icon has its own bounce, so the menu doesn't look like one sliding rectangle.
// Core Animation keeps the work off the main thread after setup.
- (void)animateEdgeControl:(UIView *)control index:(NSUInteger)index opening:(BOOL)opening {
    if (!control) return;
    [control.layer removeAnimationForKey:@"edgeContent"];
    CGFloat direction = self.right ? 1 : -1;
    CATransform3D tucked=CATransform3DConcat(
        CATransform3DMakeTranslation(direction*17,0,0),
        CATransform3DMakeScale(.72,.72,1));
    CATransform3D bounce=CATransform3DMakeScale(1.075,1.075,1);
    CATransform3D settle=CATransform3DMakeScale(.985,.985,1);
    CATransform3D neutral=CATransform3DIdentity;
    CAKeyframeAnimation *shape=[CAKeyframeAnimation animationWithKeyPath:@"transform"];
    shape.values=opening ? @[
        [NSValue valueWithCATransform3D:tucked],
        [NSValue valueWithCATransform3D:CATransform3DMakeScale(.94,.94,1)],
        [NSValue valueWithCATransform3D:bounce],
        [NSValue valueWithCATransform3D:settle],
        [NSValue valueWithCATransform3D:neutral]
    ] : @[
        [NSValue valueWithCATransform3D:neutral],
        [NSValue valueWithCATransform3D:bounce],
        [NSValue valueWithCATransform3D:CATransform3DMakeScale(.86,.86,1)],
        [NSValue valueWithCATransform3D:tucked],
        [NSValue valueWithCATransform3D:tucked]
    ];
    shape.keyTimes=@[@0,@.40,@.64,@.82,@1];
    shape.calculationMode=kCAAnimationLinear;
    CAKeyframeAnimation *fade=[CAKeyframeAnimation animationWithKeyPath:@"opacity"];
    fade.values=opening ? @[@0,@.68,@1,@1,@1] : @[@1,@1,@.65,@0,@0];
    fade.keyTimes=shape.keyTimes;
    CAAnimationGroup *group=[CAAnimationGroup animation];
    group.animations=@[shape,fade];
    group.duration=opening ? .44 : .25;
    group.beginTime=CACurrentMediaTime() + (opening ? .09 + index*.045 : index*.012);
    group.fillMode=kCAFillModeBackwards;
    group.removedOnCompletion=YES;
    [control.layer addAnimation:group forKey:@"edgeContent"];
}
- (void)animate {
    BOOL stateChanged = self.expanded != self.lastDrawnExpanded;
    if (!stateChanged) {
        // Only the inner volume capsule changes: keep the outer rail still.
        [UIView animateWithDuration:.35 delay:0 usingSpringWithDamping:.78
              initialSpringVelocity:.3 options:UIViewAnimationOptionBeginFromCurrentState|
              UIViewAnimationOptionAllowUserInteraction animations:^{
            [self layoutEdge:NO];
        } completion:nil];
        return;
    }
    BOOL reducedMotion = UIAccessibilityIsReduceMotionEnabled();
    [self layoutEdge:!reducedMotion];
    if (reducedMotion) return;
    BOOL opening=self.expanded;
    [self animateEdgeControl:self.sync index:0 opening:opening];
    [self animateEdgeControl:self.volumeCapsule index:1 opening:opening];
    for (NSUInteger i=0;i<self.shortcuts.count;i++)
        [self animateEdgeControl:self.shortcuts[i] index:i+2 opening:opening];
}
- (void)toggle:(id)sender {
    [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
    if (self.expanded) { [self closeEdge]; return; }
    self.expanded=YES;
    [self animate];
    // Enable only after the opening touch is finished, not on touch-down.
    dispatch_async(dispatch_get_main_queue(), ^{ if (self.expanded) self.outsideTap.enabled = YES; });
    [self restartIdleTimer];
    // The app list is cached when attaching the overlay; do not relayout
    // during the opening animation or while UIKit is delivering a touch.
}
- (void)toggleVolume:(id)sender {
    if (!self.volumeExpanded) [self refreshGuestGain];
    self.volumeExpanded=!self.volumeExpanded;
    [self animate];
    [self restartIdleTimer];
}
- (void)resync:(id)sender {
    [self restartIdleTimer];
    if ([NSUserDefaults.lcGuestAppId.lowercaseString isEqualToString:@"com.google.ios.youtubemusic"] && MBMHEnabled()) {
        MBMHForceResync();
        [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium] impactOccurred];
    } else {
        UINotificationFeedbackGenerator *g=[UINotificationFeedbackGenerator new];
        [g notificationOccurred:UINotificationFeedbackTypeWarning];
    }
}
- (void)openApp:(UIButton *)sender {
    if ((NSUInteger)sender.tag>=self.apps.count) return;
    NSDictionary *a=self.apps[sender.tag];
    [self closeEdge];
    if ([a[@"id"] isEqualToString:NSUserDefaults.lcGuestAppId.lowercaseString]) return;
    NSURLComponents *url=[NSURLComponents new]; url.scheme=@"bloomboxmedia"; url.host=@"livecontainer-launch";
    NSMutableArray *items=[NSMutableArray arrayWithObject:[NSURLQueryItem queryItemWithName:@"bundle-name" value:a[@"path"]]];
    if ([a[@"folder"] length]) [items addObject:[NSURLQueryItem queryItemWithName:@"container-folder-name" value:a[@"folder"]]];
    url.queryItems=items;
    if (url.URL) {
        // Use the established guest URL route; honor lock checks and the
        // existing LiveContainer switch confirmation policy.
        [UIApplication.sharedApplication openURL:url.URL options:@{} completionHandler:^(BOOL ok) {
            if (!ok) [[[UINotificationFeedbackGenerator alloc] init] notificationOccurred:UINotificationFeedbackTypeWarning];
        }];
    }
}
@end

static MBEdgeController *gMBEdgeController;
static UIWindow *MBEdgeGuestWindow(void) {
    UIWindowScene *scene=MBActiveVolumeScene();
    if (!scene) return nil;
    UIWindow *candidate=nil;
    for (UIWindow *window in scene.windows) {
        if (window.hidden || window.alpha<.01 || !window.rootViewController ||
            window.windowLevel!=UIWindowLevelNormal) continue;
        if (window.isKeyWindow) return window;
        if (!candidate) candidate=window;
    }
    return candidate;
}
static void MBEnsureGuestEdge(void) {
    if (!MBEdgeEnabled()) {
        [gMBEdgeController attachActivityObserverToWindow:nil];
        [gMBEdgeController.view removeFromSuperview];
        return;
    }
    UIWindow *window=MBEdgeGuestWindow();
    if (!window) return;
    if (!gMBEdgeController) gMBEdgeController=[MBEdgeController new];
    UIView *view=gMBEdgeController.view;
    if (view.superview!=window) {
        [view removeFromSuperview];
        view.frame=window.bounds;
        [window addSubview:view];
        [gMBEdgeController attachActivityObserverToWindow:window];
        [gMBEdgeController refreshApps];
        [gMBEdgeController refreshGuestGain];
        [gMBEdgeController layoutEdge:NO];
    } else if (!CGRectEqualToRect(view.frame,window.bounds)) {
        view.frame=window.bounds;
        [gMBEdgeController layoutEdge:NO];
    }
    // Honor new app selections without restarting the guest or listing apps
    // every frame. This checks a tiny defaults array on the existing tick.
    id configured = [NSUserDefaults.lcSharedDefaults objectForKey:@"MediaBloomEdgeAppIDsV1"];
    NSArray *selection = [configured isKindOfClass:NSArray.class] ? configured : nil;
    if (!gMBEdgeController.expanded && !gMBEdgeController.controlIsTouching &&
        (selection || gMBEdgeController.selectionSnapshot) &&
        ![selection isEqualToArray:gMBEdgeController.selectionSnapshot]) {
        [gMBEdgeController refreshApps];
        [gMBEdgeController layoutEdge:NO];
    }
    // Media guests can add sibling views over Edge after playback starts.
    // Correct z-order only when needed and never during an active control touch.
    if (view.superview == window && window.subviews.lastObject != view &&
        !gMBEdgeController.controlIsTouching && !gMBEdgeController.tab.tracking &&
        !gMBEdgeController.slider.tracking) {
        [window bringSubviewToFront:view];
    }
}
static void MBInstallGuestEdge(void) {
    if (!MBIsMediaHost()) return;
    NSString *guest=NSUserDefaults.lcGuestAppId.lowercaseString;
    if (!guest.length) return;
    BOOL found=NO;
    for (NSDictionary *a in MBEdgeApps()) if ([a[@"id"] isEqualToString:guest]) { found=YES; break; }
    if (!found) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        MBEnsureGuestEdge();
        [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer *t){ MBEnsureGuestEdge(); }];
    });
}
