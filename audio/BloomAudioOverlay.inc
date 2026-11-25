#include <objc/message.h>

// BloomAudio in-guest control. Only compiled in the independent MediaBloom IPA.
// A small, moveable speaker bubble remains available while a supported guest
// is open. It never becomes the key window and never sets iOS device volume.

static UIWindow *gMBVolumeWindow = nil;
static CGFloat gMBVolumeAnchorY = -1.0f;
static BOOL gMBVolumeExpanded = NO;
static const CGFloat MBCompactWidth = 52.0f;
static const CGFloat MBPanelWidth = 292.0f;
static const CGFloat MBPanelHeight = 124.0f;
static NSTimeInterval gMBLastControlReloadAt = 0.0;


static void MBRepositionVolumeWindow(void);
static void MBApplyLiveGain(void);

@interface MBVolumePassThroughWindow : UIWindow
@end

@implementation MBVolumePassThroughWindow
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit = [super hitTest:point withEvent:event];
    if (hit == self || hit == self.rootViewController.view) return nil;
    return hit;
}
@end

@interface MBVolumeOverlayController : UIViewController
@property(nonatomic, strong) UIView *panel;
@property(nonatomic, strong) UIButton *bubble;
@property(nonatomic, strong) UILabel *guestLabel;
@property(nonatomic, strong) UILabel *percentLabel;
@property(nonatomic, strong) UISlider *gainSlider;
@property(nonatomic, strong) UIButton *muteButton;
@property(nonatomic, assign) float previousNonzeroLevel;
@end

@implementation MBVolumeOverlayController

- (void)loadView {
    UIView *root = [[UIView alloc] initWithFrame:CGRectMake(0, 0, MBCompactWidth, MBCompactWidth)];
    root.backgroundColor = UIColor.clearColor;
    self.view = root;

    UIColor *purple = [UIColor colorWithRed:0.49 green:0.34 blue:0.88 alpha:1.0];
    UIColor *backdrop = [UIColor colorWithRed:0.10 green:0.09 blue:0.16 alpha:0.97];

    self.panel = [[UIView alloc] initWithFrame:CGRectZero];
    self.panel.backgroundColor = backdrop;
    self.panel.layer.cornerRadius = 18;
    self.panel.layer.borderWidth = 0.5;
    self.panel.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.18].CGColor;
    self.panel.hidden = YES;
    [root addSubview:self.panel];

    self.guestLabel = [[UILabel alloc] init];
    self.guestLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    self.guestLabel.textColor = UIColor.whiteColor;
    self.guestLabel.text = [gMBAudioGuest containsString:@"music"] ? @"YouTube Music" : @"YouTube";
    [self.panel addSubview:self.guestLabel];

    self.percentLabel = [[UILabel alloc] init];
    self.percentLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.85];
    self.percentLabel.font = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightMedium];
    self.percentLabel.textAlignment = NSTextAlignmentRight;
    [self.panel addSubview:self.percentLabel];

    self.gainSlider = [[UISlider alloc] init];
    self.gainSlider.minimumValue = 0.0f;
    self.gainSlider.maximumValue = 1.0f;
    self.gainSlider.minimumTrackTintColor = purple;
    self.gainSlider.maximumTrackTintColor = [UIColor colorWithWhite:1 alpha:0.24];
    self.gainSlider.thumbTintColor = UIColor.whiteColor;
    self.gainSlider.continuous = YES;
    [self.gainSlider addTarget:self action:@selector(sliderMoved:) forControlEvents:UIControlEventValueChanged];
    [self.panel addSubview:self.gainSlider];

    self.muteButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.muteButton.tintColor = UIColor.whiteColor;
    [self.muteButton addTarget:self action:@selector(toggleMute:) forControlEvents:UIControlEventTouchUpInside];
    self.muteButton.accessibilityLabel = @"Silenciar o restaurar sonido";
    [self.panel addSubview:self.muteButton];

    self.bubble = [UIButton buttonWithType:UIButtonTypeSystem];
    self.bubble.backgroundColor = [purple colorWithAlphaComponent:0.96];
    self.bubble.layer.cornerRadius = MBCompactWidth / 2;
    self.bubble.tintColor = UIColor.whiteColor;
    [self.bubble setImage:[UIImage systemImageNamed:@"speaker.wave.2.fill"] forState:UIControlStateNormal];
    self.bubble.accessibilityLabel = @"BloomAudio: abrir o cerrar volumen";
    [self.bubble addTarget:self action:@selector(togglePanel:) forControlEvents:UIControlEventTouchUpInside];
    [root addSubview:self.bubble];

    UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(dragBubble:)];
    pan.cancelsTouchesInView = YES;
    [self.bubble addGestureRecognizer:pan];

    self.previousNonzeroLevel = 1.0f;
    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:gMBAudioGuest ?: @""];
    id stored = [NSUserDefaults.lcSharedDefaults objectForKey:key];
    float initial = [stored respondsToSelector:@selector(floatValue)]
        ? MBClampVolume([stored floatValue]) : 1.0f;
    self.gainSlider.value = initial;
    if (initial > 0.001f) self.previousNonzeroLevel = initial;
    [self updateLabels];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGFloat width = CGRectGetWidth(self.view.bounds);
    CGFloat height = CGRectGetHeight(self.view.bounds);
    self.panel.hidden = !gMBVolumeExpanded;
    self.panel.frame = CGRectMake(0, 0, width, height);
    self.bubble.frame = CGRectMake(width - MBCompactWidth, 0, MBCompactWidth, MBCompactWidth);

    if (gMBVolumeExpanded) {
        self.guestLabel.frame = CGRectMake(16, 15, width - 94, 24);
        self.gainSlider.frame = CGRectMake(16, 52, width - 78, 34);
        self.percentLabel.frame = CGRectMake(width - 59, 57, 48, 26);
        self.muteButton.frame = CGRectMake(16, 91, 38, 27);
    }
}

- (void)updateLabels {
    float gain = self.gainSlider.value;
    self.percentLabel.text = [NSString stringWithFormat:@"%d%%", (int)lroundf(gain * 100)];
    NSString *iconName = gain < 0.001f ? @"speaker.slash.fill" : @"speaker.wave.2.fill";
    [self.bubble setImage:[UIImage systemImageNamed:iconName] forState:UIControlStateNormal];
    [self.muteButton setImage:[UIImage systemImageNamed:iconName] forState:UIControlStateNormal];
    self.gainSlider.accessibilityValue = self.percentLabel.text;
}

- (void)storeValue:(float)value {
    float normalized = MBClampVolume(value);
    self.gainSlider.value = normalized;
    if (normalized > 0.001f) self.previousNonzeroLevel = normalized;
    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:gMBAudioGuest ?: @""];
    [NSUserDefaults.lcSharedDefaults setFloat:normalized forKey:key];
    // The Control Center icon must also reflect changes from the floating
    // BloomAudio slider; previously only the App Intent wrote the master key.
    [NSUserDefaults.lcSharedDefaults setFloat:normalized
                                       forKey:@"MediaBloomMasterGainV1"];
    [NSUserDefaults.lcSharedDefaults synchronize];
    [self updateLabels];
    // Apply the gain immediately, even while the media guest is running.
    MBApplyLiveGain();

    // The Swift host bridge uses the public WidgetKit ControlCenter API.
    // Avoid a reload on every slider sample during long drags.
    NSTimeInterval now = NSDate.timeIntervalSinceReferenceDate;
    if (now - gMBLastControlReloadAt > 0.40) {
        gMBLastControlReloadAt = now;
        Class controlBridge = NSClassFromString(@"MediaBloomControlCenterSync");
        SEL refreshSelector = NSSelectorFromString(@"notify");
        if ([controlBridge respondsToSelector:refreshSelector]) {
            ((void(*)(id, SEL))objc_msgSend)(controlBridge, refreshSelector);
        }
    }
}

- (void)sliderMoved:(UISlider *)slider {
    [self storeValue:slider.value];
}

- (void)toggleMute:(UIButton *)sender {
    if (self.gainSlider.value > 0.001f) {
        [self storeValue:0.0f];
    } else {
        [self storeValue:fmaxf(self.previousNonzeroLevel, 0.50f)];
    }
}

- (void)togglePanel:(UIButton *)sender {
    gMBVolumeExpanded = !gMBVolumeExpanded;
    MBRepositionVolumeWindow();
}

- (void)dragBubble:(UIPanGestureRecognizer *)gesture {
    UIWindow *window = self.view.window;
    if (!window) return;
    CGPoint translation = [gesture translationInView:window];
    if (gesture.state == UIGestureRecognizerStateChanged) {
        gMBVolumeAnchorY += translation.y;
        [gesture setTranslation:CGPointZero inView:window];
        MBRepositionVolumeWindow();
    } else if (gesture.state == UIGestureRecognizerStateEnded ||
               gesture.state == UIGestureRecognizerStateCancelled) {
        CGFloat h = CGRectGetHeight(window.windowScene.coordinateSpace.bounds);
        if (h > 0) {
            [NSUserDefaults.lcSharedDefaults setFloat:(float)(gMBVolumeAnchorY/h)
                                               forKey:@"MediaBloomAudioOverlayYV1"];
        }
    }
}
@end

static MBVolumeOverlayController *gMBVolumeController;

static UIWindowScene *MBActiveVolumeScene(void) {
    if (@available(iOS 13.0, *)) {
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if ([scene isKindOfClass:UIWindowScene.class] &&
                scene.activationState == UISceneActivationStateForegroundActive) {
                return (UIWindowScene *)scene;
            }
        }
    }
    return nil;
}

static void MBRepositionVolumeWindow(void) {
    UIWindow *window = gMBVolumeWindow;
    if (!window || !window.windowScene) return;
    CGRect bounds = window.windowScene.coordinateSpace.bounds;
    CGFloat w = CGRectGetWidth(bounds);
    CGFloat h = CGRectGetHeight(bounds);
    if (w <= 0 || h <= 0) return;

    if (gMBVolumeAnchorY < 0) {
        NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
        id saved = [shared objectForKey:@"MediaBloomAudioOverlayYV1"];
        float fraction = [saved respondsToSelector:@selector(floatValue)]
            ? [saved floatValue] : 0.48f;
        if (!isfinite(fraction)) fraction = 0.48f;
        gMBVolumeAnchorY = h * fmaxf(0.15f, fminf(0.80f, fraction));
    }
    CGFloat desiredWidth = gMBVolumeExpanded ? MBPanelWidth : MBCompactWidth;
    CGFloat desiredHeight = gMBVolumeExpanded ? MBPanelHeight : MBCompactWidth;
    CGFloat width = fminf(desiredWidth, w - 16);
    CGFloat height = fminf(desiredHeight, h - 16);
    gMBVolumeAnchorY = fmaxf(55, fminf(h - height - 26, gMBVolumeAnchorY));
    CGFloat x = fmaxf(8, w - width - 10);
    CGRect newFrame = CGRectMake(x, gMBVolumeAnchorY, width, height);
    if (!CGRectEqualToRect(window.frame, newFrame)) window.frame = newFrame;
    [gMBVolumeController.view setNeedsLayout];
}

static void MBEnsureVolumeOverlay(void) {
    if (![NSThread isMainThread]) return;
    UIWindowScene *scene = MBActiveVolumeScene();
    if (!scene) return;
    id edgeChoice = [NSUserDefaults.lcSharedDefaults objectForKey:@"MediaBloomEdgeEnabledV1"];
    if (edgeChoice == nil || [edgeChoice boolValue]) {
        gMBVolumeWindow.hidden = YES;
        return;
    }
    if (gMBVolumeWindow) gMBVolumeWindow.hidden = NO;

    if (!gMBVolumeWindow) {
        gMBVolumeController = [MBVolumeOverlayController new];
        gMBVolumeWindow = [[MBVolumePassThroughWindow alloc] initWithWindowScene:scene];
        gMBVolumeWindow.rootViewController = gMBVolumeController;
        gMBVolumeWindow.backgroundColor = UIColor.clearColor;
        gMBVolumeWindow.windowLevel = UIWindowLevelNormal + 100;
        // Do NOT call makeKeyAndVisible; that steals input from the guest.
        gMBVolumeWindow.hidden = NO;
    } else if (gMBVolumeWindow.windowScene != scene) {
        gMBVolumeWindow.windowScene = scene;
    }
    MBRepositionVolumeWindow();
}

static void MBStartVolumeOverlay(void) {
    MBEnsureVolumeOverlay();
    // Only one monitor per guest process. No audio samples are captured.
    [NSTimer scheduledTimerWithTimeInterval:0.7 repeats:YES block:^(NSTimer *timer) {
        MBEnsureVolumeOverlay();
    }];
}
