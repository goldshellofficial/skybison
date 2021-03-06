#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
# $builtin-init-module$

from _builtins import _builtin, _Unbound, _unimplemented


# TODO(T36511309): make UCD non-extensible
class UCD:
    # ucd_3_2_0 is the only instance of UCD
    def __new__(cls, *args, **kwargs):
        raise TypeError(f"cannot create 'unicodedata.UCD' instances")

    def bidirectional(self, chr):
        _builtin()

    def category(self, chr):
        _builtin()

    def combining(self, chr):
        _unimplemented()

    def decimal(self, chr, default=_Unbound):
        _builtin()

    def decomposition(self, chr):
        _builtin()

    def digit(self, chr, default=_Unbound):
        _builtin()

    def east_asian_width(self, chr):
        _unimplemented()

    def is_normalized(self, form, unistr):
        _unimplemented()

    def lookup(self, name):
        _unimplemented()

    def mirrored(self, chr):
        _unimplemented()

    def name(self, chr, default=_Unbound):
        _unimplemented()

    def normalize(self, form, unistr):
        _builtin()

    def numeric(self, chr, default=_Unbound):
        _unimplemented()

    @property
    def unidata_version(self):
        return "3.2.0"


def bidirectional(chr):
    _builtin()


def category(chr):
    _builtin()


def combining(chr):
    _unimplemented()


def decimal(chr, default=_Unbound):
    _builtin()


def decomposition(chr):
    _builtin()


def digit(chr, default=_Unbound):
    _builtin()


def east_asian_width(chr):
    _unimplemented()


def is_normalized(form, unistr):
    _unimplemented()


def lookup(name):
    _builtin()


def mirrored(chr):
    _unimplemented()


def name(chr, default=_Unbound):
    _unimplemented()


def normalize(form, unistr):
    _builtin()


def numeric(chr, default=_Unbound):
    _builtin()


unidata_version = "11.0.0"
