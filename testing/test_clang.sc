
let
    TESTVAL = "-DTESTVAL2"

include
    filter "^(t.*|.*VAL)$"
    options "-DTESTVAL" TESTVAL
""""#ifndef TESTVAL
        #error "expected define"
    #endif
    #ifndef TESTVAL2
        #error "expected define 2"
    #endif
    int testfunc (int x, int y) {
        return x * y;
    }

    #define DOUBLEVAL 1.0
    #define FLOATVAL 1.0f
    #define INTVAL 3
    #define UINTVAL 3u
    #define LONGVAL 3ll
    #define ULONGVAL 0x3ull
    // eof

test ((testfunc 2 3) == 6)

static-test ((returnof testfunc) == i32)

static-assert ((typeof DOUBLEVAL) == f64)
static-assert ((typeof FLOATVAL) == f32)
static-assert ((typeof INTVAL) == i32)
static-assert ((typeof UINTVAL) == u32)
static-assert ((typeof LONGVAL) == i64)
static-assert ((typeof ULONGVAL) == u64)
