
#let x = (unconst true)
#global y = x

let C =
    import-c "libc.c"
        """"
            #include <stdio.h>
        list;

let scope = (Scope)

run-stage;

fn static-array-init ()
    private (array i32 10:usize)

compile
    typify static-array-init
    'dump-module
    'no-debug-info

global x : (array i32 10:usize)
global y : i32

fn main (argc argv)
    x @ 3 = 6
    y = 7
    C.printf ("hello world %i %i\n" as rawstring)
        load (reftoptr (x @ 3))
        load (reftoptr y)
    return 0

let main = (typify main i32 ('pointer rawstring))
#compile main
    'dump-module
    'no-debug-info

'define-symbol scope 'main main

compile-object
    module-dir .. "/test.o"
    scope
    'no-debug-info
    'dump-module

# execute with
    scopes test_object.sc && gcc -o test[.exe] test.o && ./test

    output will be

    hello world 6 7
