
do
    # immutable capture

    let a = (unconst 10)
    let b = (unconst 20)
    let f =
        capture [a b] (u v)
            print a b u v
            + a b u v

    dump f
    assert ((f 1 2) == (+ 10 20 1 2))


do
    # mutable capture (allocated on stack by default)

    let a = (unconst 10)
    let b = (unconst 20)
    let f =
        capture& [a b] (u v)
            print a b u v
            + a b u v

    dump f
    assert ((f 1 2) == (+ 10 20 1 2))

