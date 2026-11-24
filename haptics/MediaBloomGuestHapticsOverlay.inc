// MediaBloom: compact, opt-in native Music Haptics indicator inside YouTube Music.
// This only reads the existing Apple status and offers a safe Now Playing retry.
// No player pause, seek, microphone, audio hooks, or fabricated progress percent.
#import <UIKit/UIKit.h>

static NSString *const kMBMHGuestOverlayKey = @"MediaBloomHapticsGuestOverlayV1";
static UIView *gMBMHOverlayView = nil;
static UILabel *gMBMHOverlayTitle = nil;
static UILabel *gMBMHOverlaySong = nil;
static UIActivityIndicatorView *gMBMHOverlaySpinner = nil;
static NSString *gMBMHOverlayLastState = nil;

@interface MBMHOverlayRetryTarget : NSObject
- (void)mbmhRetry:(id)sender;
@end

@implementation MBMHOverlayRetryTarget
- (void)mbmhRetry:(id)sender {
    MBMHForceResync();
}
@end

static MBMHOverlayRetryTarget *gMBMHOverlayTarget = nil;

static BOOL MBMHOverlayAllowed(void) {
    if (!MBMHEnabled()) return NO;
    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    id choice = [shared objectForKey:kMBMHGuestOverlayKey];
    return choice == nil || [choice boolValue];
}

static NSString *MBMHOverlayText(NSString *state) {
    if ([state isEqualToString:@"searching"]) return @"Preparando Music Haptics…";
    if ([state isEqualToString:@"checking"]) return @"Comprobando vibraciones…";
    if ([state isEqualToString:@"resyncing"]) return @"Resincronizando…";
    if ([state isEqualToString:@"playing"]) return @"Music Haptics activo";
    if ([state isEqualToString:@"ready"]) return @"Pista háptica lista";
    if ([state isEqualToString:@"unavailable"]) return @"Sin pista háptica compatible";
    if ([state isEqualToString:@"disabled"]) return @"Activa Music Haptics en iOS";
    if ([state isEqualToString:@"error"]) return @"No se pudo cargar Music Haptics";
    return @"Music Haptics";
}

static UIWindow *MBMHOverlayWindow(void) {
    UIApplication *app = UIApplication.sharedApplication;
    if (app.applicationState == UIApplicationStateBackground) return nil;
    UIWindow *candidate = nil;
    for (UIWindow *window in app.windows) {
        if (window.hidden || window.alpha <= 0 || !window.rootViewController) continue;
        if (window.windowLevel != UIWindowLevelNormal) continue;
        if (window.isKeyWindow) return window;
        if (!candidate) candidate = window;
    }
    return candidate;
}

static void MBMHUpdateGuestOverlay(void) {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{ MBMHUpdateGuestOverlay(); });
        return;
    }
    if (!MBMHOverlayAllowed()) {
        [gMBMHOverlayView removeFromSuperview];
        gMBMHOverlayView = nil;
        gMBMHOverlayTitle = nil;
        gMBMHOverlaySong = nil;
        gMBMHOverlaySpinner = nil;
        return;
    }
    UIWindow *window = MBMHOverlayWindow();
    if (!window || !gMBMHSongSignature.length) return;

    NSUserDefaults *shared = NSUserDefaults.lcSharedDefaults;
    NSString *state = [shared stringForKey:kMBMHStatusKey] ?: @"searching";
    NSString *song = [shared stringForKey:kMBMHStatusSongKey] ?: @"";
    if (!gMBMHOverlayView) {
        UIView *root = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 312, 54)];
        root.backgroundColor = UIColor.clearColor;
        root.layer.cornerRadius = 16;
        root.clipsToBounds = YES;
        root.layer.shadowOpacity = 0;

        UIVisualEffectView *blur = [[UIVisualEffectView alloc]
            initWithEffect:[UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemThinMaterialDark]];
        blur.frame = root.bounds;
        blur.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                               UIViewAutoresizingFlexibleHeight;
        [root addSubview:blur];

        UIActivityIndicatorView *spinner = [[UIActivityIndicatorView alloc]
            initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
        spinner.color = UIColor.whiteColor;
        spinner.frame = CGRectMake(8, 17, 21, 21);
        [root addSubview:spinner];

        UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(35, 7, 212, 21)];
        title.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
        title.textColor = UIColor.whiteColor;
        title.numberOfLines = 1;
        title.adjustsFontSizeToFitWidth = YES;
        title.minimumScaleFactor = 0.75;
        [root addSubview:title];

        UILabel *subtitle = [[UILabel alloc] initWithFrame:CGRectMake(35, 27, 208, 18)];
        subtitle.font = [UIFont systemFontOfSize:11 weight:UIFontWeightRegular];
        subtitle.textColor = UIColor.whiteColor;
        subtitle.alpha = 0.8;
        subtitle.numberOfLines = 1;
        subtitle.lineBreakMode = NSLineBreakByTruncatingTail;
        [root addSubview:subtitle];

        UIButton *retry = [UIButton buttonWithType:UIButtonTypeSystem];
        retry.frame = CGRectMake(254, 8, 50, 38);
        retry.titleLabel.font = [UIFont systemFontOfSize:20 weight:UIFontWeightMedium];
        [retry setTitle:@"↻" forState:UIControlStateNormal];
        [retry setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
        retry.accessibilityLabel = @"Resincronizar Music Haptics";
        retry.layer.cornerRadius = 12;
        retry.backgroundColor = [UIColor.whiteColor colorWithAlphaComponent:0.13];
        if (!gMBMHOverlayTarget) gMBMHOverlayTarget = [MBMHOverlayRetryTarget new];
        [retry addTarget:gMBMHOverlayTarget action:@selector(mbmhRetry:)
            forControlEvents:UIControlEventTouchUpInside];
        [root addSubview:retry];

        gMBMHOverlayView = root;
        gMBMHOverlayTitle = title;
        gMBMHOverlaySong = subtitle;
        gMBMHOverlaySpinner = spinner;
    }
    CGFloat width = MIN((CGFloat)312.0, MAX((CGFloat)240.0, window.bounds.size.width - 24.0));
    CGFloat x = (window.bounds.size.width - width) / 2.0;
    CGFloat y = window.safeAreaInsets.top + 6;
    gMBMHOverlayView.frame = CGRectMake(x, y, width, 54);
    gMBMHOverlayTitle.frame = CGRectMake(35, 7, width - 100, 21);
    gMBMHOverlaySong.frame = CGRectMake(35, 27, width - 104, 18);
    UIButton *retry = [gMBMHOverlayView.subviews lastObject];
    retry.frame = CGRectMake(width - 58, 8, 50, 38);
    if (gMBMHOverlayView.superview != window) {
        [gMBMHOverlayView removeFromSuperview];
        [window addSubview:gMBMHOverlayView];
    }
    gMBMHOverlayTitle.text = MBMHOverlayText(state);
    gMBMHOverlaySong.text = song;
    BOOL loading = [@[@"searching", @"checking", @"resyncing"]
        containsObject:state];
    if (loading) {
        gMBMHOverlaySpinner.hidden = NO;
        [gMBMHOverlaySpinner startAnimating];
    } else {
        [gMBMHOverlaySpinner stopAnimating];
        gMBMHOverlaySpinner.hidden = YES;
    }
    // A real spinner represents an unknown wait; we deliberately show no
    // fake percentage because Apple's haptic download has no progress API.
    if (![state isEqualToString:gMBMHOverlayLastState]) {
        gMBMHOverlayLastState = [state copy];
    }
}
