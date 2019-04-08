#pragma once

#include "heap.h"
#include "visitor.h"

namespace python {

class Runtime;

// List of predefined symbols, one per line
#define FOREACH_SYMBOL(V)                                                      \
  V(DunderAbs, "__abs__")                                                      \
  V(DunderAdd, "__add__")                                                      \
  V(DunderAenter, "__aenter__")                                                \
  V(DunderAexit, "__aexit__")                                                  \
  V(DunderAnd, "__and__")                                                      \
  V(DunderAnnotations, "__annotations__")                                      \
  V(DunderAiter, "__aiter__")                                                  \
  V(DunderAnext, "__anext__")                                                  \
  V(DunderAwait, "__await__")                                                  \
  V(DunderBases, "__bases__")                                                  \
  V(DunderBool, "__bool__")                                                    \
  V(DunderBuildClass, "__build_class__")                                       \
  V(DunderBuiltins, "__builtins__")                                            \
  V(DunderBytes, "__bytes__")                                                  \
  V(DunderCall, "__call__")                                                    \
  V(DunderCause, "__cause__")                                                  \
  V(DunderCeil, "__ceil__")                                                    \
  V(DunderClass, "__class__")                                                  \
  V(DunderClassCell, "__classcell__")                                          \
  V(DunderCode, "__code__")                                                    \
  V(DunderComplex, "__complex__")                                              \
  V(DunderContains, "__contains__")                                            \
  V(DunderContext, "__context__")                                              \
  V(DunderDelitem, "__delitem__")                                              \
  V(DunderDelattr, "__delattr__")                                              \
  V(DunderDelete, "__delete__")                                                \
  V(DunderDict, "__dict__")                                                    \
  V(DunderDivmod, "__divmod__")                                                \
  V(DunderDoc, "__doc__")                                                      \
  V(DunderEnter, "__enter__")                                                  \
  V(DunderEq, "__eq__")                                                        \
  V(DunderExit, "__exit__")                                                    \
  V(DunderFile, "__file__")                                                    \
  V(DunderFlags, "__flags__")                                                  \
  V(DunderFloat, "__float__")                                                  \
  V(DunderFloor, "__floor__")                                                  \
  V(DunderFloordiv, "__floordiv__")                                            \
  V(DunderFormat, "__format__")                                                \
  V(DunderFspath, "__fspath__")                                                \
  V(DunderGe, "__ge__")                                                        \
  V(DunderGet, "__get__")                                                      \
  V(DunderGetitem, "__getitem__")                                              \
  V(DunderGlobals, "__globals__")                                              \
  V(DunderGt, "__gt__")                                                        \
  V(DunderHash, "__hash__")                                                    \
  V(DunderIadd, "__iadd__")                                                    \
  V(DunderIand, "__iand__")                                                    \
  V(DunderIfloordiv, "__ifloordiv__")                                          \
  V(DunderIlshift, "__ilshift__")                                              \
  V(DunderImatmul, "__imatmul__")                                              \
  V(DunderImod, "__imod__")                                                    \
  V(DunderImport, "__import__")                                                \
  V(DunderImul, "__imul__")                                                    \
  V(DunderIndex, "__index__")                                                  \
  V(DunderInit, "__init__")                                                    \
  V(DunderInt, "__int__")                                                      \
  V(DunderInvert, "__invert__")                                                \
  V(DunderIor, "__ior__")                                                      \
  V(DunderIpow, "__ipow__")                                                    \
  V(DunderIrshift, "__irshift__")                                              \
  V(DunderIsub, "__isub__")                                                    \
  V(DunderIter, "__iter__")                                                    \
  V(DunderItruediv, "__itruediv__")                                            \
  V(DunderIxor, "__ixor__")                                                    \
  V(DunderKeys, "__keys__")                                                    \
  V(DunderLe, "__le__")                                                        \
  V(DunderLen, "__len__")                                                      \
  V(DunderLengthHint, "__length_hint__")                                       \
  V(DunderLoader, "__loader__")                                                \
  V(DunderLshift, "__lshift__")                                                \
  V(DunderLt, "__lt__")                                                        \
  V(DunderMain, "__main__")                                                    \
  V(DunderMatmul, "__matmul__")                                                \
  V(DunderMod, "__mod__")                                                      \
  V(DunderModule, "__module__")                                                \
  V(DunderMro, "__mro__")                                                      \
  V(DunderMul, "__mul__")                                                      \
  V(DunderName, "__name__")                                                    \
  V(DunderNe, "__ne__")                                                        \
  V(DunderNeg, "__neg__")                                                      \
  V(DunderNew, "__new__")                                                      \
  V(DunderNext, "__next__")                                                    \
  V(DunderOr, "__or__")                                                        \
  V(DunderPackage, "__package__")                                              \
  V(DunderPos, "__pos__")                                                      \
  V(DunderPow, "__pow__")                                                      \
  V(DunderPrepare, "__prepare__")                                              \
  V(DunderQualname, "__qualname__")                                            \
  V(DunderRadd, "__radd__")                                                    \
  V(DunderRand, "__rand__")                                                    \
  V(DunderRdivmod, "__rdivmod__")                                              \
  V(DunderRepr, "__repr__")                                                    \
  V(DunderRfloordiv, "__rfloordiv__")                                          \
  V(DunderRlshift, "__rlshift__")                                              \
  V(DunderRmatmul, "__rmatmul__")                                              \
  V(DunderRmod, "__rmod__")                                                    \
  V(DunderRmul, "__rmul__")                                                    \
  V(DunderRor, "__ror__")                                                      \
  V(DunderRound, "__round__")                                                  \
  V(DunderRpow, "__rpow__")                                                    \
  V(DunderRrshift, "__rrshift__")                                              \
  V(DunderRshift, "__rshift__")                                                \
  V(DunderRsub, "__rsub__")                                                    \
  V(DunderRtruediv, "__rtruediv__")                                            \
  V(DunderRxor, "__rxor__")                                                    \
  V(DunderSet, "__set__")                                                      \
  V(DunderSetitem, "__setitem__")                                              \
  V(DunderSizeof, "__sizeof__")                                                \
  V(DunderSpec, "__spec__")                                                    \
  V(DunderStr, "__str__")                                                      \
  V(DunderSub, "__sub__")                                                      \
  V(DunderSuppressContext, "__suppress_context__")                             \
  V(DunderTruediv, "__truediv__")                                              \
  V(DunderTrunc, "__trunc__")                                                  \
  V(DunderValues, "__values__")                                                \
  V(DunderXor, "__xor__")                                                      \
  V(Abs, "abs")                                                                \
  V(AcquireLock, "acquire_lock")                                               \
  V(Add, "add")                                                                \
  V(AndUnder, "and_")                                                          \
  V(Append, "append")                                                          \
  V(Args, "args")                                                              \
  V(Argv, "argv")                                                              \
  V(ArithmeticError, "ArithmeticError")                                        \
  V(Ascii, "ascii")                                                            \
  V(AsciiDecode, "ascii_decode")                                               \
  V(AssertionError, "AssertionError")                                          \
  V(AttributeError, "AttributeError")                                          \
  V(Backslashreplace, "backslashreplace")                                      \
  V(BaseException, "BaseException")                                            \
  V(Big, "big")                                                                \
  V(BitLength, "bit_length")                                                   \
  V(BlockingIOError, "BlockingIOError")                                        \
  V(Bool, "bool")                                                              \
  V(Bootstrap, "bootstrap")                                                    \
  V(BrokenPipeError, "BrokenPipeError")                                        \
  V(BufferError, "BufferError")                                                \
  V(BuiltinModuleNames, "builtin_module_names")                                \
  V(Builtins, "builtins")                                                      \
  V(ByteArray, "bytearray")                                                    \
  V(Byteorder, "byteorder")                                                    \
  V(Bytes, "bytes")                                                            \
  V(BytesWarning, "BytesWarning")                                              \
  V(Callable, "callable")                                                      \
  V(Cast, "cast")                                                              \
  V(Cause, "cause")                                                            \
  V(ChildProcessError, "ChildProcessError")                                    \
  V(Chr, "chr")                                                                \
  V(Classmethod, "classmethod")                                                \
  V(Clear, "clear")                                                            \
  V(CoArgcount, "co_argcount")                                                 \
  V(CoCellvars, "co_cellvars")                                                 \
  V(CoCode, "co_code")                                                         \
  V(CoConsts, "co_consts")                                                     \
  V(CoFilename, "co_filename")                                                 \
  V(CoFirstlineno, "co_firstlineno")                                           \
  V(CoFlags, "co_flags")                                                       \
  V(CoFreevars, "co_freevars")                                                 \
  V(CoKwonlyargcount, "co_kwonlyargcount")                                     \
  V(CoLnotab, "co_lnotab")                                                     \
  V(CoNlocals, "co_nlocals")                                                   \
  V(CoName, "co_name")                                                         \
  V(CoNames, "co_names")                                                       \
  V(CoStacksize, "co_stacksize")                                               \
  V(CoVarnames, "co_varnames")                                                 \
  V(Code, "code")                                                              \
  V(Compile, "compile")                                                        \
  V(Complex, "complex")                                                        \
  V(Concat, "concat")                                                          \
  V(Conjugate, "conjugate")                                                    \
  V(ConnectionAbortedError, "ConnectionAbortedError")                          \
  V(ConnectionError, "ConnectionError")                                        \
  V(ConnectionRefusedError, "ConnectionRefusedError")                          \
  V(ConnectionResetError, "ConnectionResetError")                              \
  V(Contains, "contains")                                                      \
  V(Copy, "copy")                                                              \
  V(CountOf, "countOf")                                                        \
  V(Coroutine, "coroutine")                                                    \
  V(CreateBuiltin, "create_builtin")                                           \
  V(Deleter, "deleter")                                                        \
  V(DeprecationWarning, "DeprecationWarning")                                  \
  V(Dict, "dict")                                                              \
  V(DictItems, "dict_items")                                                   \
  V(DictItemIterator, "dict_itemiterator")                                     \
  V(DictKeys, "dict_keys")                                                     \
  V(DictKeyIterator, "dict_keyiterator")                                       \
  V(DictValues, "dict_values")                                                 \
  V(DictValueIterator, "dict_valueiterator")                                   \
  V(Displayhook, "displayhook")                                                \
  V(Divmod, "divmod")                                                          \
  V(DotSo, ".so")                                                              \
  V(Dummy, "dummy")                                                            \
  V(EOFError, "EOFError")                                                      \
  V(Ellipsis, "ellipsis")                                                      \
  V(Encoding, "encoding")                                                      \
  V(End, "end")                                                                \
  V(Eq, "eq")                                                                  \
  V(Excepthook, "excepthook")                                                  \
  V(Exception, "Exception")                                                    \
  V(ExceptionState, "ExceptionState")                                          \
  V(ExcInfo, "exc_info")                                                       \
  V(Exec, "exec")                                                              \
  V(ExecBuiltin, "exec_builtin")                                               \
  V(ExecDynamic, "exec_dynamic")                                               \
  V(Executable, "executable")                                                  \
  V(Exit, "exit")                                                              \
  V(Extend, "extend")                                                          \
  V(ExtensionPtr, "___extension___")                                           \
  V(ExtensionSuffixes, "extension_suffixes")                                   \
  V(File, "file")                                                              \
  V(Filename, "filename")                                                      \
  V(Fileno, "fileno")                                                          \
  V(FileExistsError, "FileExistsError")                                        \
  V(FileNotFoundError, "FileNotFoundError")                                    \
  V(Find, "find")                                                              \
  V(FixCoFilename, "_fix_co_filename")                                         \
  V(Format, "format")                                                          \
  V(Float, "float")                                                            \
  V(FloatingPointError, "FloatingPointError")                                  \
  V(Floordiv, "floordiv")                                                      \
  V(Frame, "frame")                                                            \
  V(FromBytes, "from_bytes")                                                   \
  V(FrozenSet, "frozenset")                                                    \
  V(Function, "function")                                                      \
  V(FutureWarning, "FutureWarning")                                            \
  V(Generator, "generator")                                                    \
  V(GeneratorExit, "GeneratorExit")                                            \
  V(Get, "get")                                                                \
  V(GetSizeOf, "getsizeof")                                                    \
  V(GetFrozenObject, "get_frozen_object")                                      \
  V(Getattr, "getattr")                                                        \
  V(Getter, "getter")                                                          \
  V(Hasattr, "hasattr")                                                        \
  V(Hex, "hex")                                                                \
  V(Iadd, "iadd")                                                              \
  V(Iand, "iand")                                                              \
  V(Iconcat, "iconcat")                                                        \
  V(Ifloordiv, "ifloordiv")                                                    \
  V(Ignore, "ignore")                                                          \
  V(Ilshift, "ilshift")                                                        \
  V(Imatmul, "imatmul")                                                        \
  V(Imod, "imod")                                                              \
  V(Imul, "imul")                                                              \
  V(Ior, "ior")                                                                \
  V(Ipow, "ipow")                                                              \
  V(Irshift, "irshift")                                                        \
  V(Irepeat, "irepeat")                                                        \
  V(Isub, "isub")                                                              \
  V(Itruediv, "itruediv")                                                      \
  V(Ixor, "ixor")                                                              \
  V(ImportError, "ImportError")                                                \
  V(ImportWarning, "ImportWarning")                                            \
  V(IndentationError, "IndentationError")                                      \
  V(IndexError, "IndexError")                                                  \
  V(IndexOf, "indexOf")                                                        \
  V(Insert, "insert")                                                          \
  V(Int, "int")                                                                \
  V(InterruptedError, "InterruptedError")                                      \
  V(Intersection, "intersection")                                              \
  V(Invert, "invert")                                                          \
  V(IsADirectoryError, "IsADirectoryError")                                    \
  V(IsBuiltin, "is_builtin")                                                   \
  V(IsDisjoint, "isdisjoint")                                                  \
  V(IsFrozen, "is_frozen")                                                     \
  V(IsFrozenPackage, "is_frozen_package")                                      \
  V(IsInstance, "isinstance")                                                  \
  V(IsSubclass, "issubclass")                                                  \
  V(Items, "items")                                                            \
  V(Itertools, "itertools")                                                    \
  V(Join, "join")                                                              \
  V(KeyError, "KeyError")                                                      \
  V(KeyboardInterrupt, "KeyboardInterrupt")                                    \
  V(Keys, "keys")                                                              \
  V(Length, "length")                                                          \
  V(LStrip, "lstrip")                                                          \
  V(LargeInt, "largeint")                                                      \
  V(LargeStr, "largestr")                                                      \
  V(LastType, "last_type")                                                     \
  V(LastValue, "last_value")                                                   \
  V(LastTraceback, "last_traceback")                                           \
  V(Layout, "layout")                                                          \
  V(Lineno, "lineno")                                                          \
  V(List, "list")                                                              \
  V(ListIterator, "list_iterator")                                             \
  V(Little, "little")                                                          \
  V(Loads, "loads")                                                            \
  V(LookupError, "LookupError")                                                \
  V(Lower, "lower")                                                            \
  V(Lshift, "lshift")                                                          \
  V(Lt, "lt")                                                                  \
  V(Marshal, "marshal")                                                        \
  V(Matmul, "matmul")                                                          \
  V(Maxsize, "maxsize")                                                        \
  V(MemoryError, "MemoryError")                                                \
  V(MemoryView, "memoryview")                                                  \
  V(MetaPath, "meta_path")                                                     \
  V(Metaclass, "metaclass")                                                    \
  V(Method, "method")                                                          \
  V(Mod, "mod")                                                                \
  V(Module, "module")                                                          \
  V(ModuleNotFoundError, "ModuleNotFoundError")                                \
  V(Modules, "modules")                                                        \
  V(Msg, "msg")                                                                \
  V(Mul, "mul")                                                                \
  V(NFields, "n_fields")                                                       \
  V(NSequenceFields, "n_sequence_fields")                                      \
  V(NUnnamedFields, "n_unnamed_fields")                                        \
  V(Name, "name")                                                              \
  V(NameError, "NameError")                                                    \
  V(Neg, "neg")                                                                \
  V(None, "None")                                                              \
  V(NoneType, "NoneType")                                                      \
  V(NotADirectoryError, "NotADirectoryError")                                  \
  V(NotImplemented, "NotImplemented")                                          \
  V(NotImplementedError, "NotImplementedError")                                \
  V(NotImplementedType, "NotImplementedType")                                  \
  V(Null, "<NULL>")                                                            \
  V(OSError, "OSError")                                                        \
  V(ObjectTypename, "object")                                                  \
  V(Offset, "offset")                                                          \
  V(Operator, "operator")                                                      \
  V(Ord, "ord")                                                                \
  V(OrUnder, "or_")                                                            \
  V(OverflowError, "OverflowError")                                            \
  V(Partition, "partition")                                                    \
  V(Path, "path")                                                              \
  V(PendingDeprecationWarning, "PendingDeprecationWarning")                    \
  V(PermissionError, "PermissionError")                                        \
  V(Platform, "platform")                                                      \
  V(Pop, "pop")                                                                \
  V(Pos, "pos")                                                                \
  V(Pow, "pow")                                                                \
  V(PrintFileAndLine, "print_file_and_line")                                   \
  V(ProcessLookupError, "ProcessLookupError")                                  \
  V(Property, "property")                                                      \
  V(Range, "range")                                                            \
  V(RangeIterator, "range_iterator")                                           \
  V(Reason, "reason")                                                          \
  V(RecursionError, "RecursionError")                                          \
  V(Ref, "ref")                                                                \
  V(ReferenceError, "ReferenceError")                                          \
  V(ReleaseLock, "release_lock")                                               \
  V(Remove, "remove")                                                          \
  V(Rfind, "rfind")                                                            \
  V(Repr, "repr")                                                              \
  V(Replace, "replace")                                                        \
  V(ResourceWarning, "ResourceWarning")                                        \
  V(RPartition, "rpartition")                                                  \
  V(Rshift, "rshift")                                                          \
  V(RSplit, "rsplit")                                                          \
  V(RStrip, "rstrip")                                                          \
  V(RuntimeError, "RuntimeError")                                              \
  V(RuntimeWarning, "RuntimeWarning")                                          \
  V(Send, "send")                                                              \
  V(Set, "set")                                                                \
  V(SetIterator, "set_iterator")                                               \
  V(SeqIterator, "iterator")                                                   \
  V(Setattr, "setattr")                                                        \
  V(Setter, "setter")                                                          \
  V(Signed, "signed")                                                          \
  V(Size, "size")                                                              \
  V(Slice, "slice")                                                            \
  V(SmallInt, "smallint")                                                      \
  V(SmallStr, "smallstr")                                                      \
  V(Split, "split")                                                            \
  V(Start, "start")                                                            \
  V(StaticMethod, "staticmethod")                                              \
  V(Stderr, "stderr")                                                          \
  V(Stdout, "stdout")                                                          \
  V(Step, "step")                                                              \
  V(Stop, "stop")                                                              \
  V(StopAsyncIteration, "StopAsyncIteration")                                  \
  V(StopIteration, "StopIteration")                                            \
  V(Str, "str")                                                                \
  V(StrIterator, "str_iterator")                                               \
  V(Strict, "strict")                                                          \
  V(Strip, "strip")                                                            \
  V(Sub, "sub")                                                                \
  V(Super, "super")                                                            \
  V(Surrogateescape, "surrogateescape")                                        \
  V(Surrogatepass, "surrogatepass")                                            \
  V(SyntaxError, "SyntaxError")                                                \
  V(SyntaxWarning, "SyntaxWarning")                                            \
  V(Sys, "sys")                                                                \
  V(SystemError, "SystemError")                                                \
  V(SystemExit, "SystemExit")                                                  \
  V(TabError, "TabError")                                                      \
  V(Text, "text")                                                              \
  V(Time, "time")                                                              \
  V(TimeoutError, "TimeoutError")                                              \
  V(ToBytes, "to_bytes")                                                       \
  V(Traceback, "traceback")                                                    \
  V(Truediv, "truediv")                                                        \
  V(Tuple, "tuple")                                                            \
  V(TupleIterator, "tuple_iterator")                                           \
  V(Type, "type")                                                              \
  V(TypeError, "TypeError")                                                    \
  V(UnboundLocalError, "UnboundLocalError")                                    \
  V(UnderAddress, "_address")                                                  \
  V(UnderAsciiDecode, "_ascii_decode")                                         \
  V(UnderBases, "_bases")                                                      \
  V(UnderBootstrap, "_bootstrap")                                              \
  V(UnderByteArrayJoin, "_bytearray_join")                                     \
  V(UnderByteArrayStringAppend, "_bytearray_string_append")                    \
  V(UnderByteArrayToString, "_bytearray_to_string")                            \
  V(UnderBytesGetitem, "_bytes_getitem")                                       \
  V(UnderBytesGetitemSlice, "_bytes_getitem_slice")                            \
  V(UnderBytesJoin, "_bytes_join")                                             \
  V(UnderBytesMaketrans, "_bytes_maketrans")                                   \
  V(UnderBytesNew, "_bytes_new")                                               \
  V(UnderCodecs, "_codecs")                                                    \
  V(UnderComplexImag, "_complex_imag")                                         \
  V(UnderComplexReal, "_complex_real")                                         \
  V(UnderFdWrite, "_fd_write")                                                 \
  V(UnderFindAndLoad, "_find_and_load")                                        \
  V(UnderFrozenImportlib, "_frozen_importlib")                                 \
  V(UnderFrozenImportlibExternal, "_frozen_importlib_external")                \
  V(UnderFunctools, "_functools")                                              \
  V(UnderImp, "_imp")                                                          \
  V(UnderInstall, "_install")                                                  \
  V(UnderIo, "_io")                                                            \
  V(UnderListSort, "_list_sort")                                               \
  V(UnderLongOfObj, "_long_of_obj")                                            \
  V(UnderModuleRepr, "_module_repr")                                           \
  V(UnderPatch, "_patch")                                                      \
  V(UnderReadBytes, "_readbytes")                                              \
  V(UnderReadFile, "_readfile")                                                \
  V(UnderReferent, "_referent")                                                \
  V(UnderReprEnter, "_repr_enter")                                             \
  V(UnderReprLeave, "_repr_leave")                                             \
  V(UnderSliceIndex, "_slice_index")                                           \
  V(UnderStderrFd, "_stderr_fd")                                               \
  V(UnderStdout, "_stdout")                                                    \
  V(UnderStdoutFd, "_stdout_fd")                                               \
  V(UnderStrEscapeNonAscii, "_str_escape_non_ascii")                           \
  V(UnderStrFind, "_str_find")                                                 \
  V(UnderStrReplace, "_str_replace")                                           \
  V(UnderStrRFind, "_str_rfind")                                               \
  V(UnderStructseqField, "_structseq_field")                                   \
  V(UnderStructseqFieldNames, "_structseq_field_names")                        \
  V(UnderStructseqGetAttr, "_structseq_getattr")                               \
  V(UnderStructseqGetItem, "_structseq_getitem")                               \
  V(UnderStructseqNew, "_structseq_new")                                       \
  V(UnderStructseqRepr, "_structseq_repr")                                     \
  V(UnderStructseqSetAttr, "_structseq_setattr")                               \
  V(UnderThread, "_thread")                                                    \
  V(UnderUnbound, "_Unbound")                                                  \
  V(UnderUnimplemented, "_unimplemented")                                      \
  V(UnderWarnings, "_warnings")                                                \
  V(UnderWeakRef, "_weakref")                                                  \
  V(Anonymous, "<anonymous>")                                                  \
  V(UnicodeDecodeError, "UnicodeDecodeError")                                  \
  V(UnicodeEncodeError, "UnicodeEncodeError")                                  \
  V(UnicodeError, "UnicodeError")                                              \
  V(UnicodeTranslateError, "UnicodeTranslateError")                            \
  V(UnicodeWarning, "UnicodeWarning")                                          \
  V(Update, "update")                                                          \
  V(Upper, "upper")                                                            \
  V(UserWarning, "UserWarning")                                                \
  V(Value, "value")                                                            \
  V(Values, "values")                                                          \
  V(ValueCell, "valuecell")                                                    \
  V(ValueError, "ValueError")                                                  \
  V(Version, "version")                                                        \
  V(Warn, "warn")                                                              \
  V(Warning, "Warning")                                                        \
  V(Write, "write")                                                            \
  V(Xmlcharrefreplace, "xmlcharrefreplace")                                    \
  V(Xor, "xor")                                                                \
  V(ZeroDivisionError, "ZeroDivisionError")

// clang-format off
enum class SymbolId {
  kInvalid = -1,
#define DEFINE_SYMBOL_INDEX(symbol, value) k##symbol,
  FOREACH_SYMBOL(DEFINE_SYMBOL_INDEX)
#undef DEFINE_SYMBOL_INDEX
  kMaxId,
  kSentinelId,
};
// clang-format on

// Provides convenient, fast access to commonly used names. Stolen from Dart.
class Symbols {
 public:
  explicit Symbols(Runtime* runtime);
  ~Symbols();

#define DEFINE_SYMBOL_ACCESSOR(symbol, value)                                  \
  RawObject symbol() { return at(SymbolId::k##symbol); }
  FOREACH_SYMBOL(DEFINE_SYMBOL_ACCESSOR)
#undef DEFINE_SYMBOL_ACCESSOR

  void visit(PointerVisitor* visitor);

  RawObject at(SymbolId id) {
    int index = static_cast<int>(id);
    DCHECK_INDEX(index, static_cast<int>(SymbolId::kMaxId));
    return symbols_[index];
  }

  const char* literalAt(SymbolId id);

 private:
  // TODO(T25010996) - Benchmark whether this is faster than an Tuple
  RawObject* symbols_;
};

}  // namespace python
