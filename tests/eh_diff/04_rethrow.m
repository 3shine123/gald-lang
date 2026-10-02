// ObjC 对照:04_rethrow(语义基准)
#import <Foundation/Foundation.h>

@interface Err : NSObject
@end
@implementation Err
@end

int main(void) {
    @autoreleasepool {
        @try {
            @try {
                @throw [[Err alloc] init];
            }
            @catch (Err *e) {
                fprintf(stderr, "inner caught, rethrow\n");
                @throw e;
            }
        }
        @catch (Err *e2) { fprintf(stderr, "outer caught\n"); }
        fprintf(stderr, "end\n");
    }
    return 0;
}
