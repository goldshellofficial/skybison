#!/usr/bin/env python3
"""Functional tools for creating and using iterators."""
import operator


# TODO(T42113424) Replace stubs with an actual implementation
_Unbound = _Unbound  # noqa: F821
_int_guard = _int_guard  # noqa: F821
_list_len = _list_len  # noqa: F821
_tuple_len = _tuple_len  # noqa: F821
_unimplemented = _unimplemented  # noqa: F821


class accumulate:
    def __new__(cls, iterable, func=None):
        result = object.__new__(cls)
        result._it = iter(iterable)
        result._func = operator.add if func is None else func
        result._accumulated = None
        return result

    def __iter__(self):
        return self

    def __next__(self):
        result = self._accumulated

        if result is None:
            result = next(self._it)
            self._accumulated = result
            return result

        result = self._func(result, next(self._it))
        self._accumulated = result
        return result


class chain:
    def __iter__(self):
        return self

    def __new__(cls, *iterables):
        result = object.__new__(cls)
        result._it = None
        result._iterables = iter(iterables)
        return result

    def __next__(self):
        while True:
            if self._it is None:
                try:
                    self._it = iter(next(self._iterables))
                except StopIteration:
                    raise
            try:
                result = next(self._it)
            except StopIteration:
                self._it = None
                continue
            return result

    @classmethod
    def from_iterable(cls, iterable):
        result = object.__new__(cls)
        result._it = None
        result._iterables = iter(iterable)
        return result


class combinations:
    def __init__(self, p, r):
        _unimplemented()


class combinations_with_replacement:
    def __init__(self, p, r):
        _unimplemented()


class compress:
    def __init__(self, data, selectors):
        _unimplemented()


class count:
    def __init__(self, start=0, step=1):
        start_type = type(start)
        if not hasattr(start_type, "__add__") or not hasattr(start_type, "__sub__"):
            raise TypeError("a number is required")
        self.count = start
        self.step = step

    def __iter__(self):
        return self

    def __next__(self):
        result = self.count
        self.count += self.step
        return result

    def __repr__(self):
        return f"count({self.count})"


class cycle:
    def __init__(self, seq):
        self._seq = iter(seq)
        self._saved = []
        self._first_pass = True

    def __iter__(self):
        return self

    def __next__(self):
        try:
            result = next(self._seq)
            if self._first_pass:
                self._saved.append(result)
            return result
        except StopIteration:
            self._first_pass = False
            self._seq = iter(self._saved)
            return next(self._seq)

    def __reduce__(self):
        _unimplemented()

    def __setstate__(self):
        _unimplemented()


class dropwhile:
    def __iter__(self):
        return self

    def __new__(cls, predicate, iterable):
        result = object.__new__(cls)
        result._it = iter(iterable)
        result._func = predicate
        result._start = False
        return result

    def __next__(self):
        if self._start:
            return next(self._it)

        func = self._func

        while True:
            item = next(self._it)
            if not func(item):
                self._start = True
                return item


class filterfalse:
    def __new__(cls, predicate, iterable):
        result = object.__new__(cls)
        result._it = iter(iterable)
        result._predicate = bool if predicate is None else predicate
        return result

    def __iter__(self):
        return self

    def __next__(self):
        while True:
            item = next(self._it)
            if not self._predicate(item):
                return item


# internal helper class for groupby
class _groupby_iterator:
    def __init__(self, parent, key):
        self._parent = parent
        self._key = key

    def __iter__(self):
        return self

    def __next__(self):
        parent = self._parent

        if self._key != parent._currkey:
            raise StopIteration

        r = parent._currvalue
        try:
            parent._groupby_step()
        except StopIteration:
            # sentinel value will raise StopIteration on next iteration
            parent._currkey = _Unbound
        return r


class groupby:
    def __new__(cls, iterable, key=None):
        result = object.__new__(cls)
        result._it = iter(iterable)
        result._lastkey = _Unbound
        result._currkey = _Unbound
        result._currvalue = _Unbound
        result._keyfunc = key
        return result

    def __iter__(self):
        return self

    def __next__(self):
        while self._currkey == self._lastkey:
            self._groupby_step()

        self._lastkey = self._currkey
        grouper = _groupby_iterator(self, self._currkey)
        return (self._currkey, grouper)

    def _groupby_step(self):
        newvalue = next(self._it)
        newkey = newvalue if self._keyfunc is None else self._keyfunc(newvalue)
        self._currkey = newkey
        self._currvalue = newvalue


class islice:
    def __init__(self, seq, *args):
        _unimplemented()


class permutations:
    def __init__(self, iterable, r=None):
        iterable = tuple(iterable)
        n = _tuple_len(iterable)
        if r is None:
            r = n
        elif r > n:
            self._iterable = self._r = self._indices = self._cycles = None
            return
        self._iterable = iterable
        self._r = r
        self._indices = list(range(n))
        self._cycles = list(range(n, n - r, -1))

    def __iter__(self):
        return self

    def __next__(self):
        iterable = self._iterable
        if iterable is None:
            raise StopIteration
        r = self._r
        indices = self._indices
        indices_len = _list_len(indices)
        result = (*(iterable[indices[i]] for i in range(r)),)
        cycles = self._cycles
        i = r - 1
        while i >= 0:
            j = cycles[i] - 1
            if j > 0:
                cycles[i] = j
                indices[i], indices[-j] = indices[-j], indices[i]
                break
            cycles[i] = indices_len - i
            tmp = indices[i]
            k = i + 1
            while k < indices_len:
                indices[k - 1] = indices[k]
                k += 1
            indices[k - 1] = tmp
            i -= 1
        else:
            self._iterable = self._r = self._indices = self._cycles = None
        return result


class product:
    def __init__(self, *iterables, repeat=1):
        if not isinstance(repeat, int):
            raise TypeError
        length = _tuple_len(iterables) if repeat else 0
        i = 0
        repeated = [None] * length
        while i < length:
            item = tuple(iterables[i])
            if not item:
                self._iterables = self._digits = None
                return
            repeated[i] = item
            i += 1
        repeated *= repeat
        self._iterables = repeated
        self._digits = [0] * (length * repeat)

    def __iter__(self):
        return self

    def __next__(self):
        iterables = self._iterables
        if iterables is None:
            raise StopIteration
        digits = self._digits
        length = _list_len(iterables)
        result = [None] * length
        i = length - 1
        carry = 1
        while i >= 0:
            j = digits[i]
            result[i] = iterables[i][j]
            j += carry
            if j < _tuple_len(iterables[i]):
                carry = 0
                digits[i] = j
            else:
                carry = 1
                digits[i] = 0
            i -= 1
        if carry:
            # counter overflowed, stop iteration
            self._iterables = self._digits = None
        return tuple(result)


class repeat:
    def __init__(self, elem, times=None):
        self._elem = elem
        if times is not None:
            _int_guard(times)
        self._times = times

    def __iter__(self):
        return self

    def __next__(self):
        if self._times is None:
            return self._elem
        if self._times > 0:
            self._times -= 1
            return self._elem
        raise StopIteration


class starmap:
    def __init__(self, fun, seq):
        _unimplemented()


def tee(iterable, n=2):
    _int_guard(n)
    if n < 0:
        raise ValueError("n must be >= 0")
    if n == 0:
        return ()

    it = iter(iterable)
    copyable = it if hasattr(it, "__copy__") else _tee.from_iterable(it)
    copyfunc = copyable.__copy__
    return tuple(copyable if i == 0 else copyfunc() for i in range(n))


# Internal cache for tee, a linked list where each link is a cached window to
# a section of the source iterator
class _tee_dataobject:
    # CPython sets this at 57 to align exactly with cache line size. We choose
    # 55 to align with cache lines in our system: Arrays <=255 elements have 1
    # word of header. The header and each data element is 8 bytes on a 64-bit
    # machine.  Cache lines are 64-bytes on all x86 machines though they tend to
    # be fetched in pairs, so any multiple of 8 minus 1 up to 255 is fine.
    _MAX_VALUES = 55

    def __init__(self, it):
        self._num_read = 0
        self._next_link = _Unbound
        self._it = it
        self._values = []

    def get_item(self, i):
        assert i < self.__class__._MAX_VALUES

        if i < self._num_read:
            return self._values[i]
        else:
            assert i == self._num_read
            value = next(self._it)
            self._num_read += 1
            # mutable tuple might be a nice future optimization here
            self._values.append(value)
            return value

    def next_link(self):
        if self._next_link is _Unbound:
            self._next_link = self.__class__(self._it)
        return self._next_link


class _tee:
    def __copy__(self):
        return self.__class__(self._data, self._index)

    def __init__(self, data, index):
        self._data = data
        self._index = index

    def __iter__(self):
        return self

    def __next__(self):
        if self._index >= _tee_dataobject._MAX_VALUES:
            self._data = self._data.next_link()
            self._index = 0

        value = self._data.get_item(self._index)
        self._index += 1
        return value

    @classmethod
    def from_iterable(cls, iterable):
        it = iter(iterable)

        if isinstance(it, _tee):
            return it.__copy__()
        else:
            return cls(_tee_dataobject(it), 0)


class takewhile:
    def __iter__(self):
        return self

    def __new__(cls, predicate, iterable):
        result = object.__new__(cls)
        result._it = iter(iterable)
        result._func = predicate
        result._stop = False
        return result

    def __next__(self):
        if self._stop:
            raise StopIteration

        item = next(self._it)
        if self._func(item):
            return item

        self._stop = True
        raise StopIteration


class zip_longest:
    def __init__(self, *seqs, fillvalue=None):
        length = _tuple_len(seqs)
        self._iters = [iter(seq) for seq in seqs]
        self._num_iters = length
        self._num_active = length
        self._fillvalue = fillvalue

    def __iter__(self):
        return self

    def __next__(self):
        iters = self._iters
        if not self._num_active:
            raise StopIteration
        fillvalue = self._fillvalue
        values = [fillvalue] * self._num_iters
        for i, it in enumerate(iters):
            try:
                values[i] = next(it)
            except StopIteration:
                self._num_active -= 1
                if not self._num_active:
                    raise
                self._iters[i] = repeat(fillvalue)
        return tuple(values)
