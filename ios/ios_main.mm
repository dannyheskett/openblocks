// iOS app shell: a UIKit application hosting a CAMetalLayer view driven by a
// CADisplayLink. No Storyboard, no scene manifest — a classic AppDelegate
// window. The display link runs the shared game loop (ob_app_frame); touches and
// gesture recognizers feed the platform layer (plat_ios) that the game polls.
#import <UIKit/UIKit.h>

#import "app.h"
#import "ob_types.h"
#import "gfx_metal.h"
#import "plat_ios.h"

@interface OBMetalView : UIView
@property (nonatomic) BOOL started;
@end

@implementation OBMetalView

+ (Class)layerClass { return [CAMetalLayer class]; }

- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (!self.window || self.started) return;
    self.started = YES;

    self.multipleTouchEnabled = YES;
    gfx_metal_attach((CAMetalLayer*)self.layer);
    [self updateDrawableSize];
    ob_app_init();

    // Gesture recognizers (don't swallow the raw touches the game also reads).
    UITapGestureRecognizer* tap =
        [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(onTap:)];
    tap.cancelsTouchesInView = NO;
    [self addGestureRecognizer:tap];
    UISwipeGestureRecognizer* up =
        [[UISwipeGestureRecognizer alloc] initWithTarget:self action:@selector(onSwipeUp:)];
    up.direction = UISwipeGestureRecognizerDirectionUp;
    up.cancelsTouchesInView = NO;
    [self addGestureRecognizer:up];
    UISwipeGestureRecognizer* down =
        [[UISwipeGestureRecognizer alloc] initWithTarget:self action:@selector(onSwipeDown:)];
    down.direction = UISwipeGestureRecognizerDirectionDown;
    down.cancelsTouchesInView = NO;
    [self addGestureRecognizer:down];

    CADisplayLink* link = [CADisplayLink displayLinkWithTarget:self selector:@selector(onFrame:)];
    link.preferredFramesPerSecond = 60;
    [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)layoutSubviews { [super layoutSubviews]; [self updateDrawableSize]; }

- (void)updateDrawableSize {
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    CGFloat scale = self.window ? self.window.screen.scale : [UIScreen mainScreen].scale;
    self.contentScaleFactor = scale;
    layer.contentsScale = scale;
    CGSize px = CGSizeMake(self.bounds.size.width * scale, self.bounds.size.height * scale);
    if (px.width < 1) px.width = 1;
    if (px.height < 1) px.height = 1;
    layer.drawableSize = px;
    gfx_metal_resize((int)px.width, (int)px.height);
    plat_ios_set_screen((int)px.width, (int)px.height);
}

- (void)onFrame:(CADisplayLink*)link { (void)link; ob_app_frame(); }

// --- Touches: publish the set of active points (in drawable pixels). --------
- (void)publishTouches:(UIEvent*)event {
    NSSet<UITouch*>* all = [event allTouches];
    Vector2 pts[16];
    int n = 0;
    CGFloat scale = self.contentScaleFactor;
    for (UITouch* t in all) {
        if (t.phase == UITouchPhaseEnded || t.phase == UITouchPhaseCancelled) continue;
        if (n >= 16) break;
        CGPoint p = [t locationInView:self];
        pts[n].x = (float)(p.x * scale);
        pts[n].y = (float)(p.y * scale);
        n++;
    }
    plat_ios_set_touches(pts, n);
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { (void)touches; [self publishTouches:event]; }
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { (void)touches; [self publishTouches:event]; }
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { (void)touches; [self publishTouches:event]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { (void)touches; [self publishTouches:event]; }

- (void)onTap:(UITapGestureRecognizer*)g       { (void)g; plat_ios_post_gesture(GESTURE_TAP); }
- (void)onSwipeUp:(UISwipeGestureRecognizer*)g  { (void)g; plat_ios_post_gesture(GESTURE_SWIPE_UP); }
- (void)onSwipeDown:(UISwipeGestureRecognizer*)g{ (void)g; plat_ios_post_gesture(GESTURE_SWIPE_DOWN); }

@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
        didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)application; (void)launchOptions;
    CGRect bounds = [UIScreen mainScreen].bounds;
    self.window = [[UIWindow alloc] initWithFrame:bounds];
    UIViewController* vc = [[UIViewController alloc] init];
    vc.view = [[OBMetalView alloc] initWithFrame:bounds];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];

    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidBecomeActiveNotification
        object:nil queue:nil usingBlock:^(NSNotification* n){ (void)n; plat_ios_set_focus(true); }];
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillResignActiveNotification
        object:nil queue:nil usingBlock:^(NSNotification* n){ (void)n; plat_ios_set_focus(false); }];
    return YES;
}

@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
