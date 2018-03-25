
fn assert-reference (self)
    assert ((typeof self) < reference) "array must be reference"

fn gen-element-destructor (element-type)
    let element-destructor = (type@ element-type 'delete)
    if (none? element-destructor)
        fn "MutableArray-destructor" (self items)
    else
        fn "MutableArray-destructor" (self items)
            let count =
                self.count as immutable
            let loop (i) = (unconst 0:usize)
            if (i < count)
                let ptr = (items @ i)
                element-destructor ptr
                loop (i + 1:usize)

fn define-common-array-methods (T element-type ensure-capacity)
    let destructor = (gen-element-destructor element-type)

    typefn T 'delete (self)
        assert-reference self
        let items = (load self.items)
        destructor self items
        free items

    typefn T 'as& (self T)
        if (T == Generator)
            Generator
                label (fret fdone i)
                    if (i >= self.count)
                        fdone;
                    else
                        fret (i + 1:usize) (self @ i)
                unconst 0:usize
        else
            return;

    typefn T 'countof (self)
        self.count

    typefn T 'countof& (self)
        self.count as immutable

    typefn T '@& (self index)
        let index = (index as usize)
        assert ((index < self.count) & (index >= 0:usize)) "index out of bounds"
        reference.from-pointer
            getelementptr (load self.items) index

    fn append-slot (self)
        assert-reference self
        let count = self.count
        ensure-capacity self count
        let idx = (count as immutable)
        count = count + 1
        return
            reference.from-pointer
                getelementptr (load self.items) idx

    typefn T 'emplace-append (self args...)
        let dest = (append-slot self)
        store
            element-type args...
            dest
        dest

    typefn T 'append (self value)
        let dest = (append-slot self)
        store
            value as immutable
            dest
        dest

    return;


fn FixedMutableArray (element-type capacity)
    let arrayT =
        pointer element-type 'mutable

    struct
        .. "<MutableArray "
            string-repr element-type
            " x "
            string-repr (i32 capacity)
            ">"
        count : usize
        items : arrayT

        define-common-array-methods this-struct element-type
            fn "ensure-capacity" (self count)
                assert (count < capacity) "capacity exceeded"

        method 'repr (self)
            ..
                "[count="
                repr self.count
                " items="
                repr self.items
                "]"

        method 'apply-type (cls)
            CStruct.apply-type cls
                count = 0:usize
                items = (malloc-array element-type capacity)

let DEFAULT_CAPACITY = (1:usize << 4:usize)
fn VariableMutableArray (element-type)
    let arrayT =
        pointer element-type 'mutable

    fn grow (self)
        let new-capacity =
            self.capacity << 1:usize
        let count =
            self.count as immutable
        let old-items = (load self.items)
        let new-items =
            malloc-array element-type new-capacity
        let loop (i) = (unconst 0:usize)
        if (i < count)
            let sptr = (getelementptr old-items i)
            let dptr = (getelementptr new-items i)
            store (load sptr) dptr
            loop (i + 1:usize)
        self.items = new-items
        self.capacity = new-capacity
        free old-items
        return;

    struct
        .. "<MutableArray "
            string-repr element-type
            ">"
        capacity : usize
        count : usize
        items : arrayT

        define-common-array-methods this-struct element-type
            fn "ensure-capacity" (self count)
                if (count == self.capacity)
                    grow self

        method 'repr (self)
            ..
                "[count="
                repr self.count
                " capacity="
                repr self.capacity
                " items="
                repr self.items
                "]"

        method 'apply-type (cls capacity)
            let capacity =
                if (none? capacity) DEFAULT_CAPACITY
                else capacity
            CStruct.apply-type cls
                capacity = capacity
                count = 0:usize
                items = (malloc-array element-type capacity)

fn MutableArray (element-type capacity)
    """"Construct a mutable array type of ``element-type`` with a variable or
        fixed maximum capacity.

        If ``capacity`` is defined, then it specifies the maximum number
        of array elements permitted. If it is undefined, then an initial
        capacity of 16 elements is assumed, which is doubled whenever
        it is exceeded, allowing for an indefinite number of elements.
    if (none? capacity)
        VariableMutableArray element-type
    else
        assert (constant? capacity) "capacity must be constant"
        FixedMutableArray element-type (capacity as usize)
