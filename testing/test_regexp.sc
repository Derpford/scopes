
let k i0 i1 = ('match? ("^(t.st)?(t.st)?(t.st)?$" as GlobalString) "tisttosttust")
assert k
assert (i0 == 0)
assert (i1 == 12)
let k = ('match? ("^(t.st)?(t.st)?(t.st)?$" as GlobalString) "tisttozt")
assert (not k)

let s = ("^(gl(.+)|GL(.+))$" as GlobalString)
loop (i = 0)
    if (i < 20000)
        assert ('match? s "GL_UNIFORM_BUFFER_EXT")
        assert ('match? s "GL_CURRENT_MATRIX_INDEX_ARB")
        assert ('match? s "GL_DOT4_ATI")
        repeat (i + 1)
    break;

