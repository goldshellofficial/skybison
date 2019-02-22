#!/usr/bin/env python3
"""The sys module"""


def exit(code=0):
    raise SystemExit(code)


path_hooks = []


path_importer_cache = {}


# TODO(T39224400): Implement flags as a structsequence
class FlagsStructSeq:
    def __init__(self):
        self.verbose = 0


flags = FlagsStructSeq()


# TODO(T40871632): Add sys.implementation as a namespace object
class ImplementationType:
    def __init__(self):
        # TODO(T40871490): Cache compiles *.py files to a __cache__ directory
        # Setting cache_tag to None avoids caching or searching for cached files
        self.cache_tag = None


implementation = ImplementationType()


dont_write_bytecode = False
