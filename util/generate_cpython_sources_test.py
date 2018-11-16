#!/usr/bin/env python3
import unittest
import generate_cpython_sources as gcs


class TestSymbolRegex(unittest.TestCase):
    def test_typedef_regex_returns_multiple_symbols(self):
        lines = """
typedef type1 Foo; // Comment

typedef void (*Bar)(void *);

typedef struct newtype {
  int foo_bar;
} Foo_Bar;

typedef foo_bar Baz;
"""
        res = gcs.find_symbols_in_file(gcs.SYMBOL_REGEX, lines)["typedef"]
        self.assertListEqual(res, ["Foo", "Bar", "Baz"])

    def test_multiline_typedef_regex_returns_multiple_symbols(self):
        lines = """
typedef int Baz;

typedef struct {
  Foo *foo1;
  Foo *foo2; /* Comment */
} Foo;

typedef struct {
  int bar;
} Bar;

struct FooBarBaz {
  Foobar* foobar;
};
"""
        res = gcs.find_symbols_in_file(gcs.SYMBOL_REGEX, lines)[
            "multiline_typedef"
        ]
        self.assertListEqual(res, ["Foo", "Bar"])

    def test_struct_regex_returns_multiple_symbols(self):
        lines = """
struct Foo {
  Baz baz1; /* Comment */
  Baz baz2;
};

typedef int FooBar;

#define FooBaz 1,

struct Bar {
  Baz baz;
};
"""
        res = gcs.find_symbols_in_file(gcs.SYMBOL_REGEX, lines)["struct"]
        self.assertListEqual(res, ["Foo", "Bar"])

    def test_macro_regex_returns_multiple_symbols(self):
        lines = """
#define Foo 0, // Comment

typedef int FooBar;

#define Bar(o) Foo;

#define FooBaz(o)       \\
    { Baz(type) },
"""
        res = gcs.find_symbols_in_file(gcs.SYMBOL_REGEX, lines)["macro"]
        self.assertListEqual(res, ["Foo", "Bar"])

    def test_multiline_macro_regex_returns_multiple_symbols(self):
        lines = """
#define Foo       \\
    { Baz(1) },

typedef int FooBar;

#define FooBaz(o) Foo,

#define Bar(op)     \\
    do {            \\
        int a = 1;  \\
        if (a == 1) \\
          Foo       \\
        else        \\
          Baz(a)    \\
    } while (0)
"""
        res = gcs.find_symbols_in_file(gcs.SYMBOL_REGEX, lines)[
            "multiline_macro"
        ]
        self.assertListEqual(res, ["Foo", "Bar"])

    def test_pytypeobject_regex_returns_multiple_symbols(self):
        lines = """
extern "C" PyTypeObject* Foo_Type_Ptr() {
  Thread* thread = Thread::currentThread();
}

extern "C" PyObject* Foo_Function(void) {
  // Some implementation
}

extern "C" PyTypeObject *Bar_Type_Ptr() {
  Thread* thread = Thread::currentThread();
}
"""
        res = gcs.find_symbols_in_file(gcs.SYMBOL_REGEX, lines)["pytypeobject"]
        self.assertListEqual(res, ["Foo_Type", "Bar_Type"])

    def test_pytypeobject_macro_regex_returns_multiple_symbols(self):
        lines = """
#define Foo_Type (*Foo_Type_Ptr())

#define Foo       \\
    { Baz(type) },

typedef int FooBar;

#define Bar_Type (*Bar_Type_Ptr())

#define FooBaz(o) Foo,
"""
        res = gcs.find_symbols_in_file(gcs.SYMBOL_REGEX, lines)[
            "pytypeobject_macro"
        ]
        self.assertEqual(res, ["Foo_Type", "Bar_Type"])


class TestDefinitionRegex(unittest.TestCase):
    def test_multiple_typedef_definitions_are_replaced(self):
        original_lines = """
typedef type1 Foo; // Comment

typedef void (*Bar)(void *);

typedef struct newtype {
  int foo_bar;
} Foo_Bar;

typedef foo_bar Baz;
"""
        expected_lines = """


typedef struct newtype {
  int foo_bar;
} Foo_Bar;

"""
        symbols_to_replace = {"typedef": ["Foo", "Bar", "Baz"]}
        res = gcs.modify_file(original_lines, symbols_to_replace)
        self.assertEqual(res, expected_lines)

    def test_multiple_multiline_typedef_definitions_are_replaced(self):
        original_lines = """
typedef int Baz;

typedef struct {
  Foo *foo1;
  Foo *foo2; /* Comment */
} Foo;

typedef struct {
  int bar;
} Bar;

struct FooBarBaz {
  Foobar* foobar;
};
"""
        expected_lines = """
typedef int Baz;



struct FooBarBaz {
  Foobar* foobar;
};
"""
        symbols_to_replace = {"multiline_typedef": ["Foo", "Bar"]}
        res = gcs.modify_file(original_lines, symbols_to_replace)
        self.assertEqual(res, expected_lines)

    def test_multiple_struct_definitions_are_replaced(self):
        original_lines = """
struct Foo {
  Baz baz1; /* Comment */
  Baz baz2;
};

typedef int FooBar;

#define FooBaz 1,

struct Bar {
  Baz baz;
};
"""
        expected_lines = """

typedef int FooBar;

#define FooBaz 1,

"""
        symbols_to_replace = {"struct": ["Foo", "Bar"]}
        res = gcs.modify_file(original_lines, symbols_to_replace)
        self.assertEqual(res, expected_lines)

    def test_multiple_macro_definitions_are_replaced(self):
        original_lines = """
#define Foo 0, // Comment

typedef int FooBar;

#define Bar(o) Foo;

#define FooBaz(o)       \\
    { Baz(type) },
"""
        expected_lines = """
typedef int FooBar;

#define FooBaz(o)       \\
    { Baz(type) },
"""
        symbols_to_replace = {"macro": ["Foo", "Bar"]}
        res = gcs.modify_file(original_lines, symbols_to_replace)
        self.assertEqual(res, expected_lines)

    def test_multiline_macro_definitions_are_replaced(self):
        original_lines = """
#define Foo       \\
    { Baz(1) },

typedef int FooBar;

#define FooBaz(o) Foo,

#define Bar(op)     \\
    do {            \\
        int a = 1;  \\
        if (a == 1) \\
          Foo       \\
        else        \\
          Baz(a)    \\
    } while (0)
"""
        expected_lines = """

typedef int FooBar;

#define FooBaz(o) Foo,

"""
        symbols_to_replace = {"multiline_macro": ["Foo", "Bar"]}
        res = gcs.modify_file(original_lines, symbols_to_replace)
        self.assertEqual(res, expected_lines)

    def test_pytypeobject_definitions_are_replaced(self):
        original_lines = """
PyObject *Foo_Function(void)
{
  // Some implementation
};

PyTypeObject Foo_Type = {
  // Some implementation
};

PyObject *Bar_Function(void)
{
  // Some implementation
};

PyTypeObject Bar_Type = {
  // Some implementation
};

PyTypeObject Baz_Type = {
  // Some implementation
};
"""
        expected_lines = """
PyObject *Foo_Function(void)
{
  // Some implementation
};


PyObject *Bar_Function(void)
{
  // Some implementation
};


PyTypeObject Baz_Type = {
  // Some implementation
};
"""
        symbols_to_replace = {"pytypeobject": ["Foo_Type", "Bar_Type"]}
        res = gcs.modify_file(original_lines, symbols_to_replace)
        self.assertEqual(res, expected_lines)

    def test_pytypeobject_macro_definitions_are_replaced(self):
        original_lines = """
PyAPI_DATA(PyTypeObject) Foo_Type;

#define Foo       \\
    { Baz(1) },

typedef int FooBar;

PyAPI_DATA(PyTypeObject) Bar_Type;

#define FooBaz(o) Foo,
"""
        expected_lines = """

#define Foo       \\
    { Baz(1) },

typedef int FooBar;


#define FooBaz(o) Foo,
"""
        symbols_to_replace = {"pytypeobject_macro": ["Foo_Type", "Bar_Type"]}
        res = gcs.modify_file(original_lines, symbols_to_replace)
        self.assertEqual(res, expected_lines)


if __name__ == "__main__":
    unittest.main()
