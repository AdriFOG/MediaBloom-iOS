// MediaBloom only: native in-guest Edge menu. No changes to primary BloomBox.
#import <UIKit/UIKit.h>
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
@interface MBEdgeButton : UIButton
@property(nonatomic,assign) CGFloat touchPadding;
@property(nonatomic,strong) CAShapeLayer *railShape;
@property(nonatomic,assign) BOOL railExpanded;
@end
@implementation MBEdgeButton
- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)event {
    if (!self.railShape) {
        return CGRectContainsPoint(CGRectInset(self.bounds,-self.touchPadding,-self.touchPadding),p);
    }
    // The visible rail and its touch target belong to the SAME button.
    // Convert into the drawing layer's own local coordinates, not screen coordinates.
    CAShapeLayer *shape = self.railShape;
    CGPoint local = [shape convertPoint:p fromLayer:self.layer];
    CAShapeLayer *visible = (CAShapeLayer *)shape.presentationLayer;
    CGPathRef path = visible.path ?: shape.path;
    if (!path) return NO;
    if (CGPathContainsPoint(path,NULL,local,false)) return YES;
    if (self.railExpanded) return NO;
    // Only a small tolerance around the visible closed tab, never a remote hitbox.
    return CGRectContainsPoint(CGRectInset(CGPathGetPathBoundingBox(path),-14,-7),local);
}
@end

// A subview of the actual guest window, not a second transparent UIWindow.
// UIKit resolves touches normally within the guest's own responder hierarchy.
@interface MBEdgeTouchView : UIView
@property(nonatomic,copy) void (^boundsDidChange)(void);
@property(nonatomic,assign) CGRect lastLayoutBounds;
@end
@implementation MBEdgeTouchView
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
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
           (gMBAudioAVPlayerSetVolume || gMBAudioAVAudioPlayerSetVolume || gMBAudioAVRendererSetVolume);
}

@interface MBEdgeController : UIViewController
@property(nonatomic,strong) CAShapeLayer *rail;
@property(nonatomic,strong) MBEdgeButton *tab;
@property(nonatomic,strong) UIButton *sync;
@property(nonatomic,strong) UIButton *speaker;
@property(nonatomic,strong) UIView *volumeCapsule;
@property(nonatomic,strong) UISlider *slider;
@property(nonatomic,strong) UILabel *volumeUnavailable;
@property(nonatomic,strong) NSMutableArray<UIButton *> *shortcuts;
@property(nonatomic,strong) NSArray<NSDictionary *> *apps;
@property(nonatomic,assign) BOOL expanded;
@property(nonatomic,assign) BOOL volumeExpanded;
@property(nonatomic,assign) BOOL right;
@property(nonatomic,assign) CGFloat anchor;
@end

@implementation MBEdgeController
- (UIButton *)button:(NSString *)symbol color:(UIColor *)color label:(NSString *)label action:(SEL)action {
    MBEdgeButton *b = [MBEdgeButton buttonWithType:UIButtonTypeCustom];
    b.touchPadding = 4;
    b.tintColor = UIColor.whiteColor;
    b.backgroundColor = color;
    b.layer.cornerRadius = 22.5;
    [b setImage:[UIImage systemImageNamed:symbol] forState:UIControlStateNormal];
    b.accessibilityLabel = label;
    [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return b;
}
- (void)loadView {
    self.view = [[MBEdgeTouchView alloc] initWithFrame:CGRectZero];
    self.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view.backgroundColor = UIColor.clearColor;
    self.anchor = .54;
    self.rail = [CAShapeLayer layer];
    self.rail.fillColor = UIColor.blackColor.CGColor;
    self.tab = (MBEdgeButton *)[self button:@"" color:UIColor.clearColor label:@"MediaBloom Edge" action:@selector(toggle:)];
    self.tab.railShape = self.rail;
    self.tab.layer.cornerRadius = 0;
    self.tab.clipsToBounds = NO;
    [self.tab.layer addSublayer:self.rail];
    [self.tab setImage:nil forState:UIControlStateNormal];
    UIView *mark = [[UIView alloc] initWithFrame:CGRectMake(9,20,2,25)];
    mark.tag = 101;
    mark.backgroundColor = [UIColor colorWithRed:.60 green:.43 blue:.93 alpha:1];
    mark.layer.cornerRadius = 1;
    mark.userInteractionEnabled = NO;
    [self.tab addSubview:mark];
    [self.view addSubview:self.tab];
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
- (void)refreshGuestGain {
    NSString *guest = NSUserDefaults.lcGuestAppId.lowercaseString;
    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:guest ?: @""];
    id saved = [NSUserDefaults.lcSharedDefaults objectForKey:key];
    self.slider.value = [saved respondsToSelector:@selector(floatValue)] ? MBClampVolume([saved floatValue]) : 1;
    BOOL available = MBEdgeLocalGainAvailable();
    self.slider.enabled = available;
    self.volumeUnavailable.text = MBIsSupportedGuest(guest) ? @"Activa BloomAudio" : @"No compatible";
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
}
- (void)refreshApps {
    NSArray *all = MBEdgeApps();
    NSArray *next = [all subarrayWithRange:NSMakeRange(0,MIN((NSUInteger)4,all.count))];
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
    // Fix the layer's frame explicitly: a free-standing CAShapeLayer may
    // otherwise draw outside zero-sized bounds while hit testing elsewhere.
    self.tab.frame = bounds;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.rail.frame = self.tab.bounds;
    [CATransaction commit];
    self.tab.railExpanded = self.expanded;
    UIBezierPath *p=[self pathWidth:width height:height center:cy];
    if (animated && self.rail.path) {
        CABasicAnimation *m=[CABasicAnimation animationWithKeyPath:@"path"];
        CAShapeLayer *presentation=(CAShapeLayer *)self.rail.presentationLayer;
        m.fromValue=(__bridge id)(presentation.path ?: self.rail.path);
        m.toValue=(__bridge id)p.CGPath; m.duration=.40;
        m.timingFunction=[CAMediaTimingFunction functionWithControlPoints:.20 :.9 :.25 :1.0];
        [self.rail addAnimation:m forKey:@"edgeMorph"];
    }
    [CATransaction begin]; [CATransaction setDisableActions:YES]; self.rail.path=p.CGPath; [CATransaction commit];
    CGFloat x=self.right?w-38:38, size=45*scale;
    CGFloat row=55*scale, first=cy-145*scale;
    UIView *mark = [self.tab viewWithTag:101];
    mark.frame = CGRectMake(self.right?w-6:5,cy-12.5,2,25);
    mark.alpha = self.expanded ? 0 : 1;
    self.tab.accessibilityLabel = self.expanded ? @"Cerrar menú lateral" : @"Abrir menú lateral";
    self.tab.accessibilityFrameInContainerSpace = CGRectIntersection(bounds,CGRectInset(CGPathGetPathBoundingBox(p.CGPath),-14,-7));
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

}
- (void)viewDidLayoutSubviews { [super viewDidLayoutSubviews]; [self layoutEdge:NO]; }
- (void)animate {
    [UIView animateWithDuration:.42 delay:0 usingSpringWithDamping:.72 initialSpringVelocity:0 options:UIViewAnimationOptionBeginFromCurrentState|UIViewAnimationOptionAllowUserInteraction animations:^{ [self layoutEdge:YES]; } completion:nil];
}
- (void)toggle:(id)sender {
    BOOL opening=!self.expanded;
    self.expanded=!self.expanded; if (!self.expanded) self.volumeExpanded=NO;
    [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
    [self animate];
    if (opening) {
        // Cached on attachment; refresh after the opening tap has completed.
        dispatch_async(dispatch_get_main_queue(), ^{
            [self refreshApps];
            [self layoutEdge:NO];
        });
    }
}
- (void)toggleVolume:(id)sender {
    if (!self.volumeExpanded) [self refreshGuestGain];
    self.volumeExpanded=!self.volumeExpanded;
    [self animate];
}
- (void)resync:(id)sender {
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
    self.expanded=NO; self.volumeExpanded=NO; [self animate];
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
        [gMBEdgeController refreshApps];
        [gMBEdgeController refreshGuestGain];
        [gMBEdgeController layoutEdge:NO];
    } else if (!CGRectEqualToRect(view.frame,window.bounds)) {
        view.frame=window.bounds;
        [gMBEdgeController layoutEdge:NO];
    }
    // Do not repeatedly reattach or reorder the view while a touch is active.
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
