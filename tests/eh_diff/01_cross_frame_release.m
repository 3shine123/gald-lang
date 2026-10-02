// ObjC 对照:01_cross_frame_release(语义基准)
#import <Foundation/Foundation.h>

@interface Item : NSObject
@end
@implementation Item
- (void)dealloc { fprintf(stderr, "Item dealloc\n"); }
@end

@interface Thrower : NSObject
@end
@implementation Thrower
+ (void)boom {
    @throw [[Item alloc] init];
}
+ (void)middle {
    Item *m = [[Item alloc] init];
    fprintf(stderr, "middle created m\n");
    [Thrower boom];
}
@end

int main(void) {
    @autoreleasepool {
        @try { [Thrower middle]; } @catch (Item *e) { fprintf(stderr, "caught\n"); }
        fprintf(stderr, "--- end ---\n");
    }
    return 0;
}
