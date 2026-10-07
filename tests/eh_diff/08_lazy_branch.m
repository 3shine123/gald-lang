// ObjC 对照:08_lazy_branch(语义基准)
#import <Foundation/Foundation.h>

@interface Bumper : NSObject {
@public
    int _side;
}
- (int)bump;
- (int)side;
@end

@implementation Bumper
- (int)bump { _side++; fprintf(stderr, "bump executed\n"); return 42; }
- (int)side { return _side; }
@end

int main(void) {
    @autoreleasepool {
        Bumper *b = [[Bumper alloc] init];
        int on = 1;
        int off = 0;

        int v1 = on ? 7 : [b bump];
        fprintf(stderr, "ternary_then v=%d side=%d\n", v1, [b side]);

        int v2 = off ? [b bump] : 9;
        fprintf(stderr, "ternary_else v=%d side=%d\n", v2, [b side]);

        int v3 = (off && ([b bump] != 0));
        fprintf(stderr, "and_short v=%d side=%d\n", v3, [b side]);
    }
    return 0;
}
