
using import Capture

do
    # immutable capture

    let a = 10
    let b = 20

    @@ report
    capture cf (u v) {a b}
        print a b u v
        + a b u v

    fn testf (f)
        assert ((f 1 2) == (+ 10 20 1 2))

    testf cf

let T = (array.type i32 16)
spice-capture f () {T}
    dump T
    `(nullof T)

run-stage;

print (f)

# conditional capture generated by function
fn make (s a b)
    if s
        'instance
            capture "cf1" (u v) {a b}
                print a b u v
                + a b u v
            \ i32 i32
    else
        'instance
            capture "cf2" (u v) {a b}
                print a b u v
                - (+ a b u v)
            \ i32 i32

let cf1 = (make true 10 20)
assert ((cf1 1 2) == (+ 10 20 1 2))
let cf2 = (make false 10 20)
assert ((cf2 1 2) == (- (+ 10 20 1 2)))

# callback arrays
using import Array

local callbacks : (GrowingArray (Capture (function void voidstar)))

let a = "1!"
let b = "2!"
'append callbacks
    capture () {a}
        print a
        ;
'append callbacks
    capture () {b}
        print b
        ;

for cb in callbacks
    cb;


;