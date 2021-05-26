from .consts import PyCF_ONLY_AST, PyCF_DONT_IMPLY_DEDENT

CO_FUTURE_DIVISION = 0x2000
CO_FUTURE_ABSOLUTE_IMPORT = 0x4000
CO_FUTURE_WITH_STATEMENT = 0x8000
CO_FUTURE_PRINT_FUNCTION = 0x10000
CO_FUTURE_UNICODE_LITERALS = 0x20000
CO_FUTURE_BARRY_AS_BDFL = 0x40000
CO_FUTURE_GENERATOR_STOP = 0x80000
CO_FUTURE_ANNOTATIONS = 0x100000
CO_STATICALLY_COMPILED = 0x200000
PyCF_COMPILE_MASK: int = PyCF_ONLY_AST | PyCF_DONT_IMPLY_DEDENT
CO_NO_FRAME = 0x800000

PyCF_MASK: int = (
    CO_FUTURE_DIVISION
    | CO_FUTURE_ABSOLUTE_IMPORT
    | CO_FUTURE_WITH_STATEMENT
    | CO_FUTURE_PRINT_FUNCTION
    | CO_FUTURE_UNICODE_LITERALS
    | CO_FUTURE_BARRY_AS_BDFL
    | CO_FUTURE_GENERATOR_STOP
    | CO_FUTURE_ANNOTATIONS
    | CO_STATICALLY_COMPILED
    | CO_NO_FRAME
)
