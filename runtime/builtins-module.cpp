#include "builtins-module.h"

#include <unistd.h>
#include <iostream>

#include "frame.h"
#include "globals.h"
#include "handles.h"
#include "int-builtins.h"
#include "interpreter.h"
#include "objects.h"
#include "runtime.h"
#include "thread.h"
#include "type-builtins.h"

namespace python {

std::ostream* builtInStdout = &std::cout;
std::ostream* builtinStderr = &std::cerr;

RawObject builtinBuildClass(Thread* thread, Frame* frame, word nargs) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  if (nargs < 2) {
    std::abort();  // TODO(cshapiro): throw a TypeError exception.
  }
  Arguments args(frame, nargs);
  if (!args.get(0)->isFunction()) {
    std::abort();  // TODO(cshapiro): throw a TypeError exception.
  }
  if (!args.get(1)->isStr()) {
    std::abort();  // TODO(cshapiro): throw a TypeError exception.
  }

  Function body(&scope, args.get(0));
  Object name(&scope, args.get(1));
  Tuple bases(&scope, runtime->newTuple(nargs - 2));
  for (word i = 0, j = 2; j < nargs; i++, j++) {
    bases->atPut(i, args.get(j));
  }

  Dict dict(&scope, runtime->newDict());
  Object key(&scope, runtime->symbols()->DunderName());
  runtime->dictAtPutInValueCell(dict, key, name);
  // TODO(cshapiro): might need to do some kind of callback here and we want
  // backtraces to work correctly.  The key to doing that would be to put some
  // state on the stack in between the the incoming arguments from the builtin'
  // caller and the on-stack state for the class body function call.
  thread->runClassFunction(*body, *dict);

  Type type(&scope, runtime->typeAt(LayoutId::kType));
  Function dunder_call(
      &scope, runtime->lookupSymbolInMro(thread, type, SymbolId::kDunderCall));
  frame->pushValue(*dunder_call);
  frame->pushValue(*type);
  frame->pushValue(*name);
  frame->pushValue(*bases);
  frame->pushValue(*dict);
  return Interpreter::call(thread, frame, 4);
}

RawObject builtinBuildClassKw(Thread* thread, Frame* frame, word nargs) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  KwArguments args(frame, nargs);
  if (args.numArgs() < 2) {
    return thread->raiseTypeErrorWithCStr("not enough args for build class.");
  }
  if (!args.get(0)->isFunction()) {
    return thread->raiseTypeErrorWithCStr("class body is not function.");
  }
  if (!args.get(1)->isStr()) {
    return thread->raiseTypeErrorWithCStr("class name is not string.");
  }

  Function body(&scope, args.get(0));
  Object name(&scope, args.get(1));

  Object bootstrap(&scope, args.getKw(runtime->symbols()->Bootstrap()));
  if (bootstrap->isError()) {
    bootstrap = Bool::falseObj();
  }

  Object metaclass(&scope, args.getKw(runtime->symbols()->Metaclass()));
  if (metaclass->isError()) {
    metaclass = runtime->typeAt(LayoutId::kType);
  }

  Tuple bases(&scope,
              runtime->newTuple(args.numArgs() - args.numKeywords() - 1));
  for (word i = 0, j = 2; j < args.numArgs(); i++, j++) {
    bases->atPut(i, args.get(j));
  }

  Object dict_obj(&scope, NoneType::object());
  Object type_obj(&scope, NoneType::object());
  if (*bootstrap == Bool::falseObj()) {
    // An ordinary class initialization creates a new class dictionary.
    dict_obj = runtime->newDict();
  } else {
    // A bootstrap class initialization uses the existing class dictionary.
    CHECK(frame->previousFrame() != nullptr, "must have a caller frame");
    Dict globals(&scope, frame->previousFrame()->globals());
    ValueCell value_cell(&scope, runtime->dictAt(globals, name));
    CHECK(value_cell->value()->isType(), "name is not bound to a type object");
    Type type(&scope, value_cell->value());
    type_obj = *type;
    dict_obj = type->dict();
  }

  // TODO(zekun): might need to do some kind of callback here and we want
  // backtraces to work correctly.  The key to doing that would be to put some
  // state on the stack in between the the incoming arguments from the builtin'
  // caller and the on-stack state for the class body function call.
  thread->runClassFunction(*body, *dict_obj);

  // A bootstrap class initialization is complete at this point.  Add a type
  // name to the type dictionary and return the initialized type object.
  if (*bootstrap == Bool::trueObj()) {
    Object key(&scope, runtime->symbols()->DunderName());
    Dict dict(&scope, *dict_obj);
    runtime->dictAtPutInValueCell(dict, key, name);
    return *type_obj;
  }

  Type type(&scope, *metaclass);
  Function dunder_call(
      &scope, runtime->lookupSymbolInMro(thread, type, SymbolId::kDunderCall));
  frame->pushValue(*dunder_call);
  frame->pushValue(*type);
  frame->pushValue(*name);
  frame->pushValue(*bases);
  frame->pushValue(*dict_obj);
  return Interpreter::call(thread, frame, 4);
}

RawObject builtinCallable(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 1) {
    return thread->raiseTypeErrorWithCStr("callable expects one argument");
  }
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object arg(&scope, args.get(0));
  if (arg->isFunction() || arg->isBoundMethod() || arg->isType()) {
    return Bool::trueObj();
  }
  Runtime* runtime = thread->runtime();
  Type type(&scope, runtime->typeOf(*arg));
  // If its type defines a __call__, it is also callable (even if __call__ is
  // not actually callable).
  // Note that this does not include __call__ defined on the particular
  // instance, only __call__ defined on the type.
  Object callable(&scope, thread->runtime()->lookupSymbolInMro(
                              thread, type, SymbolId::kDunderCall));
  return Bool::fromBool(!callable->isError());
}

RawObject builtinChr(Thread* thread, Frame* frame_frame, word nargs) {
  if (nargs != 1) {
    return thread->raiseTypeErrorWithCStr("Unexpected 1 argumment in 'chr'");
  }
  Arguments args(frame_frame, nargs);
  RawObject arg = args.get(0);
  if (!arg->isSmallInt()) {
    return thread->raiseTypeErrorWithCStr("Unsupported type in builtin 'chr'");
  }
  word w = RawSmallInt::cast(arg)->value();
  const char s[2]{static_cast<char>(w), 0};
  return SmallStr::fromCStr(s);
}

// TODO(mpage): isinstance (somewhat unsurprisingly at this point I guess) is
// actually far more complicated than one might expect. This is enough to get
// richards working.
RawObject builtinIsinstance(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 2) {
    return thread->raiseTypeErrorWithCStr("isinstance expected 2 arguments");
  }

  Arguments args(frame, nargs);
  if (!args.get(1)->isType()) {
    // TODO(mpage): This error message is misleading. Ultimately, isinstance()
    // may accept a type or a tuple.
    return thread->raiseTypeErrorWithCStr("isinstance arg 2 must be a type");
  }

  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Object obj(&scope, args.get(0));
  Type type(&scope, args.get(1));
  return runtime->isInstance(obj, type);
}

RawObject builtinIssubclass(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 2) {
    return thread->raiseTypeErrorWithCStr("issubclass expected 2 arguments");
  }

  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  if (!args.get(0)->isType()) {
    return thread->raiseTypeErrorWithCStr("issubclass arg 1 must be a type");
  }
  Type type(&scope, args.get(0));
  Object classinfo(&scope, args.get(1));
  if (runtime->isInstanceOfType(*classinfo)) {
    Type possible_superclass(&scope, *classinfo);
    return runtime->isSubClass(type, possible_superclass);
  }
  // If classinfo is not a tuple, then throw a TypeError.
  if (!classinfo->isTuple()) {
    return thread->raiseTypeErrorWithCStr(
        "issubclass() arg 2 must be a class of tuple of classes");
  }
  // If classinfo is a tuple, try each of the values, and return
  // True if the first argument is a subclass of any of them.
  Tuple tuple_of_types(&scope, *classinfo);
  for (word i = 0; i < tuple_of_types->length(); i++) {
    // If any argument is not a type, then throw TypeError.
    if (!runtime->isInstanceOfType(tuple_of_types->at(i))) {
      return thread->raiseTypeErrorWithCStr(
          "issubclass() arg 2 must be a class of tuple of classes");
    }
    Type possible_superclass(&scope, tuple_of_types->at(i));
    Bool result(&scope, runtime->isSubClass(type, possible_superclass));
    // If any of the types are a superclass, return true.
    if (result->value()) {
      return Bool::trueObj();
    }
  }
  // None of the types in the tuple were a superclass, so return false.
  return Bool::falseObj();
}

RawObject builtinLen(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 1) {
    return thread->raiseTypeErrorWithCStr("len() takes exactly one argument");
  }
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self(&scope, args.get(0));
  Object method(&scope, Interpreter::lookupMethod(thread, frame, self,
                                                  SymbolId::kDunderLen));
  if (method->isError()) {
    return thread->raiseTypeErrorWithCStr("object has no len()");
  }
  return Interpreter::callMethod1(thread, frame, method, self);
}

RawObject builtinOrd(Thread* thread, Frame* frame_frame, word nargs) {
  if (nargs != 1) {
    return thread->raiseTypeErrorWithCStr("Unexpected 1 argumment in 'ord'");
  }
  Arguments args(frame_frame, nargs);
  RawObject arg = args.get(0);
  if (!arg->isStr()) {
    return thread->raiseTypeErrorWithCStr("Unsupported type in builtin 'ord'");
  }
  auto str = RawStr::cast(arg);
  if (str->length() != 1) {
    return thread->raiseTypeErrorWithCStr(
        "Builtin 'ord' expects string of length 1");
  }
  return SmallInt::fromWord(str->charAt(0));
}

static void printStr(RawStr str, std::ostream* ostream) {
  for (word i = 0; i < str->length(); i++) {
    *ostream << str->charAt(i);
  }
}

static void printQuotedStr(RawStr str, std::ostream* ostream) {
  *ostream << "'";
  for (word i = 0; i < str->length(); i++) {
    *ostream << str->charAt(i);
  }
  *ostream << "'";
}

// Print a scalar value to ostream.
static void printScalarTypes(RawObject arg, std::ostream* ostream) {
  if (arg->isBool()) {
    *ostream << (RawBool::cast(arg)->value() ? "True" : "False");
  } else if (arg->isFloat()) {
    *ostream << RawFloat::cast(arg)->value();
  } else if (arg->isSmallInt()) {
    *ostream << RawSmallInt::cast(arg)->value();
  } else if (arg->isStr()) {
    printStr(RawStr::cast(arg), ostream);
  } else {
    UNIMPLEMENTED("Custom print unsupported");
  }
}

// Print a scalar value to ostream, quoting it if it's a string.
static void printQuotedScalarTypes(RawObject arg, std::ostream* ostream) {
  if (arg->isStr()) {
    printQuotedStr(RawStr::cast(arg), ostream);
  } else {
    printScalarTypes(arg, ostream);
  }
}

static bool supportedScalarType(RawObject arg) {
  return (arg->isBool() || arg->isFloat() || arg->isSmallInt() || arg->isStr());
}

// NB: The print functions do not represent the final state of builtin functions
// and should not be emulated when creating new builtins. They are minimal
// implementations intended to get the Richards & Pystone benchmark working.
static RawObject doBuiltinPrint(const Arguments& args, word nargs,
                                const Object& end, std::ostream* ostream) {
  const char separator = ' ';

  for (word i = 0; i < nargs; i++) {
    RawObject arg = args.get(i);
    if (supportedScalarType(arg)) {
      printScalarTypes(arg, ostream);
    } else if (arg->isList()) {
      *ostream << "[";
      HandleScope scope;
      List list(&scope, arg);
      for (word i = 0; i < list->numItems(); i++) {
        if (supportedScalarType(list->at(i))) {
          printQuotedScalarTypes(list->at(i), ostream);
        } else {
          UNIMPLEMENTED("Custom print unsupported");
        }
        if (i != list->numItems() - 1) {
          *ostream << ", ";
        }
      }
      *ostream << "]";
    } else if (arg->isTuple()) {
      *ostream << "(";
      HandleScope scope;
      Tuple array(&scope, arg);
      for (word i = 0; i < array->length(); i++) {
        if (supportedScalarType(array->at(i))) {
          printScalarTypes(array->at(i), ostream);
        } else {
          UNIMPLEMENTED("Custom print unsupported");
        }
        if (i != array->length() - 1) {
          *ostream << ", ";
        }
      }
      *ostream << ")";
    } else if (arg->isDict()) {
      *ostream << "{";
      HandleScope scope;
      Dict dict(&scope, arg);
      Tuple data(&scope, dict->data());
      word items = dict->numItems();
      for (word i = 0; i < data->length(); i += 3) {
        if (!data->at(i)->isNoneType()) {
          Object key(&scope, Dict::Bucket::key(*data, i));
          Object value(&scope, Dict::Bucket::value(*data, i));
          if (supportedScalarType(*key)) {
            printQuotedScalarTypes(*key, ostream);
          } else {
            UNIMPLEMENTED("Custom print unsupported");
          }
          *ostream << ": ";
          if (supportedScalarType(*value)) {
            printQuotedScalarTypes(*value, ostream);
          } else {
            UNIMPLEMENTED("Custom print unsupported");
          }
          if (items-- != 1) {
            *ostream << ", ";
          }
        }
      }
      *ostream << "}";
    } else if (arg->isNoneType()) {
      *ostream << "None";
    } else {
      UNIMPLEMENTED("Custom print unsupported");
    }
    if ((i + 1) != nargs) {
      *ostream << separator;
    }
  }
  if (end->isNoneType()) {
    *ostream << "\n";
  } else if (end->isStr()) {
    printStr(RawStr::cast(*end), ostream);
  } else {
    UNIMPLEMENTED("Unexpected type for end: %ld",
                  static_cast<word>(end->layoutId()));
  }

  return NoneType::object();
}

RawObject builtinPrint(Thread* thread, Frame* frame, word nargs) {
  HandleScope scope(thread);
  Object end(&scope, NoneType::object());
  Arguments args(frame, nargs);
  return doBuiltinPrint(args, nargs, end, builtInStdout);
}

RawObject builtinPrintKw(Thread* thread, Frame* frame, word nargs) {
  KwArguments kw_args(frame, nargs);
  HandleScope scope(thread);
  if (kw_args.numKeywords() > 2) {
    return thread->raiseRuntimeErrorWithCStr(
        "Too many keyword arguments supplied to print");
  }

  Runtime* runtime = thread->runtime();
  RawObject end = NoneType::object();
  std::ostream* ostream = builtInStdout;

  Object file_arg(&scope, kw_args.getKw(runtime->symbols()->File()));
  if (!file_arg->isError()) {
    if (file_arg->isSmallInt()) {
      word stream_val = RawSmallInt::cast(*file_arg)->value();
      switch (stream_val) {
        case STDOUT_FILENO:
          ostream = builtInStdout;
          break;
        case STDERR_FILENO:
          ostream = builtinStderr;
          break;
        default:
          return thread->raiseTypeErrorWithCStr(
              "Unsupported argument type for 'file'");
      }
    } else {
      return thread->raiseTypeErrorWithCStr(
          "Unsupported argument type for 'file'");
    }
  }

  Object end_arg(&scope, kw_args.getKw(runtime->symbols()->End()));
  if (!end_arg->isError()) {
    if ((end_arg->isStr() || end_arg->isNoneType())) {
      end = *end_arg;
    } else {
      return thread->raiseTypeErrorWithCStr("Unsupported argument for 'end'");
    }
  }

  // Remove kw arg tuple and the value for the end keyword argument
  Arguments rest(frame, nargs - kw_args.numKeywords() - 1);
  Object end_val(&scope, end);
  return doBuiltinPrint(rest, nargs - kw_args.numKeywords() - 1, end_val,
                        ostream);
}

RawObject builtinRange(Thread* thread, Frame* frame, word nargs) {
  if (nargs < 1 || nargs > 3) {
    return thread->raiseTypeErrorWithCStr(
        "Incorrect number of arguments to range()");
  }

  Arguments args(frame, nargs);

  for (word i = 0; i < nargs; i++) {
    if (!args.get(i)->isSmallInt()) {
      return thread->raiseTypeErrorWithCStr(
          "Arguments to range() must be an integers.");
    }
  }

  word start = 0;
  word stop = 0;
  word step = 1;

  if (nargs == 1) {
    stop = RawSmallInt::cast(args.get(0))->value();
  } else if (nargs == 2) {
    start = RawSmallInt::cast(args.get(0))->value();
    stop = RawSmallInt::cast(args.get(1))->value();
  } else if (nargs == 3) {
    start = RawSmallInt::cast(args.get(0))->value();
    stop = RawSmallInt::cast(args.get(1))->value();
    step = RawSmallInt::cast(args.get(2))->value();
  }

  if (step == 0) {
    return thread->raiseValueErrorWithCStr(
        "range() step argument must not be zero");
  }

  return thread->runtime()->newRange(start, stop, step);
}

RawObject builtinRepr(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 1) {
    return thread->raiseTypeErrorWithCStr("repr() takes exactly one argument");
  }
  Arguments args(frame, nargs);

  HandleScope scope(thread);
  Object obj(&scope, args.get(0));
  // Only one argument, the value to be repr'ed.
  Object method(&scope, Interpreter::lookupMethod(thread, frame, obj,
                                                  SymbolId::kDunderRepr));
  CHECK(!method->isError(),
        "__repr__ doesn't exist for this object, which is impossible since "
        "object has a __repr__, and everything descends from object");
  RawObject ret = Interpreter::callMethod1(thread, frame, method, obj);
  if (!ret->isStr() && !ret->isError()) {
    // TODO(T31744782): Change this to allow subtypes of string.
    // If __repr__ doesn't return a string or error, throw a type error
    return thread->raiseTypeErrorWithCStr("__repr__ returned non-string");
  }
  return ret;
}

RawObject builtinGetattr(Thread* thread, Frame* frame, word nargs) {
  if (nargs < 2 || nargs > 3) {
    return thread->raiseTypeErrorWithCStr("getattr expected 2 or 3 arguments.");
  }
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self(&scope, args.get(0));
  Object name(&scope, args.get(1));
  if (!name->isStr()) {
    return thread->raiseTypeErrorWithCStr(
        "getattr(): attribute name must be string.");
  }
  Object result(&scope, thread->runtime()->attributeAt(thread, self, name));
  if (result->isError() && nargs == 3) {
    result = args.get(2);
    // TODO(T32775277) Implement PyErr_ExceptionMatches
    thread->clearPendingException();
  }
  return *result;
}

RawObject builtinHasattr(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 2) {
    return thread->raiseTypeErrorWithCStr("hasattr expected 2 arguments.");
  }
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self(&scope, args.get(0));
  Object name(&scope, args.get(1));
  if (!name->isStr()) {
    return thread->raiseTypeErrorWithCStr(
        "hasattr(): attribute name must be string.");
  }
  Object result(&scope, thread->runtime()->attributeAt(thread, self, name));
  if (result->isError()) {
    // TODO(T32775277) Implement PyErr_ExceptionMatches
    thread->clearPendingException();
    return Bool::falseObj();
  }
  return Bool::trueObj();
}

RawObject builtinSetattr(Thread* thread, Frame* frame, word nargs) {
  if (nargs != 3) {
    return thread->raiseTypeErrorWithCStr("setattr expected 3 arguments.");
  }
  Arguments args(frame, nargs);
  HandleScope scope(thread);
  Object self(&scope, args.get(0));
  Object name(&scope, args.get(1));
  Object value(&scope, args.get(2));
  if (!name->isStr()) {
    return thread->raiseTypeErrorWithCStr(
        "setattr(): attribute name must be string.");
  }
  Object result(&scope,
                thread->runtime()->attributeAtPut(thread, self, name, value));
  if (result->isError()) {
    // populate the exception
    return *result;
  }
  return NoneType::object();
}

}  // namespace python
