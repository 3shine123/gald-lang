// ObjC 对照:02_stmt_interrupt(语义基准)
#import <Foundation/Foundation.h>

@interface T : NSObject
@end
@implementation T
+ (int)boom { fprintf(stderr, "about to throw\n"); @throw @"err"; return 0; }
+ (int)bar  { fprintf(stderr, "bar executed\n"); return 2; }
@end

int main(void) {
    @autoreleasepool {
        int x = 0;
        @try {
            x = [T boom] + [T bar];
        }
        @catch (NSString *e) { fprintf(stderr, "caught\n"); }
        fprintf(stderr, "x=%d\n", x);
    }
    return 0;
}
