#!/usr/bin/env python3
import unittest
from unittest.mock import Mock


class IntepreterTest(unittest.TestCase):
    def test_compare_op_in_propagetes_exception(self):
        class C:
            def __contains__(self, value):
                raise UserWarning("C.__contains__")

        c = C()
        with self.assertRaises(UserWarning) as context:
            1 in c
        self.assertEqual(str(context.exception), "C.__contains__")

    def test_compare_op_not_in_propagetes_exception(self):
        class C:
            def __contains__(self, value):
                raise UserWarning("C.__contains__")

        c = C()
        with self.assertRaises(UserWarning) as context:
            1 not in c
        self.assertEqual(str(context.exception), "C.__contains__")

    def test_store_name_calls_dunder_setitem(self):
        class C(dict):
            def __setitem__(self, key, value):
                self.result = (key, value)

        locals = C()
        exec("x = 44", None, locals)
        self.assertEqual(locals.result[0], "x")
        self.assertEqual(locals.result[1], 44)

    def test_store_name_setitem_propagates_exception(self):
        class C:
            def __setitem__(self, key, value):
                raise UserWarning("bar")

            def __getitem__(self, key):
                ...  # just here so this type is considered a mapping.

        locals = C()
        with self.assertRaises(UserWarning) as context:
            exec("x = 1", None, locals)
        self.assertEqual(str(context.exception), "bar")

    def test_store_name_setitem_propagates_descriptor_exception(self):
        class D:
            def __get__(self, instance, owner):
                self.result = (instance, owner)
                raise UserWarning("baz")

        d = D()

        class C:
            __setitem__ = d

            def __getitem__(self, key):
                ...  # just here so this type is considered a mapping.

        locals = C()
        with self.assertRaises(UserWarning) as context:
            exec("x = 1", None, locals)
        self.assertEqual(str(context.exception), "baz")
        self.assertEqual(d.result[0], locals)
        self.assertEqual(d.result[1], C)

    def test_load_name_calls_dunder_getitem(self):
        class C:
            def __getitem__(self, key):
                return (key, 17)

        # TODO(T54956257): Use regular dict for globals.
        from types import ModuleType

        module = ModuleType("test_module")
        globals = module.__dict__
        globals["foo"] = None
        locals = C()
        exec('assert(foo == ("foo", 17))', globals, locals)

    def test_load_name_continues_on_keyerror(self):
        class C:
            def __getitem__(self, key):
                self.result = key
                raise KeyError()

        # TODO(T54956257): Use regular dict for globals.
        from types import ModuleType

        module = ModuleType("test_module")
        globals = module.__dict__
        exec("foo = 99", globals)
        self.assertIn("foo", globals)

        locals = C()
        exec("assert(foo == 99)", globals, locals)  # should not raise
        self.assertEqual(locals.result, "foo")

    def test_load_name_getitem_propagates_exception(self):
        class C:
            def __getitem__(self, key):
                raise UserWarning("bar")

        globals = None
        locals = C()
        with self.assertRaises(UserWarning) as context:
            exec("foo", globals, locals)
        self.assertEqual(str(context.exception), "bar")

    def test_load_name_getitem_propagates_descriptor_exception(self):
        class D:
            def __get__(self, instance, owner):
                self.result = (instance, owner)
                raise UserWarning("baz")

        d = D()

        class C:
            __getitem__ = d

        globals = None
        locals = C()
        with self.assertRaises(UserWarning) as context:
            exec("foo", globals, locals)
        self.assertEqual(str(context.exception), "baz")
        self.assertEqual(d.result[0], locals)
        self.assertEqual(d.result[1], C)

    def test_delete_name_calls_dunder_delitem(self):
        class C(dict):
            def __delitem__(self, key):
                self.result = key
                return None

        locals = C()
        exec("del foo", None, locals)
        self.assertEqual(locals.result, "foo")

    def test_delete_name_raises_name_error_on_error(self):
        class C(dict):
            def __delitem__(self, key):
                self.result = key
                raise UserWarning("bar")

        # TODO(T54956257): Use regular dict for globals.
        from types import ModuleType

        module = ModuleType("test_module")
        globals = module.__dict__
        locals = C()
        with self.assertRaises(NameError) as context:
            exec("del foo", globals, locals)
        self.assertEqual(str(context.exception), "name 'foo' is not defined")
        self.assertEqual(locals.result, "foo")

    def test_delete_name_raises_name_error_on_descriptor_error(self):
        class D:
            def __get__(self, instance, owner):
                self.result = (instance, owner)
                raise UserWarning("baz")

        d = D()

        class C(dict):
            __delitem__ = d

        # TODO(T54956257): Use regular dict for globals.
        from types import ModuleType

        module = ModuleType("test_module")

        globals = module.__dict__
        locals = C()
        with self.assertRaises(NameError) as context:
            exec("del foo", globals, locals)
        self.assertEqual(str(context.exception), "name 'foo' is not defined")
        self.assertEqual(d.result[0], locals)
        self.assertEqual(d.result[1], C)

    def test_get_iter_with_range_with_bool(self):
        result = None
        for i in range(True):
            if result is None:
                result = i
            else:
                result += i
        self.assertEqual(result, 0)

        result = 0
        for i in range(True, 4):
            result += i
        self.assertEqual(result, 6)

        result = 0
        for i in range(0, 4, True):
            result += i
        self.assertEqual(result, 6)

    def test_import_performs_secondary_lookup(self):
        import sys

        class FakeModule:
            def __getattribute__(self, name):
                if name == "__name__":
                    return "test_import_performs_secondary_lookup_fake_too"
                raise AttributeError(name)

        sys.modules["test_import_performs_secondary_lookup_fake"] = FakeModule()
        sys.modules["test_import_performs_secondary_lookup_fake_too.foo"] = 42
        try:
            from test_import_performs_secondary_lookup_fake import foo
        finally:
            del sys.modules["test_import_performs_secondary_lookup_fake"]
            del sys.modules["test_import_performs_secondary_lookup_fake_too.foo"]
        self.assertEqual(foo, 42)

    def test_call_ex_kw_with_class_with_iterable_raises_type_error(self):
        def foo(a):
            return a

        mapping = [("hello", "world")]
        with self.assertRaisesRegex(TypeError, "must be a mapping, not list"):
            foo(**mapping)

    def test_call_ex_kw_with_class_without_keys_method_raises_type_error(self):
        class C:
            __getitem__ = Mock(name="__getitem__", return_value="mock")

        def foo(a):
            return a

        mapping = C()
        with self.assertRaisesRegex(TypeError, "must be a mapping, not C"):
            foo(**mapping)

    def test_call_ex_kw_with_dict_subclass_does_not_call_keys_or_dunder_getitem(self):
        class C(dict):
            __getitem__ = Mock(name="__getitem__", return_value="mock")
            keys = Mock(name="keys", return_value=("a",))

        def foo(a):
            return a

        mapping = C({"a": "foo"})
        result = foo(**mapping)
        self.assertEqual(result, "foo")
        C.keys.assert_not_called()
        C.__getitem__.assert_not_called()

    def test_call_ex_kw_with_non_dict_calls_keys_and_dunder_getitem(self):
        class C:
            __getitem__ = Mock(name="__getitem__", return_value="mock")
            keys = Mock(name="keys", return_value=("a",))

        def foo(a):
            return a

        mapping = C()
        result = foo(**mapping)
        self.assertEqual(result, "mock")
        C.keys.assert_called_once()
        C.__getitem__.assert_called_once()

    def test_call_ex_kw_with_non_dict_passes_dict_as_kwargs(self):
        class C:
            __getitem__ = Mock(name="__getitem__", return_value="mock")
            keys = Mock(name="keys", return_value=("hello", "world"))

        def foo(**kwargs):
            self.assertIs(type(kwargs), dict)
            self.assertIn("hello", kwargs)
            self.assertIn("world", kwargs)

        mapping = C()
        foo(**mapping)
        C.keys.assert_called_once()
        self.assertEqual(C.__getitem__.call_count, 2)


if __name__ == "__main__":
    unittest.main()
