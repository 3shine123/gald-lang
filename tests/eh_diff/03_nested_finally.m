// ObjC 对照:03_nested_finally(语义基准)
#import <Foundation/Foundation.h>

int main(void) {
    @autoreleasepool {
        @try {
            @try {
                fprintf(stderr, "inner try\n");
                @throw @"inner-err";
            }
            @finally { fprintf(stderr, "inner finally\n"); }
        }
        @catch (NSString *e) { fprintf(stderr, "outer caught\n"); }
        @finally { fprintf(stderr, "outer finally\n"); }
        fprintf(stderr, "end\n");
    }
    return 0;
}
