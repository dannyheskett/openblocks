// iOS app shell: a UIKit application hosting a CAMetalLayer-backed view driven
// by a CADisplayLink. No Storyboard, no scene manifest — a classic AppDelegate
// window. For Phase 2 the per-frame tick just clears the Metal drawable, proving
// the toolchain + Metal render in the Simulator. Phase 3 replaces ios_tick() with
// the real game loop (frame_step through the gfx primitives).
#import <UIKit/UIKit.h>

#import "gfx.h"
#import "gfx_metal.h"

// Phase-2 frame: clear to the app's dark navy so the screenshot proves Metal ran.
static void ios_tick(void) {
    gfx_begin_frame();
    gfx_clear((Color){18, 20, 30, 255});
    gfx_end_frame();
}

@interface OBMetalView : UIView
@end

@implementation OBMetalView

+ (Class)layerClass { return [CAMetalLayer class]; }

- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (!self.window) return;
    gfx_metal_attach((CAMetalLayer*)self.layer);
    CADisplayLink* link = [CADisplayLink displayLinkWithTarget:self selector:@selector(onFrame:)];
    link.preferredFramesPerSecond = 60;
    [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    [self updateDrawableSize];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self updateDrawableSize];
}

- (void)updateDrawableSize {
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    CGFloat scale = self.window ? self.window.screen.scale : [UIScreen mainScreen].scale;
    layer.contentsScale = scale;
    CGSize px = CGSizeMake(self.bounds.size.width * scale, self.bounds.size.height * scale);
    layer.drawableSize = px;
    gfx_metal_resize((int)px.width, (int)px.height);
}

- (void)onFrame:(CADisplayLink*)link { (void)link; ios_tick(); }

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
    return YES;
}

@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
