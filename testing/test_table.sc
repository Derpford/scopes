
spice tset (target key value)
    if (not ('constant? value))
        error "value must be constant"
    sc_table_set_symbol target (key as Symbol) value

spice tget (target key)
    sc_table_at target (key as Symbol)

run-stage;

local k = 1

tset k 'test 1
if true
    tset k 'test 2
    dump (tget k 'test)
else
    tset k 'test 3
    dump (tget k 'test)

dump (tget k 'test)


;