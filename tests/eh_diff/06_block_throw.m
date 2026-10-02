// ObjC 对照:06_block_throw(语义基准)
#import <Foundation/Foundation.h>

typedef void (^ThrowerBlock)(void);

int main(void) {
    @autoreleasepool {
        @try {
            ThrowerBlock b = ^{
                fprintf(stderr, "in block\n");
                @throw @"from-block";
            };
            b();
        }
        @catch (NSString *e) { fprintf(stderr, "caught from block\n"); }
        fprintf(stderr, "end\n");
    }
    return 0;
}
