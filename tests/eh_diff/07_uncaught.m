// ObjC 对照:07_uncaught(语义基准)
#import <Foundation/Foundation.h>

int main(void) {
    @autoreleasepool {
        fprintf(stderr, "before throw\n");
        @throw @"uncaught";
    }
    return 0;
}
