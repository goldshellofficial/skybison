#include "trampolines.h"

#include "frame.h"
#include "globals.h"
#include "handles.h"
#include "interpreter.h"
#include "objects.h"
#include "runtime.h"
#include "thread.h"
#include "tuple-builtins.h"

namespace python {

// Populate the free variable and cell variable arguments.
void processFreevarsAndCellvars(Thread* thread, const Function& function,
                                Frame* callee_frame) {
  CHECK(function.hasFreevarsOrCellvars(),
        "no free variables or cell variables");

  // initialize cell variables
  HandleScope scope(thread);
  Code code(&scope, function.code());
  Runtime* runtime = thread->runtime();
  word num_locals = code.nlocals();
  word num_cellvars = code.numCellvars();
  for (word i = 0; i < code.numCellvars(); i++) {
    ValueCell value_cell(&scope, runtime->newValueCell());

    // Allocate a cell for a local variable if cell2arg is not preset
    if (code.cell2arg().isNoneType()) {
      callee_frame->setLocal(num_locals + i, *value_cell);
      continue;
    }

    // Allocate a cell for a local variable if cell2arg is present but
    // the cell does not match any argument
    Object arg_index(&scope, Tuple::cast(code.cell2arg()).at(i));
    if (arg_index.isNoneType()) {
      callee_frame->setLocal(num_locals + i, *value_cell);
      continue;
    }

    // Allocate a cell for an argument
    word local_idx = Int::cast(*arg_index).asWord();
    value_cell.setValue(callee_frame->local(local_idx));
    callee_frame->setLocal(local_idx, NoneType::object());
    callee_frame->setLocal(num_locals + i, *value_cell);
  }

  // initialize free variables
  DCHECK(code.numFreevars() == 0 ||
             code.numFreevars() == Tuple::cast(function.closure()).length(),
         "Number of freevars is different than the closure.");
  for (word i = 0; i < code.numFreevars(); i++) {
    callee_frame->setLocal(num_locals + num_cellvars + i,
                           Tuple::cast(function.closure()).at(i));
  }
}

RawObject processDefaultArguments(Thread* thread, RawFunction function_raw,
                                  Frame* caller, const word argc) {
  HandleScope scope(thread);
  Function function(&scope, function_raw);
  Object tmp_varargs(&scope, NoneType::object());
  word new_argc = argc;
  if (new_argc < function.argcount() && function.hasDefaults()) {
    // Add default positional args
    Tuple default_args(&scope, function.defaults());
    if (default_args.length() < (function.argcount() - new_argc)) {
      return thread->raiseWithFmt(
          LayoutId::kTypeError,
          "TypeError: '%F' takes min %w positional arguments but %w given",
          &function, function.argcount() - default_args.length(), argc);
    }
    const word positional_only = function.argcount() - default_args.length();
    for (; new_argc < function.argcount(); new_argc++) {
      caller->pushValue(default_args.at(new_argc - positional_only));
    }
  }
  Runtime* runtime = thread->runtime();
  if ((new_argc > function.argcount()) || function.hasVarargs()) {
    // VARARGS - spill extra positional args into the varargs tuple.
    if (function.hasVarargs()) {
      word len = Utils::maximum(word{0}, new_argc - function.argcount());
      Tuple varargs(&scope, runtime->newTuple(len));
      for (word i = (len - 1); i >= 0; i--) {
        varargs.atPut(i, caller->topValue());
        caller->popValue();
        new_argc--;
      }
      tmp_varargs = *varargs;
    } else {
      return thread->raiseWithFmt(
          LayoutId::kTypeError,
          "TypeError: '%F' takes max %w positional arguments but %w given",
          &function, function.argcount(), argc);
    }
  }

  // If there are any keyword-only args, there must be defaults for them
  // because we arrived here via CALL_FUNCTION (and thus, no keywords were
  // supplied at the call site).
  Code code(&scope, function.code());
  if (code.kwonlyargcount() != 0 && !function.kwDefaults().isNoneType()) {
    Dict kw_defaults(&scope, function.kwDefaults());
    if (!kw_defaults.isNoneType()) {
      Tuple formal_names(&scope, code.varnames());
      word first_kw = function.argcount();
      for (word i = 0; i < code.kwonlyargcount(); i++) {
        Object name(&scope, formal_names.at(first_kw + i));
        RawObject val = runtime->dictAt(thread, kw_defaults, name);
        if (!val.isError()) {
          caller->pushValue(val);
          new_argc++;
        } else {
          return thread->raiseWithFmt(
              LayoutId::kTypeError, "TypeError: missing keyword-only argument");
        }
      }
    } else {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "TypeError: missing keyword-only argument");
    }
  }

  if (function.hasVarargs()) {
    caller->pushValue(*tmp_varargs);
    new_argc++;
  }

  if (function.hasVarkeyargs()) {
    // VARKEYARGS - because we arrived via CALL_FUNCTION, no keyword arguments
    // provided.  Just add an empty dict.
    Object kwdict(&scope, runtime->newDict());
    caller->pushValue(*kwdict);
    new_argc++;
  }

  // At this point, we should have the correct number of arguments.  Throw if
  // not.
  if (new_argc != function.totalArgs()) {
    return thread->raiseWithFmt(
        LayoutId::kTypeError,
        "TypeError: '%F' takes %w positional arguments but %w given", &function,
        function.argcount(),
        new_argc - function.hasVarargs() - function.hasVarkeyargs());
  }
  return *function;
}

// Verify correct number and order of arguments.  If order is wrong, try to
// fix it.  If argument is missing (denoted by Error::object()), try to supply
// it with a default.  This routine expects the number of args on the stack
// and number of names in the actual_names tuple to match.  Caller must pad
// prior to calling to ensure this.
// Return None::object() if successful, error object if not.
static RawObject checkArgs(Thread* thread, const Function& function,
                           RawObject* kw_arg_base, const Tuple& actual_names,
                           const Tuple& formal_names, word start) {
  word posonlyargcount = RawCode::cast(function.code()).posonlyargcount();
  word num_actuals = actual_names.length();
  // Helper function to swap actual arguments and names
  auto swap = [&kw_arg_base, &actual_names](word arg_pos1,
                                            word arg_pos2) -> void {
    RawObject tmp = *(kw_arg_base - arg_pos1);
    *(kw_arg_base - arg_pos1) = *(kw_arg_base - arg_pos2);
    *(kw_arg_base - arg_pos2) = tmp;
    tmp = actual_names.at(arg_pos1);
    actual_names.atPut(arg_pos1, actual_names.at(arg_pos2));
    actual_names.atPut(arg_pos2, tmp);
  };
  // Helper function to retrieve argument
  auto arg_at = [&kw_arg_base](word idx) -> RawObject& {
    return *(kw_arg_base - idx);
  };
  HandleScope scope(thread);
  Object formal_name(&scope, NoneType::object());
  for (word arg_pos = 0; arg_pos < num_actuals; arg_pos++) {
    word formal_pos = arg_pos + start;
    formal_name = formal_names.at(formal_pos);
    if (actual_names.at(arg_pos) == formal_name) {
      if (formal_pos >= posonlyargcount) {
        // We're good here: actual & formal arg names match.  Check the next
        // one.
        continue;
      }
      // A matching keyword arg but for a positional-only parameter.
      return Thread::current()->raiseWithFmt(
          LayoutId::kTypeError,
          "TypeError: keyword argument specified for positional-only argument "
          "'%S'",
          &formal_name);
    }
    // Mismatch.  Try to fix it.  Note: args grow down.
    bool swapped = false;
    // Look for expected Formal name in Actuals tuple.
    for (word i = arg_pos + 1; i < num_actuals; i++) {
      if (actual_names.at(i) == formal_name) {
        // Found it.  Swap both the stack and the actual_names tuple.
        swap(arg_pos, i);
        swapped = true;
        break;
      }
    }
    if (swapped) {
      // We managed to fix it.  Check the next one.
      continue;
    }
    // Can't find an Actual for this Formal.
    // If we have a real actual in current slot, move it somewhere safe.
    if (!arg_at(arg_pos).isError()) {
      for (word i = arg_pos + 1; i < num_actuals; i++) {
        if (arg_at(i).isError()) {
          // Found an uninitialized slot.  Use it to save current actual.
          swap(arg_pos, i);
          break;
        }
      }
      // If we were unable to find a slot to swap into, TypeError
      if (!arg_at(arg_pos).isError()) {
        // TODO(T40470525): Print out qualname and formal name in error message
        return thread->raiseWithFmt(
            LayoutId::kTypeError,
            "TypeError: invalid keyword argument supplied");
      }
    }
    // Now, can we fill that slot with a default argument?
    word absolute_pos = arg_pos + start;
    word argcount = function.argcount();
    if (absolute_pos < argcount) {
      word defaults_size = function.hasDefaults()
                               ? Tuple::cast(function.defaults()).length()
                               : 0;
      word defaults_start = argcount - defaults_size;
      if (absolute_pos >= (defaults_start)) {
        // Set the default value
        Tuple default_args(&scope, function.defaults());
        *(kw_arg_base - arg_pos) =
            default_args.at(absolute_pos - defaults_start);
        continue;  // Got it, move on to the next
      }
    } else if (!function.kwDefaults().isNoneType()) {
      // How about a kwonly default?
      Dict kw_defaults(&scope, function.kwDefaults());
      Object name(&scope, formal_names.at(arg_pos + start));
      RawObject val = thread->runtime()->dictAt(thread, kw_defaults, name);
      if (!val.isError()) {
        *(kw_arg_base - arg_pos) = val;
        continue;  // Got it, move on to the next
      }
    }
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "TypeError: missing argument");
  }
  return NoneType::object();
}

static word findName(word posonlyargcount, RawObject name, RawTuple name_list) {
  word len = name_list.length();
  for (word i = posonlyargcount; i < len; i++) {
    if (name == name_list.at(i)) {
      return i;
    }
  }
  return len;
}

// Converts the outgoing arguments of a keyword call into positional arguments
// and processes default arguments, rearranging everything into a form expected
// by the callee.
RawObject prepareKeywordCall(Thread* thread, RawFunction function_raw,
                             Frame* caller, word argc) {
  HandleScope scope(thread);
  Function function(&scope, function_raw);
  // Destructively pop the tuple of kwarg names
  Tuple keywords(&scope, caller->topValue());
  caller->popValue();
  Code code(&scope, function.code());
  word expected_args = function.argcount() + code.kwonlyargcount();
  word num_keyword_args = keywords.length();
  word num_positional_args = argc - num_keyword_args;
  Tuple formal_parm_names(&scope, code.varnames());
  Object tmp_varargs(&scope, NoneType::object());
  Object tmp_dict(&scope, NoneType::object());

  // We expect use of keyword argument calls to be uncommon, but when used
  // we anticipate mostly use of simple forms.  General scheme here is to
  // normalize the odd forms into standard form and then handle them all
  // in the same place.
  if (function.hasVarargsOrVarkeyargs()) {
    Runtime* runtime = thread->runtime();
    if (function.hasVarargs()) {
      // If we have more positional than expected, add the remainder to a tuple,
      // remove from the stack and close up the hole.
      word excess =
          Utils::maximum<word>(0, num_positional_args - function.argcount());
      Tuple varargs(&scope, runtime->newTuple(excess));
      if (excess > 0) {
        // Point to the leftmost excess argument
        RawObject* p =
            (caller->valueStackTop() + num_keyword_args + excess) - 1;
        // Copy the excess to the * tuple
        for (word i = 0; i < excess; i++) {
          varargs.atPut(i, *(p - i));
        }
        // Fill in the hole
        for (word i = 0; i < num_keyword_args; i++) {
          *p = *(p - excess);
          p--;
        }
        // Adjust the counts
        caller->dropValues(excess);
        argc -= excess;
        num_positional_args -= excess;
      }
      tmp_varargs = *varargs;
    }
    if (function.hasVarkeyargs()) {
      // Too many positional args passed?
      if (num_positional_args > function.argcount()) {
        return thread->raiseWithFmt(LayoutId::kTypeError,
                                    "TypeError: Too many positional arguments");
      }
      // If we have keyword arguments that don't appear in the formal parameter
      // list, add them to a keyword dict.
      Dict dict(&scope, runtime->newDict());
      List saved_keyword_list(&scope, runtime->newList());
      List saved_values(&scope, runtime->newList());
      word formal_parm_size = formal_parm_names.length();
      RawObject* p = caller->valueStackTop() + (num_keyword_args - 1);
      word posonlyargcount = code.posonlyargcount();
      for (word i = 0; i < num_keyword_args; i++) {
        Object key(&scope, keywords.at(i));
        Object value(&scope, *(p - i));
        if (findName(posonlyargcount, *key, *formal_parm_names) <
            formal_parm_size) {
          // Got a match, stash pair for future restoration on the stack
          runtime->listAdd(thread, saved_keyword_list, key);
          runtime->listAdd(thread, saved_values, value);
        } else {
          // New, add it and associated value to the varkeyargs dict
          runtime->dictAtPut(thread, dict, key, value);
          argc--;
        }
      }
      // Now, restore the stashed values to the stack and build a new
      // keywords name list.
      caller->dropValues(
          num_keyword_args);  // Pop all of the old keyword values
      num_keyword_args = saved_keyword_list.numItems();
      // Replace the old keywords list with a new one.
      keywords = runtime->newTuple(num_keyword_args);
      for (word i = 0; i < num_keyword_args; i++) {
        caller->pushValue(saved_values.at(i));
        keywords.atPut(i, saved_keyword_list.at(i));
      }
      tmp_dict = *dict;
    }
  }
  // At this point, all vararg forms have been normalized
  RawObject* kw_arg_base = (caller->valueStackTop() + num_keyword_args) -
                           1;  // pointer to first non-positional arg
  if (UNLIKELY(argc > expected_args)) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "TypeError: Too many arguments");
  }
  if (UNLIKELY(argc < expected_args)) {
    // Too few args passed.  Can we supply default args to make it work?
    // First, normalize & pad keywords and stack arguments
    word name_tuple_size = expected_args - num_positional_args;
    Tuple padded_keywords(&scope, thread->runtime()->newTuple(name_tuple_size));
    for (word i = 0; i < num_keyword_args; i++) {
      padded_keywords.atPut(i, keywords.at(i));
    }
    // Fill in missing spots w/ Error code
    for (word i = num_keyword_args; i < name_tuple_size; i++) {
      caller->pushValue(Error::error());
      padded_keywords.atPut(i, Error::error());
    }
    keywords = *padded_keywords;
  }
  // Now we've got the right number.  Do they match up?
  RawObject res = checkArgs(thread, function, kw_arg_base, keywords,
                            formal_parm_names, num_positional_args);
  if (res.isError()) {
    return res;  // TypeError created by checkArgs.
  }
  CHECK(res.isNoneType(), "checkArgs should return an Error or None");
  // If we're a vararg form, need to push the tuple/dict.
  if (function.hasVarargs()) {
    caller->pushValue(*tmp_varargs);
  }
  if (function.hasVarkeyargs()) {
    caller->pushValue(*tmp_dict);
  }
  return *function;
}

// Converts explode arguments into positional arguments.
//
// Returns the new number of positional arguments as a SmallInt, or Error if an
// exception was raised (most likely due to a non-string keyword name).
static RawObject processExplodeArguments(Thread* thread, Frame* caller,
                                         word flags) {
  HandleScope scope(thread);
  Object kw_dict(&scope, NoneType::object());
  if (flags & CallFunctionExFlag::VAR_KEYWORDS) {
    kw_dict = caller->topValue();
    caller->popValue();
  }
  Tuple positional_args(&scope, caller->popValue());
  word argc = positional_args.length();
  for (word i = 0; i < argc; i++) {
    caller->pushValue(positional_args.at(i));
  }
  Runtime* runtime = thread->runtime();
  if (flags & CallFunctionExFlag::VAR_KEYWORDS) {
    Dict dict(&scope, *kw_dict);
    Tuple keys(&scope, runtime->dictKeys(thread, dict));
    for (word i = 0; i < keys.length(); i++) {
      Object key(&scope, keys.at(i));
      if (!thread->runtime()->isInstanceOfStr(*key)) {
        return thread->raiseWithFmt(LayoutId::kTypeError,
                                    "keywords must be strings");
      }
      caller->pushValue(runtime->dictAt(thread, dict, key));
    }
    argc += keys.length();
    caller->pushValue(*keys);
  }
  return SmallInt::fromWord(argc);
}

// Takes the outgoing arguments of an explode argument call and rearranges them
// into the form expected by the callee.
RawObject prepareExplodeCall(Thread* thread, RawFunction function_raw,
                             Frame* caller, word flags) {
  HandleScope scope(thread);
  Function function(&scope, function_raw);

  RawObject arg_obj = processExplodeArguments(thread, caller, flags);
  if (arg_obj.isError()) return arg_obj;
  word new_argc = SmallInt::cast(arg_obj).value();

  if (flags & CallFunctionExFlag::VAR_KEYWORDS) {
    RawObject result = prepareKeywordCall(thread, *function, caller, new_argc);
    if (result.isError()) {
      return result;
    }
  } else {
    // Are we one of the less common cases?
    if (new_argc != function.argcount() || !(function.hasSimpleCall())) {
      RawObject result =
          processDefaultArguments(thread, *function, caller, new_argc);
      if (result.isError()) {
        return result;
      }
    }
  }
  return *function;
}

static RawObject createGeneratorObject(Runtime* runtime,
                                       const Function& function) {
  if (function.isGenerator()) return runtime->newGenerator();
  if (function.isCoroutine()) return runtime->newCoroutine();
  DCHECK(function.isAsyncGenerator(), "unexpected type");
  return runtime->newAsyncGenerator();
}

static RawObject createGenerator(Thread* thread, const Function& function,
                                 const Str& qualname) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  GeneratorBase gen_base(&scope, createGeneratorObject(runtime, function));
  gen_base.setHeapFrame(runtime->newHeapFrame(function));
  gen_base.setExceptionState(runtime->newExceptionState());
  gen_base.setQualname(*qualname);
  runtime->genSave(thread, gen_base);
  thread->popFrame();
  return *gen_base;
}

RawObject generatorTrampoline(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Function function(&scope, caller->peek(argc));
  RawObject error = preparePositionalCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  thread->pushCallFrame(*function);
  Str qualname(&scope, function.qualname());
  return createGenerator(thread, function, qualname);
}

RawObject generatorTrampolineKw(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  // The argument does not include the hidden keyword dictionary argument.  Add
  // one to skip over the keyword dictionary to read the function object.
  Function function(&scope, caller->peek(argc + 1));
  RawObject error = prepareKeywordCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  thread->pushCallFrame(*function);
  Str qualname(&scope, function.qualname());
  return createGenerator(thread, function, qualname);
}

RawObject generatorTrampolineEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  // The argument is either zero when there is one argument and one when there
  // are two arguments.  Skip over these arguments to read the function object.
  Function function(
      &scope, caller->peek((flags & CallFunctionExFlag::VAR_KEYWORDS) ? 2 : 1));
  RawObject error = prepareExplodeCall(thread, *function, caller, flags);
  if (error.isError()) {
    return error;
  }
  thread->pushCallFrame(*function);
  Str qualname(&scope, function.qualname());
  return createGenerator(thread, function, qualname);
}

RawObject generatorClosureTrampoline(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Function function(&scope, caller->peek(argc));
  RawObject error = preparePositionalCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  Frame* callee_frame = thread->pushCallFrame(*function);
  processFreevarsAndCellvars(thread, function, callee_frame);
  Str qualname(&scope, function.qualname());
  return createGenerator(thread, function, qualname);
}

RawObject generatorClosureTrampolineKw(Thread* thread, Frame* caller,
                                       word argc) {
  HandleScope scope(thread);
  // The argument does not include the hidden keyword dictionary argument.  Add
  // one to skip the keyword dictionary to get to the function object.
  Function function(&scope, caller->peek(argc + 1));
  RawObject error = prepareKeywordCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  Frame* callee_frame = thread->pushCallFrame(*function);
  processFreevarsAndCellvars(thread, function, callee_frame);
  Str qualname(&scope, function.qualname());
  return createGenerator(thread, function, qualname);
}

RawObject generatorClosureTrampolineEx(Thread* thread, Frame* caller,
                                       word flags) {
  HandleScope scope(thread);
  // The argument is either zero when there is one argument and one when there
  // are two arguments.  Skip over these arguments to read the function object.
  Function function(
      &scope, caller->peek((flags & CallFunctionExFlag::VAR_KEYWORDS) ? 2 : 1));
  RawObject error = prepareExplodeCall(thread, *function, caller, flags);
  if (error.isError()) {
    return error;
  }
  Frame* callee_frame = thread->pushCallFrame(*function);
  processFreevarsAndCellvars(thread, function, callee_frame);
  Str qualname(&scope, function.qualname());
  return createGenerator(thread, function, qualname);
}

RawObject interpreterTrampoline(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Function function(&scope, caller->peek(argc));
  RawObject error = preparePositionalCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  thread->pushCallFrame(*function);
  return Interpreter::execute(thread);
}

RawObject interpreterTrampolineKw(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  // The argument does not include the hidden keyword dictionary argument.  Add
  // one to skip the keyword dictionary to get to the function object.
  Function function(&scope, caller->peek(argc + 1));
  RawObject error = prepareKeywordCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  thread->pushCallFrame(*function);
  return Interpreter::execute(thread);
}

RawObject interpreterTrampolineEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  // The argument is either zero when there is one argument and one when there
  // are two arguments.  Skip over these arguments to read the function object.
  Function function(
      &scope, caller->peek((flags & CallFunctionExFlag::VAR_KEYWORDS) ? 2 : 1));
  RawObject error = prepareExplodeCall(thread, *function, caller, flags);
  if (error.isError()) {
    return error;
  }
  thread->pushCallFrame(*function);
  return Interpreter::execute(thread);
}

RawObject interpreterClosureTrampoline(Thread* thread, Frame* caller,
                                       word argc) {
  HandleScope scope(thread);
  Function function(&scope, caller->peek(argc));
  RawObject error = preparePositionalCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  Frame* callee_frame = thread->pushCallFrame(*function);
  processFreevarsAndCellvars(thread, function, callee_frame);
  return Interpreter::execute(thread);
}

RawObject interpreterClosureTrampolineKw(Thread* thread, Frame* caller,
                                         word argc) {
  HandleScope scope(thread);
  // The argument does not include the hidden keyword dictionary argument.  Add
  // one to skip the keyword dictionary to get to the function object.
  Function function(&scope, caller->peek(argc + 1));
  RawObject error = prepareKeywordCall(thread, *function, caller, argc);
  if (error.isError()) {
    return error;
  }
  Frame* callee_frame = thread->pushCallFrame(*function);
  processFreevarsAndCellvars(thread, function, callee_frame);
  return Interpreter::execute(thread);
}

RawObject interpreterClosureTrampolineEx(Thread* thread, Frame* caller,
                                         word flags) {
  HandleScope scope(thread);
  // The argument is either zero when there is one argument and one when there
  // are two arguments.  Skip over these arguments to read the function object.
  Function function(
      &scope, caller->peek((flags & CallFunctionExFlag::VAR_KEYWORDS) ? 2 : 1));
  RawObject error = prepareExplodeCall(thread, *function, caller, flags);
  if (error.isError()) {
    return error;
  }
  Frame* callee_frame = thread->pushCallFrame(*function);
  processFreevarsAndCellvars(thread, function, callee_frame);
  return Interpreter::execute(thread);
}

// method no args

static RawObject callMethNoArgs(Thread* thread, const Function& function,
                                const Object& self) {
  HandleScope scope(thread);
  Int address(&scope, function.code());
  binaryfunc method = bit_cast<binaryfunc>(address.asCPtr());
  PyObject* self_obj = ApiHandle::borrowedReference(thread, *self);
  PyObject* result = (*method)(self_obj, nullptr);
  if (result != nullptr) return ApiHandle::fromPyObject(result)->asObject();
  if (thread->hasPendingException()) return Error::exception();
  return thread->raiseWithFmt(LayoutId::kSystemError,
                              "NULL return without exception set");
}

RawObject methodTrampolineNoArgs(Thread* thread, Frame* caller, word argc) {
  if (argc != 1) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no arguments");
  }
  HandleScope scope(thread);
  Function function(&scope, caller->peek(1));
  Object self(&scope, caller->peek(0));
  return callMethNoArgs(thread, function, self);
}

RawObject methodTrampolineNoArgsKw(Thread* thread, Frame* caller, word argc) {
  if (argc != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no keyword arguments");
  }
  HandleScope scope(thread);
  Function function(&scope, caller->peek(1));
  Object self(&scope, caller->peek(0));
  return callMethNoArgs(thread, function, self);
}

RawObject methodTrampolineNoArgsEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  Tuple varargs(&scope, caller->peek(has_varkeywords));
  if (varargs.length() != 1) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no arguments");
  }
  if (has_varkeywords) {
    Object kw_args(&scope, caller->topValue());
    if (!kw_args.isDict()) UNIMPLEMENTED("mapping kwargs");
    if (Dict::cast(*kw_args).numItems() != 0) {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "function takes no keyword arguments");
    }
  }
  Function function(&scope, caller->peek(has_varkeywords + 1));
  Object self(&scope, varargs.at(0));
  return callMethNoArgs(thread, function, self);
}

// method one arg

static RawObject callMethOneArg(Thread* thread, const Function& function,
                                const Object& self, const Object& arg) {
  HandleScope scope(thread);
  Int address(&scope, function.code());
  binaryfunc method = bit_cast<binaryfunc>(address.asCPtr());
  PyObject* self_obj = ApiHandle::borrowedReference(thread, *self);
  PyObject* arg_obj = ApiHandle::borrowedReference(thread, *arg);
  PyObject* result = (*method)(self_obj, arg_obj);
  if (result != nullptr) return ApiHandle::fromPyObject(result)->asObject();
  if (thread->hasPendingException()) return Error::exception();
  return thread->raiseWithFmt(LayoutId::kSystemError,
                              "NULL return without exception set");
}

RawObject methodTrampolineOneArg(Thread* thread, Frame* caller, word argc) {
  if (argc != 2) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes exactly one argument");
  }
  HandleScope scope(thread);
  Function function(&scope, caller->peek(2));
  Object self(&scope, caller->peek(1));
  Object arg(&scope, caller->peek(0));
  return callMethOneArg(thread, function, self, arg);
}

RawObject methodTrampolineOneArgKw(Thread* thread, Frame* caller, word argc) {
  if (argc != 2) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes exactly two arguments");
  }
  HandleScope scope(thread);
  Tuple kwargs(&scope, caller->peek(0));
  if (kwargs.length() != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no keyword arguments");
  }
  Function function(&scope, caller->peek(3));
  Object self(&scope, caller->peek(1));
  Object arg(&scope, caller->peek(2));
  return callMethOneArg(thread, function, self, arg);
}

RawObject methodTrampolineOneArgEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  if (has_varkeywords) {
    Object kw_args(&scope, caller->topValue());
    if (!kw_args.isDict()) UNIMPLEMENTED("mapping kwargs");
    if (Dict::cast(*kw_args).numItems() != 0) {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "function takes no keyword arguments");
    }
  }
  Tuple varargs(&scope, caller->peek(has_varkeywords));
  if (varargs.length() != 2) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes exactly two argument");
  }
  Object self(&scope, varargs.at(0));
  Object arg(&scope, varargs.at(1));
  Function function(&scope, caller->peek(has_varkeywords + 1));
  return callMethOneArg(thread, function, self, arg);
}

// callMethVarArgs

static RawObject callMethVarArgs(Thread* thread, const Function& function,
                                 const Object& self, const Object& varargs) {
  HandleScope scope(thread);
  Int address(&scope, function.code());
  binaryfunc method = bit_cast<binaryfunc>(address.asCPtr());
  PyObject* self_obj = ApiHandle::borrowedReference(thread, *self);
  PyObject* varargs_obj = ApiHandle::borrowedReference(thread, *varargs);
  PyObject* result = (*method)(self_obj, varargs_obj);
  if (result != nullptr) return ApiHandle::fromPyObject(result)->asObject();
  if (thread->hasPendingException()) return Error::exception();
  return thread->raiseWithFmt(LayoutId::kSystemError,
                              "NULL return without exception set");
}

RawObject methodTrampolineVarArgs(Thread* thread, Frame* caller, word argc) {
  if (argc < 1) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes at least one arguments");
  }
  HandleScope scope(thread);
  Function function(&scope, caller->peek(argc));
  Object self(&scope, caller->peek(argc - 1));
  Tuple varargs(&scope, thread->runtime()->newTuple(argc - 1));
  for (word i = 0; i < argc - 1; i++) {
    varargs.atPut(argc - i - 2, caller->peek(i));
  }
  return callMethVarArgs(thread, function, self, varargs);
}

RawObject methodTrampolineVarArgsKw(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Tuple kwargs(&scope, caller->peek(0));
  if (kwargs.length() != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no keyword arguments");
  }
  Function function(&scope, caller->peek(argc + 1));
  Object self(&scope, caller->peek(argc - 1));
  Tuple varargs(&scope, thread->runtime()->newTuple(argc));
  for (word i = 1; i < argc; i++) {
    varargs.atPut(argc - i - 1, caller->peek(i + 1));
  }
  return callMethVarArgs(thread, function, self, varargs);
}

RawObject methodTrampolineVarArgsEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  if (has_varkeywords) {
    Object kw_args(&scope, caller->topValue());
    if (!kw_args.isDict()) UNIMPLEMENTED("mapping kwargs");
    if (Dict::cast(*kw_args).numItems() != 0) {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "function takes no keyword arguments");
    }
  }
  Function function(&scope, caller->peek(has_varkeywords + 1));
  Tuple varargs(&scope, caller->peek(has_varkeywords));
  Object self(&scope, varargs.at(0));
  Object args(&scope, thread->runtime()->tupleSubseq(thread, varargs, 1,
                                                     varargs.length() - 1));
  return callMethVarArgs(thread, function, self, args);
}

// callMethKeywordArgs

static RawObject callMethKeywords(Thread* thread, const Function& function,
                                  const Object& self, const Object& args,
                                  const Object& kwargs) {
  HandleScope scope(thread);
  Int address(&scope, function.code());
  ternaryfunc method = bit_cast<ternaryfunc>(address.asCPtr());
  PyObject* self_obj = ApiHandle::borrowedReference(thread, *self);
  PyObject* args_obj = ApiHandle::borrowedReference(thread, *args);
  PyObject* kwargs_obj = nullptr;
  if (*kwargs != NoneType::object()) {
    kwargs_obj = ApiHandle::borrowedReference(thread, *kwargs);
  }
  PyObject* result = (*method)(self_obj, args_obj, kwargs_obj);
  if (result != nullptr) return ApiHandle::fromPyObject(result)->asObject();
  if (thread->hasPendingException()) return Error::exception();
  return thread->raiseWithFmt(LayoutId::kSystemError,
                              "NULL return without exception set");
}

RawObject methodTrampolineKeywords(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Function function(&scope, caller->peek(argc));
  Object self(&scope, caller->peek(argc - 1));
  Tuple varargs(&scope, runtime->newTuple(argc - 1));
  for (word i = 0; i < argc - 1; i++) {
    varargs.atPut(argc - i - 2, caller->peek(i));
  }
  Object keywords(&scope, NoneType::object());
  return callMethKeywords(thread, function, self, varargs, keywords);
}

RawObject methodTrampolineKeywordsKw(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Tuple kw_names(&scope, caller->peek(0));
  Object kwargs(&scope, NoneType::object());
  word num_keywords = kw_names.length();
  if (num_keywords != 0) {
    Dict dict(&scope, runtime->newDict());
    for (word i = 0; i < num_keywords; i++) {
      Object key(&scope, kw_names.at(i));
      Object value(&scope, caller->peek(num_keywords - i));
      runtime->dictAtPut(thread, dict, key, value);
    }
    kwargs = *dict;
  }
  word num_positional = argc - num_keywords - 1;
  Tuple args(&scope, runtime->newTuple(num_positional));
  for (word i = 0; i < num_positional; i++) {
    args.atPut(i, caller->peek(argc - i - 1));
  }
  Function function(&scope, caller->peek(argc + 1));
  Object self(&scope, caller->peek(argc));
  return callMethKeywords(thread, function, self, args, kwargs);
}

RawObject methodTrampolineKeywordsEx(Thread* thread, Frame* caller,
                                     word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  Tuple varargs(&scope, caller->peek(has_varkeywords));
  Object kwargs(&scope, NoneType::object());
  if (has_varkeywords) {
    kwargs = caller->topValue();
    if (!kwargs.isDict()) UNIMPLEMENTED("mapping kwargs");
  }
  Function function(&scope, caller->peek(has_varkeywords + 1));
  Object self(&scope, varargs.at(0));
  Object args(&scope, thread->runtime()->tupleSubseq(thread, varargs, 1,
                                                     varargs.length() - 1));
  return callMethKeywords(thread, function, self, args, kwargs);
}

RawObject moduleTrampolineNoArgs(Thread* thread, Frame* caller, word argc) {
  if (argc != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no arguments");
  }
  HandleScope scope(thread);
  Function function(&scope, caller->peek(0));
  Object module(&scope, function.module());
  return callMethNoArgs(thread, function, module);
}

RawObject moduleTrampolineNoArgsKw(Thread* thread, Frame* caller, word argc) {
  if (argc != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no keyword arguments");
  }
  HandleScope scope(thread);
  Function function(&scope, caller->peek(1));
  Object module(&scope, function.module());
  return callMethNoArgs(thread, function, module);
}

RawObject moduleTrampolineNoArgsEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  Tuple varargs(&scope, caller->peek(has_varkeywords));
  if (varargs.length() != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no arguments");
  }
  if (has_varkeywords) {
    Object kw_args(&scope, caller->topValue());
    if (!kw_args.isDict()) UNIMPLEMENTED("mapping kwargs");
    if (Dict::cast(*kw_args).numItems() != 0) {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "function takes no keyword arguments");
    }
  }
  Function function(&scope, caller->peek(has_varkeywords + 1));
  Object module(&scope, function.module());
  return callMethNoArgs(thread, function, module);
}

RawObject moduleTrampolineOneArg(Thread* thread, Frame* caller, word argc) {
  if (argc != 1) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes exactly one argument");
  }
  HandleScope scope(thread);
  Object arg(&scope, caller->peek(0));
  Function function(&scope, caller->peek(1));
  Object module(&scope, function.module());
  return callMethOneArg(thread, function, module, arg);
}

RawObject moduleTrampolineOneArgKw(Thread* thread, Frame* caller, word argc) {
  if (argc != 1) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes exactly one argument");
  }
  HandleScope scope(thread);
  Tuple kwargs(&scope, caller->peek(0));
  if (kwargs.length() != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no keyword arguments");
  }
  Object arg(&scope, caller->peek(1));
  Function function(&scope, caller->peek(2));
  Object module(&scope, function.module());
  return callMethOneArg(thread, function, module, arg);
}

RawObject moduleTrampolineOneArgEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  if (has_varkeywords) {
    Object kw_args(&scope, caller->topValue());
    if (!kw_args.isDict()) UNIMPLEMENTED("mapping kwargs");
    if (Dict::cast(*kw_args).numItems() != 0) {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "function takes no keyword arguments");
    }
  }
  Tuple varargs(&scope, caller->peek(has_varkeywords));
  if (varargs.length() != 1) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes exactly one argument");
  }
  Object arg(&scope, varargs.at(0));
  Function function(&scope, caller->peek(has_varkeywords + 1));
  Object module(&scope, function.module());
  return callMethOneArg(thread, function, module, arg);
}

RawObject moduleTrampolineVarArgs(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Function function(&scope, caller->peek(argc));
  Object module(&scope, function.module());
  Tuple varargs(&scope, thread->runtime()->newTuple(argc));
  for (word i = 0; i < argc; i++) {
    varargs.atPut(argc - i - 1, caller->peek(i));
  }
  return callMethVarArgs(thread, function, module, varargs);
}

RawObject moduleTrampolineVarArgsKw(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Tuple kwargs(&scope, caller->peek(0));
  if (kwargs.length() != 0) {
    return thread->raiseWithFmt(LayoutId::kTypeError,
                                "function takes no keyword arguments");
  }
  Function function(&scope, caller->peek(argc + 1));
  Object module(&scope, function.module());
  Tuple varargs(&scope, thread->runtime()->newTuple(argc));
  for (word i = 0; i < argc; i++) {
    varargs.atPut(argc - i - 1, caller->peek(i + 1));
  }
  return callMethVarArgs(thread, function, module, varargs);
}

RawObject moduleTrampolineVarArgsEx(Thread* thread, Frame* caller, word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  if (has_varkeywords) {
    Object kw_args(&scope, caller->topValue());
    if (!kw_args.isDict()) UNIMPLEMENTED("mapping kwargs");
    if (Dict::cast(*kw_args).numItems() != 0) {
      return thread->raiseWithFmt(LayoutId::kTypeError,
                                  "function takes no keyword arguments");
    }
  }
  Function function(&scope, caller->peek(has_varkeywords + 1));
  Object module(&scope, function.module());
  Object args(&scope, caller->peek(has_varkeywords));
  return callMethVarArgs(thread, function, module, args);
}

RawObject moduleTrampolineKeywords(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Function function(&scope, caller->peek(argc));
  Object module(&scope, function.module());
  Tuple args(&scope, runtime->newTuple(argc));
  for (word i = 0; i < argc; i++) {
    args.atPut(argc - i - 1, caller->peek(i));
  }
  Object kwargs(&scope, NoneType::object());
  return callMethKeywords(thread, function, module, args, kwargs);
}

RawObject moduleTrampolineKeywordsKw(Thread* thread, Frame* caller, word argc) {
  HandleScope scope(thread);
  Runtime* runtime = thread->runtime();
  Tuple kw_names(&scope, caller->peek(0));
  Object kwargs(&scope, NoneType::object());
  word num_keywords = kw_names.length();
  if (num_keywords != 0) {
    Dict dict(&scope, runtime->newDict());
    for (word i = 0; i < num_keywords; i++) {
      Object key(&scope, kw_names.at(i));
      Object value(&scope, caller->peek(num_keywords - i));
      runtime->dictAtPut(thread, dict, key, value);
    }
    kwargs = *dict;
  }
  word num_varargs = argc - num_keywords;
  Tuple args(&scope, runtime->newTuple(num_varargs));
  for (word i = 0; i < num_varargs; i++) {
    args.atPut(i, caller->peek(argc - i));
  }
  Function function(&scope, caller->peek(argc + 1));
  Object module(&scope, function.module());
  return callMethKeywords(thread, function, module, args, kwargs);
}

RawObject moduleTrampolineKeywordsEx(Thread* thread, Frame* caller,
                                     word flags) {
  HandleScope scope(thread);
  bool has_varkeywords = flags & CallFunctionExFlag::VAR_KEYWORDS;
  Object varargs(&scope, caller->peek(has_varkeywords));
  Object kwargs(&scope, NoneType::object());
  if (has_varkeywords) {
    kwargs = caller->topValue();
    if (!kwargs.isDict()) UNIMPLEMENTED("mapping kwargs");
  }
  Function function(&scope, caller->peek(has_varkeywords + 1));
  Object module(&scope, function.module());
  return callMethKeywords(thread, function, module, varargs, kwargs);
}

RawObject unimplementedTrampoline(Thread*, Frame*, word) {
  UNIMPLEMENTED("Trampoline");
}

static inline RawObject builtinTrampolineImpl(Thread* thread, Frame* caller,
                                              word arg, word function_idx,
                                              PrepareCallFunc prepare_call) {
  // Warning: This code is using `RawXXX` variables for performance! This is
  // despite the fact that we call functions that do potentially perform memory
  // allocations. This is legal here because we always rely on the functions
  // returning an up-to-date address and we make sure to never access any value
  // produce before a call after that call. Be careful not to break this
  // invariant if you change the code!

  RawObject prepare_result = prepare_call(
      thread, Function::cast(caller->peek(function_idx)), caller, arg);
  if (prepare_result.isError()) return prepare_result;
  RawFunction function = Function::cast(prepare_result);

  RawObject result = NoneType::object();
  {
    DCHECK(!function.code().isNoneType(),
           "builtin functions should have annotated code objects");
    RawCode code = Code::cast(function.code());
    DCHECK(code.code().isSmallInt(),
           "builtin functions should contain entrypoint in code.code");
    void* entry = SmallInt::cast(code.code()).asCPtr();

    word argc = function.totalArgs();
    Frame* frame = thread->pushNativeFrame(argc);
    result = bit_cast<Function::Entry>(entry)(thread, frame, argc);
    // End scope so people do not accidentally use raw variables after the call
    // which could have triggered a GC.
  }
  DCHECK(thread->isErrorValueOk(result), "error/exception mismatch");
  thread->popFrame();
  return result;
}

RawObject builtinTrampoline(Thread* thread, Frame* caller, word argc) {
  return builtinTrampolineImpl(thread, caller, argc, /*function_idx=*/argc,
                               preparePositionalCall);
}

RawObject builtinTrampolineKw(Thread* thread, Frame* caller, word argc) {
  return builtinTrampolineImpl(thread, caller, argc, /*function_idx=*/argc + 1,
                               prepareKeywordCall);
}

RawObject builtinTrampolineEx(Thread* thread, Frame* caller, word flags) {
  return builtinTrampolineImpl(
      thread, caller, flags,
      /*function_idx=*/(flags & CallFunctionExFlag::VAR_KEYWORDS) ? 2 : 1,
      prepareExplodeCall);
}

}  // namespace python
