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
@end
@implementation MBEdgeButton
- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)event {
    return CGRectContainsPoint(CGRectInset(self.bounds,-self.touchPadding,-self.touchPadding),p);
}
@end

@interface MBEdgeController : UIViewController
@property(nonatomic,strong) CAShapeLayer *rail;
@property(nonatomic,strong) UIButton *tab;
@property(nonatomic,strong) UIButton *railClose;
@property(nonatomic,strong) UIButton *sync;
@property(nonatomic,strong) UIButton *speaker;
@property(nonatomic,strong) UIView *volumeCapsule;
@property(nonatomic,strong) UIView *slider;
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
    self.view = [[UIView alloc] initWithFrame:CGRectZero];
    self.view.backgroundColor = UIColor.clearColor;
    self.anchor = .54;
    self.rail = [CAShapeLayer layer];
    self.rail.fillColor = UIColor.blackColor.CGColor;
    [self.view.layer addSublayer:self.rail];
    self.railClose = [self button:@"" color:UIColor.clearColor label:@"Cerrar menú lateral" action:@selector(toggle:)];
    [self.view addSubview:self.railClose];
    self.tab = [self button:@"line.3.horizontal" color:UIColor.clearColor label:@"MediaBloom Edge" action:@selector(toggle:)];
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
    dlopen("/System/Library/Frameworks/MediaPlayer.framework/MediaPlayer", RTLD_LAZY);
    Class volumeClass = NSClassFromString(@"MPVolumeView");
    if (volumeClass) {
        self.slider = [[volumeClass alloc] initWithFrame:CGRectZero];
        SEL route = NSSelectorFromString(@"setShowsRouteButton:");
        if ([self.slider respondsToSelector:route]) ((void(*)(id,SEL,BOOL))objc_msgSend)(self.slider,route,NO);
        self.slider.tintColor = UIColor.systemPurpleColor;
        [self.volumeCapsule insertSubview:self.slider belowSubview:self.speaker];
    }
    self.shortcuts = [NSMutableArray array];
    self.apps = @[];
    UILongPressGestureRecognizer *pan = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(drag:)];
    pan.minimumPressDuration = .35;
    pan.cancelsTouchesInView = YES;
    [self.tab addGestureRecognizer:pan];
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
    CGFloat edge = self.right ? self.view.bounds.size.width : 0;
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
    self.tab.frame=CGRectMake(self.right?w-48:0,cy-48,48,96);
    UIView *mark = [self.tab viewWithTag:101];
    mark.frame = CGRectMake(self.right?42:5,35,2,25);
    self.tab.alpha=self.expanded?0:1; self.tab.userInteractionEnabled=!self.expanded;
    self.railClose.frame=CGRectMake(self.right?w-width:0,cy-height/2,width,height);
    self.railClose.hidden=!self.expanded;
    self.railClose.userInteractionEnabled=self.expanded;
    self.sync.frame=CGRectMake(x-size/2,first-size/2,size,size);
    CGFloat volY=first+row;
    CGFloat capWidth=self.volumeExpanded?MIN(215,w-32):size;
    self.volumeCapsule.frame=CGRectMake(self.right?x+size/2-capWidth:x-size/2,volY-size/2,capWidth,size);
    self.volumeCapsule.layer.cornerRadius=size/2;
    self.speaker.frame=CGRectMake(self.right?capWidth-size:0,0,size,size);
    self.slider.frame=CGRectMake(self.right?12:size+4,0,MAX(0,capWidth-size-16),size);
    self.slider.alpha=self.volumeExpanded?1:0;
    self.slider.userInteractionEnabled=self.volumeExpanded;
    self.sync.alpha=self.expanded?1:0; self.sync.userInteractionEnabled=self.expanded;
    self.volumeCapsule.alpha=self.expanded?1:0; self.volumeCapsule.userInteractionEnabled=self.expanded;
    for (NSUInteger i=0;i<self.shortcuts.count;i++) {
        UIButton *b=self.shortcuts[i];
        b.frame=CGRectMake(x-size/2,volY+row*(i+1)+15*scale-size/2,size,size);
        b.layer.cornerRadius=size*.23; b.alpha=self.expanded?1:0; b.userInteractionEnabled=self.expanded;
    }
    self.sync.layer.cornerRadius=size/2;
    [self.view bringSubviewToFront:self.tab];
    [self setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
}
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return self.right ? UIRectEdgeRight : UIRectEdgeLeft; }
- (void)viewDidLayoutSubviews { [super viewDidLayoutSubviews]; [self layoutEdge:NO]; }
- (void)animate {
    [UIView animateWithDuration:.42 delay:0 usingSpringWithDamping:.72 initialSpringVelocity:0 options:UIViewAnimationOptionBeginFromCurrentState|UIViewAnimationOptionAllowUserInteraction animations:^{ [self layoutEdge:YES]; } completion:nil];
}
- (void)toggle:(id)sender {
    if (!self.expanded) [self refreshApps];
    self.expanded=!self.expanded; if (!self.expanded) self.volumeExpanded=NO;
    [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];
    [self animate];
}
- (void)toggleVolume:(id)sender { self.volumeExpanded=!self.volumeExpanded; [self animate]; }
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
- (void)drag:(UILongPressGestureRecognizer *)pan {
    if (pan.state==UIGestureRecognizerStateChanged) {
        CGFloat h=self.view.bounds.size.height;
        if (h>0) self.anchor=MAX(.2,MIN(.8,[pan locationInView:self.view].y/h));
        [self layoutEdge:NO];
    }
}
@end

// The rail itself closes when tapped, but all unrelated touches pass through.
@interface MBEdgeWindow : UIWindow
@end
@implementation MBEdgeWindow
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    if (self.hidden || self.alpha < .01 || !self.userInteractionEnabled) return nil;
    MBEdgeController *c=(MBEdgeController *)self.rootViewController;
    if (!c.isViewLoaded) return nil;
    CGPoint local=[c.view convertPoint:point fromView:self];
    // Check controls directly so the transparent overlay never steals a
    // button tap or lets the guest underneath receive it instead.
    if (!c.expanded) {
        CGPoint p=[c.tab convertPoint:local fromView:c.view];
        return [c.tab pointInside:p withEvent:event] ? c.tab : nil;
    }
    NSArray *targets = [@[c.volumeCapsule,c.sync] arrayByAddingObjectsFromArray:c.shortcuts];
    for (UIView *target in targets) {
        if (target.hidden || !target.userInteractionEnabled) continue;
        CGPoint p=[target convertPoint:local fromView:c.view];
        UIView *hit=[target hitTest:p withEvent:event];
        if (hit) return hit;
    }
    if (c.rail.path && CGPathContainsPoint(c.rail.path,NULL,local,false)) return c.railClose;
    return nil;
}
@end

static MBEdgeWindow *gMBEdgeWindow;
static void MBEnsureGuestEdge(void) {
    if (!MBEdgeEnabled()) { gMBEdgeWindow.hidden=YES; return; }
    UIWindowScene *scene=MBActiveVolumeScene();
    if (!scene) return;
    if (!gMBEdgeWindow) {
        MBEdgeController *controller=[MBEdgeController new];
        gMBEdgeWindow=[[MBEdgeWindow alloc] initWithWindowScene:scene];
        gMBEdgeWindow.rootViewController=controller;
        gMBEdgeWindow.windowLevel=UIWindowLevelNormal+101;
        gMBEdgeWindow.backgroundColor=UIColor.clearColor;
        gMBEdgeWindow.frame=scene.coordinateSpace.bounds;
        gMBEdgeWindow.hidden=NO; // never become the guest's key window
    } else {
        if (gMBEdgeWindow.windowScene!=scene) gMBEdgeWindow.windowScene=scene;
        gMBEdgeWindow.hidden=NO;
        if (!CGRectEqualToRect(gMBEdgeWindow.frame,scene.coordinateSpace.bounds)) gMBEdgeWindow.frame=scene.coordinateSpace.bounds;
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
