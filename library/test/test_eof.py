#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
# WARNING: This is a temporary copy of code from the cpython library to
# facilitate bringup. Please file a task for anything you change!
# flake8: noqa
# fmt: off
# TODO(T39919550): unfork this file
"""test script for a few new invalid token catches"""

from test import support
import unittest

class EOFTestCase(unittest.TestCase):
    def test_EOFC(self):
        expect = "EOL while scanning string literal (<string>, line 1)"
        try:
            eval("""'this is a test\
            """)
        except SyntaxError as msg:
            self.assertEqual(str(msg), expect)
        else:
            raise support.TestFailed

    def test_EOFS(self):
        expect = ("EOF while scanning triple-quoted string literal "
                  "(<string>, line 1)")
        try:
            eval("""'''this is a test""")
        except SyntaxError as msg:
            self.assertEqual(str(msg), expect)
        else:
            raise support.TestFailed

    def test_line_continuation_EOF(self):
        """A continuation at the end of input must be an error; bpo2180."""
        expect = 'unexpected EOF while parsing (<string>, line 1)'
        with self.assertRaises(SyntaxError) as excinfo:
            exec('x = 5\\')
        self.assertEqual(str(excinfo.exception), expect)
        with self.assertRaises(SyntaxError) as excinfo:
            exec('\\')
        self.assertEqual(str(excinfo.exception), expect)

if __name__ == "__main__":
    unittest.main()
