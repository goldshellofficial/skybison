# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
class F():
    z: int

# EXPECTED:
[
    ...,
    CODE_START('F'),
    ...,
    SETUP_ANNOTATIONS(0),
    LOAD_NAME('int'),
    STORE_ANNOTATION('z'),
    ...,
]
