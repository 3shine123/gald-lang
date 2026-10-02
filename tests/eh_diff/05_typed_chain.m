// ObjC 对照:05_typed_chain(语义基准)
#import <Foundation/Foundation.h>

@interface A : NSObject
@end
@implementation A
@end

@interface B : NSObject
@end
@implementation B
@end

int main(void) {
    @autoreleasepool {
        @try {
            @try {
                @throw [[B alloc] init];
            }
            @catch (A *a) { fprintf(stderr, "caught A (wrong)\n"); }
        }
        @catch (B *b) { fprintf(stderr, "caught B\n"); }
        fprintf(stderr, "end\n");
    }
    return 0;
}
