#include "runtime.h"

#include <unistd.h>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

#include "bool-builtins.h"
#include "builtins-module.h"
#include "builtins.h"
#include "bytecode.h"
#include "callback.h"
#include "dict-builtins.h"
#include "float-builtins.h"
#include "frame.h"
#include "globals.h"
#include "handles.h"
#include "heap.h"
#include "int-builtins.h"
#include "interpreter.h"
#include "layout.h"
#include "list-builtins.h"
#include "marshal.h"
#include "os.h"
#include "ref-builtins.h"
#include "scavenger.h"
#include "set-builtins.h"
#include "siphash.h"
#include "str-builtins.h"
#include "super-builtins.h"
#include "sys-module.h"
#include "thread.h"
#include "time-module.h"
#include "trampolines-inl.h"
#include "tuple-builtins.h"
#include "utils.h"
#include "visitor.h"

namespace python {

Runtime::Runtime(word heap_size)
    : heap_(heap_size), new_value_cell_callback_(this) {
  initializeRandom();
  initializeThreads();
  // This must be called before initializeClasses is called. Methods in
  // initializeClasses rely on instances that are created in this method.
  initializePrimitiveInstances();
  initializeInterned();
  initializeSymbols();
  initializeClasses();
  initializeModules();
  initializeApiHandles();
}

Runtime::Runtime() : Runtime(64 * MiB) {}

Runtime::~Runtime() {
  deallocApiHandles();
  for (Thread* thread = threads_; thread != nullptr;) {
    if (thread == Thread::currentThread()) {
      Thread::setCurrentThread(nullptr);
    } else {
      UNIMPLEMENTED("threading");
    }
    auto prev = thread;
    thread = thread->next();
    delete prev;
  }
  for (void* ptr : apihandle_store_) {
    std::free(ptr);
  }
  delete symbols_;
}

Object* Runtime::newBoundMethod(
    const Handle<Object>& function,
    const Handle<Object>& self) {
  HandleScope scope;
  Handle<BoundMethod> bound_method(&scope, heap()->createBoundMethod());
  bound_method->setFunction(*function);
  bound_method->setSelf(*self);
  return *bound_method;
}

Object* Runtime::newLayout() {
  return newLayoutWithId(newLayoutId());
}

Object* Runtime::newLayoutWithId(word layout_id) {
  DCHECK(
      layout_id >= static_cast<word>(IntrinsicLayoutId::kObject) ||
          layout_id == IntrinsicLayoutId::kSmallInteger || (layout_id & 1) == 1,
      "kSmallInteger must be the only even immediate layout id");
  HandleScope scope;
  Handle<Layout> layout(&scope, heap()->createLayout(layout_id));
  layout->setNumInObjectAttributes(0);
  layout->setInObjectAttributes(empty_object_array_);
  layout->setOverflowAttributes(empty_object_array_);
  layout->setAdditions(newList());
  layout->setDeletions(newList());
  List::cast(layouts_)->atPut(layout_id, *layout);
  return *layout;
}

Object* Runtime::newByteArray(word length, byte fill) {
  DCHECK(length >= 0, "invalid length %ld", length);
  if (length == 0) {
    return empty_byte_array_;
  }
  Object* result = heap()->createByteArray(length);
  byte* dst = reinterpret_cast<byte*>(ByteArray::cast(result)->address());
  std::memset(dst, fill, length);
  return result;
}

Object* Runtime::newByteArrayWithAll(View<byte> array) {
  if (array.length() == 0) {
    return empty_byte_array_;
  }
  Object* result = heap()->createByteArray(array.length());
  byte* dst = reinterpret_cast<byte*>(ByteArray::cast(result)->address());
  std::memcpy(dst, array.data(), array.length());
  return result;
}

Object* Runtime::newClass() {
  HandleScope scope;
  Handle<Class> result(&scope, heap()->createClass());
  Handle<Dictionary> dict(&scope, newDictionary());
  result->setFlags(SmallInteger::fromWord(0));
  result->setDictionary(*dict);
  return *result;
}

Object* Runtime::classGetAttr(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name) {
  if (!name->isString()) {
    // TODO(T25140871): Refactor into something like:
    //     thread->throwUnexpectedTypeError(expected, actual)
    return thread->throwTypeErrorFromCString("attribute name must be a string");
  }

  HandleScope scope(thread);
  Handle<Class> klass(&scope, *receiver);
  Handle<Class> meta_klass(&scope, classOf(*receiver));

  // Look for the attribute in the meta class
  Handle<Object> meta_attr(&scope, lookupNameInMro(thread, meta_klass, name));
  if (isDataDescriptor(thread, meta_attr)) {
    // TODO(T25692531): Call __get__ from meta_attr
    UNIMPLEMENTED("custom descriptors are unsupported");
  }

  // No data descriptor found on the meta class, look in the mro of the klass
  Handle<Object> attr(&scope, lookupNameInMro(thread, klass, name));
  if (!attr->isError()) {
    if (isNonDataDescriptor(thread, attr)) {
      Handle<Object> instance(&scope, None::object());
      return Interpreter::callDescriptorGet(
          thread, thread->currentFrame(), attr, instance, receiver);
    }
    return *attr;
  }

  // No attr found in klass or its mro, use the non-data descriptor found in
  // the metaclass (if any).
  if (isNonDataDescriptor(thread, meta_attr)) {
    Handle<Object> owner(&scope, *meta_klass);
    return Interpreter::callDescriptorGet(
        thread, thread->currentFrame(), meta_attr, receiver, owner);
  }

  // If a regular attribute was found in the metaclass, return it
  if (!meta_attr->isError()) {
    return *meta_attr;
  }

  // TODO(T25140871): Refactor this into something like:
  //     thread->throwMissingAttributeError(name)
  return thread->throwAttributeErrorFromCString("missing attribute");
}

Object* Runtime::classSetAttr(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name,
    const Handle<Object>& value) {
  if (!name->isString()) {
    // TODO(T25140871): Refactor into something like:
    //     thread->throwUnexpectedTypeError(expected, actual)
    return thread->throwTypeErrorFromCString("attribute name must be a string");
  }

  HandleScope scope(thread);
  Handle<Class> klass(&scope, *receiver);
  if (klass->isIntrinsicOrExtension()) {
    // TODO(T25140871): Refactor this into something that includes the type name
    // like:
    //     thread->throwImmutableTypeManipulationError(klass)
    return thread->throwTypeErrorFromCString(
        "can't set attributes of built-in/extension type");
  }

  // Check for a data descriptor
  Handle<Class> metaklass(&scope, classOf(*receiver));
  Handle<Object> meta_attr(&scope, lookupNameInMro(thread, metaklass, name));
  if (isDataDescriptor(thread, meta_attr)) {
    // TODO(T25692531): Call __set__ from meta_attr
    UNIMPLEMENTED("custom descriptors are unsupported");
  }

  // No data descriptor found, store the attribute in the klass dictionary
  Handle<Dictionary> klass_dict(&scope, klass->dictionary());
  dictionaryAtPutInValueCell(klass_dict, name, value);

  return None::object();
}

// Generic attribute lookup code used for instance objects
Object* Runtime::instanceGetAttr(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name) {
  if (!name->isString()) {
    // TODO(T25140871): Refactor into something like:
    //     thread->throwUnexpectedTypeError(expected, actual)
    return thread->throwTypeErrorFromCString("attribute name must be a string");
  }

  if (String::cast(*name)->equals(symbols()->DunderClass())) {
    // TODO(T27735822): Make __class__ a descriptor
    return classOf(*receiver);
  }

  // Look for the attribute in the class
  HandleScope scope(thread);
  Handle<Class> klass(&scope, classOf(*receiver));
  Handle<Object> klass_attr(&scope, lookupNameInMro(thread, klass, name));
  if (isDataDescriptor(thread, klass_attr)) {
    // TODO(T25692531): Call __get__ from klass_attr
    UNIMPLEMENTED("custom descriptors are unsupported");
  }

  // No data descriptor found on the class, look at the instance.
  Handle<HeapObject> instance(&scope, *receiver);
  Object* result = thread->runtime()->instanceAt(thread, instance, name);
  if (!result->isError()) {
    return result;
  }

  // Nothing found in the instance, if we found a non-data descriptor via the
  // class search, use it.
  if (isNonDataDescriptor(thread, klass_attr)) {
    Handle<Object> owner(&scope, *klass);
    return Interpreter::callDescriptorGet(
        thread, thread->currentFrame(), klass_attr, receiver, owner);
  }

  // If a regular attribute was found in the class, return it
  if (!klass_attr->isError()) {
    return *klass_attr;
  }

  // TODO(T25140871): Refactor this into something like:
  //     thread->throwMissingAttributeError(name)
  return thread->throwAttributeErrorFromCString("missing attribute");
}

Object* Runtime::instanceSetAttr(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name,
    const Handle<Object>& value) {
  if (!name->isString()) {
    // TODO(T25140871): Refactor into something like:
    //     thread->throwUnexpectedTypeError(expected, actual)
    return thread->throwTypeErrorFromCString("attribute name must be a string");
  }

  // Check for a data descriptor
  HandleScope scope(thread);
  Handle<Class> klass(&scope, classOf(*receiver));
  Handle<Object> klass_attr(&scope, lookupNameInMro(thread, klass, name));
  if (isDataDescriptor(thread, klass_attr)) {
    // TODO(T25692531): Call __set__ from klass_attr
    UNIMPLEMENTED("custom descriptors are unsupported");
  }

  // No data descriptor found, store on the instance
  Handle<HeapObject> instance(&scope, *receiver);
  return thread->runtime()->instanceAtPut(thread, instance, name, value);
}

// Note that PEP 562 adds support for data descriptors in module objects.
// We are targeting python 3.6 for now, so we won't worry about that.
Object* Runtime::moduleGetAttr(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name) {
  if (!name->isString()) {
    // TODO(T25140871): Refactor into something like:
    //     thread->throwUnexpectedTypeError(expected, actual)
    return thread->throwTypeErrorFromCString("attribute name must be a string");
  }

  HandleScope scope(thread);
  Handle<Module> mod(&scope, *receiver);
  Handle<Object> ret(&scope, moduleAt(mod, name));

  if (!ret->isError()) {
    return *ret;
  } else {
    // TODO(T25140871): Refactor this into something like:
    //     thread->throwMissingAttributeError(name)
    return thread->throwAttributeErrorFromCString("missing attribute");
  }
}

Object* Runtime::moduleSetAttr(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name,
    const Handle<Object>& value) {
  if (!name->isString()) {
    // TODO(T25140871): Refactor into something like:
    //     thread->throwUnexpectedTypeError(expected, actual)
    return thread->throwTypeErrorFromCString("attribute name must be a string");
  }

  HandleScope scope(thread);
  Handle<Module> mod(&scope, *receiver);
  moduleAtPut(mod, name, value);
  return None::object();
}

bool Runtime::isDataDescriptor(Thread* thread, const Handle<Object>& object) {
  if (object->isFunction() || object->isClassMethod() ||
      object->isStaticMethod() || object->isError()) {
    return false;
  }
  // TODO(T25692962): Track "descriptorness" through a bit on the class
  HandleScope scope(thread);
  Handle<Class> klass(&scope, classOf(*object));
  Handle<Object> dunder_set(&scope, symbols()->DunderSet());
  return !lookupNameInMro(thread, klass, dunder_set)->isError();
}

bool Runtime::isNonDataDescriptor(
    Thread* thread,
    const Handle<Object>& object) {
  if (object->isFunction() || object->isClassMethod() ||
      object->isStaticMethod()) {
    return true;
  } else if (object->isError()) {
    return false;
  }
  // TODO(T25692962): Track "descriptorness" through a bit on the class
  HandleScope scope(thread);
  Handle<Class> klass(&scope, classOf(*object));
  Handle<Object> dunder_get(&scope, symbols()->DunderGet());
  return !lookupNameInMro(thread, klass, dunder_get)->isError();
}

Object* Runtime::newCode() {
  HandleScope scope;
  Handle<Code> result(&scope, heap()->createCode());
  result->setArgcount(0);
  result->setKwonlyargcount(0);
  result->setCell2arg(0);
  result->setNlocals(0);
  result->setStacksize(0);
  result->setFlags(0);
  result->setFreevars(empty_object_array_);
  result->setCellvars(empty_object_array_);
  result->setFirstlineno(0);
  return *result;
}

Object* Runtime::newBuiltinFunction(
    Function::Entry entry,
    Function::Entry entry_kw,
    Function::Entry entry_ex) {
  Object* result = heap()->createFunction();
  DCHECK(result != nullptr, "failed to createFunction");
  auto function = Function::cast(result);
  function->setEntry(entry);
  function->setEntryKw(entry_kw);
  function->setEntryEx(entry_ex);
  return result;
}

Object* Runtime::newFunction() {
  Object* object = heap()->createFunction();
  DCHECK(object != nullptr, "failed to createFunction");
  auto function = Function::cast(object);
  function->setEntry(unimplementedTrampoline);
  function->setEntryKw(unimplementedTrampoline);
  function->setEntryEx(unimplementedTrampoline);
  return function;
}

Object* Runtime::newInstance(const Handle<Layout>& layout) {
  HandleScope scope;
  word layout_id = layout->id();
  word num_words = layout->instanceSize();
  Handle<HeapObject> instance(
      &scope, heap()->createInstance(layout_id, num_words));
  // Set the overflow array
  instance->instanceVariableAtPut(
      layout->overflowOffset(), empty_object_array_);
  return *instance;
}

void Runtime::classAddBuiltinFunction(
    const Handle<Class>& klass,
    Object* name,
    Function::Entry entry,
    Function::Entry entry_kw,
    Function::Entry entry_ex) {
  HandleScope scope;
  Handle<Object> key(&scope, name);
  Handle<Function> function(
      &scope, newBuiltinFunction(entry, entry_kw, entry_ex));
  function->setName(*key);
  Handle<Object> value(&scope, *function);
  Handle<Dictionary> dict(&scope, klass->dictionary());
  dictionaryAtPutInValueCell(dict, key, value);
}

void Runtime::classAddExtensionFunction(
    const Handle<Class>& klass,
    Object* name,
    void* c_function) {
  DCHECK(
      !klass->extensionType()->isNone(), "Class must contain extension type");

  HandleScope scope;
  Handle<Function> function(&scope, newFunction());
  function->setName(name);
  function->setCode(newIntegerFromCPointer(c_function));
  function->setEntry(extensionTrampoline);
  function->setEntryKw(extensionTrampolineKw);
  function->setEntryEx(extensionTrampolineEx);
  Handle<Object> key(&scope, name);
  Handle<Object> value(&scope, *function);
  Handle<Dictionary> dict(&scope, klass->dictionary());
  dictionaryAtPutInValueCell(dict, key, value);
}

Object* Runtime::newList() {
  HandleScope scope;
  Handle<List> result(&scope, heap()->createList());
  result->setAllocated(0);
  result->setItems(empty_object_array_);
  return *result;
}

Object* Runtime::newListIterator(const Handle<Object>& list) {
  HandleScope scope;
  Handle<ListIterator> list_iterator(&scope, heap()->createListIterator());
  list_iterator->setIndex(0);
  list_iterator->setList(*list);
  return *list_iterator;
}

Object* Runtime::newModule(const Handle<Object>& name) {
  HandleScope scope;
  Handle<Module> result(&scope, heap()->createModule());
  Handle<Dictionary> dictionary(&scope, newDictionary());
  result->setDictionary(*dictionary);
  result->setName(*name);
  Handle<Object> key(&scope, symbols()->DunderName());
  dictionaryAtPutInValueCell(dictionary, key, name);
  return *result;
}

Object* Runtime::newIntegerFromCPointer(void* ptr) {
  return newInteger(reinterpret_cast<word>(ptr));
}

Object* Runtime::newObjectArray(word length) {
  if (length == 0) {
    return empty_object_array_;
  }
  return heap()->createObjectArray(length, None::object());
}

Object* Runtime::newInteger(word value) {
  if (SmallInteger::isValid(value)) {
    return SmallInteger::fromWord(value);
  }
  return LargeInteger::cast(heap()->createLargeInteger(value));
}

Object* Runtime::newDouble(double value) {
  return Double::cast(heap()->createDouble(value));
}

Object* Runtime::newComplex(double real, double imag) {
  return Complex::cast(heap()->createComplex(real, imag));
}

Object* Runtime::newRange(word start, word stop, word step) {
  auto range = Range::cast(heap()->createRange());
  range->setStart(start);
  range->setStop(stop);
  range->setStep(step);
  return range;
}

Object* Runtime::newRangeIterator(const Handle<Object>& range) {
  HandleScope scope;
  Handle<RangeIterator> range_iterator(&scope, heap()->createRangeIterator());
  range_iterator->setRange(*range);
  return *range_iterator;
}

Object* Runtime::newSlice(
    const Handle<Object>& start,
    const Handle<Object>& stop,
    const Handle<Object>& step) {
  HandleScope scope;
  Handle<Slice> slice(&scope, heap()->createSlice());
  slice->setStart(*start);
  slice->setStop(*stop);
  slice->setStep(*step);
  return *slice;
}

Object* Runtime::newStaticMethod() {
  return heap()->createStaticMethod();
}

Object* Runtime::newStringFromCString(const char* c_string) {
  word length = std::strlen(c_string);
  auto data = reinterpret_cast<const byte*>(c_string);
  return newStringWithAll(View<byte>(data, length));
}

Object* Runtime::newStringWithAll(View<byte> code_units) {
  word length = code_units.length();
  if (length <= SmallString::kMaxLength) {
    return SmallString::fromBytes(code_units);
  }
  Object* result = heap()->createLargeString(length);
  DCHECK(result != nullptr, "failed to create large string");
  byte* dst = reinterpret_cast<byte*>(LargeString::cast(result)->address());
  const byte* src = code_units.data();
  memcpy(dst, src, length);
  return result;
}

Object* Runtime::internStringFromCString(const char* c_string) {
  HandleScope scope;
  // TODO(T29648342): Optimize lookup to avoid creating an intermediary String
  Handle<Object> str(&scope, newStringFromCString(c_string));
  return internString(str);
}

Object* Runtime::internString(const Handle<Object>& string) {
  HandleScope scope;
  Handle<Set> set(&scope, interned());
  Handle<Object> key(&scope, *string);
  DCHECK(string->isString(), "not a string");
  if (string->isSmallString()) {
    return *string;
  }
  return setAdd(set, key);
}

Object* Runtime::hash(Object* object) {
  if (!object->isHeapObject()) {
    return immediateHash(object);
  }
  if (object->isByteArray() || object->isLargeString()) {
    return valueHash(object);
  }
  return identityHash(object);
}

Object* Runtime::immediateHash(Object* object) {
  if (object->isSmallInteger()) {
    return object;
  }
  if (object->isBoolean()) {
    return SmallInteger::fromWord(Boolean::cast(object)->value() ? 1 : 0);
  }
  if (object->isSmallString()) {
    return SmallInteger::fromWord(
        reinterpret_cast<uword>(object) >> SmallString::kTagSize);
  }
  return SmallInteger::fromWord(reinterpret_cast<uword>(object));
}

// Xoroshiro128+
// http://xoroshiro.di.unimi.it/
uword Runtime::random() {
  const uint64_t s0 = random_state_[0];
  uint64_t s1 = random_state_[1];
  const uint64_t result = s0 + s1;
  s1 ^= s0;
  random_state_[0] = Utils::rotateLeft(s0, 55) ^ s1 ^ (s1 << 14);
  random_state_[1] = Utils::rotateLeft(s1, 36);
  return result;
}

void Runtime::setArgv(int argc, const char** argv) {
  HandleScope scope;
  Handle<List> list(&scope, newList());
  CHECK(argc >= 1, "Unexpected argc");
  for (int i = 1; i < argc; i++) { // skip program name (i.e. "python")
    Handle<Object> arg_val(&scope, newStringFromCString(argv[i]));
    listAdd(list, arg_val);
  }

  Handle<Object> module_name(&scope, symbols()->Sys());
  Handle<Module> sys_module(&scope, findModule(module_name));
  Handle<Object> argv_name(&scope, symbols()->Argv());
  Handle<Object> argv_value(&scope, *list);
  moduleAddGlobal(sys_module, argv_name, argv_value);
}

Object* Runtime::identityHash(Object* object) {
  HeapObject* src = HeapObject::cast(object);
  word code = src->header()->hashCode();
  if (code == 0) {
    code = random() & Header::kHashCodeMask;
    code = (code == 0) ? 1 : code;
    src->setHeader(src->header()->withHashCode(code));
  }
  return SmallInteger::fromWord(code);
}

word Runtime::siphash24(View<byte> array) {
  word result = 0;
  ::halfsiphash(
      array.data(),
      array.length(),
      reinterpret_cast<const uint8_t*>(hash_secret_),
      reinterpret_cast<uint8_t*>(&result),
      sizeof(result));
  return result;
}

Object* Runtime::valueHash(Object* object) {
  HeapObject* src = HeapObject::cast(object);
  Header* header = src->header();
  word code = header->hashCode();
  if (code == 0) {
    word size = src->headerCountOrOverflow();
    code = siphash24(View<byte>(reinterpret_cast<byte*>(src->address()), size));
    code &= Header::kHashCodeMask;
    code = (code == 0) ? 1 : code;
    src->setHeader(header->withHashCode(code));
    DCHECK(code == src->header()->hashCode(), "hash failure");
  }
  return SmallInteger::fromWord(code);
}

void Runtime::initializeClasses() {
  initializeLayouts();
  initializeHeapClasses();
  initializeImmediateClasses();
}

void Runtime::initializeLayouts() {
  HandleScope scope;
  Handle<ObjectArray> array(&scope, newObjectArray(256));
  Handle<List> list(&scope, newList());
  list->setItems(*array);
  const word allocated = static_cast<word>(IntrinsicLayoutId::kLastId + 1);
  CHECK(allocated < array->length(), "bad allocation %ld", allocated);
  list->setAllocated(allocated);
  layouts_ = *list;
}

Object* Runtime::createMro(View<IntrinsicLayoutId> layout_ids) {
  HandleScope scope;
  Handle<ObjectArray> result(&scope, newObjectArray(layout_ids.length()));
  for (word i = 0; i < layout_ids.length(); i++) {
    result->atPut(i, classAt(layout_ids.get(i)));
  }
  return *result;
}

template <typename... Args>
Object* Runtime::initializeHeapClass(const char* name, Args... args) {
  HandleScope scope;
  const IntrinsicLayoutId layout_ids[] = {args..., IntrinsicLayoutId::kObject};
  Handle<Layout> layout(&scope, newLayoutWithId(layout_ids[0]));
  Handle<Class> klass(&scope, newClass());
  layout->setDescribedClass(*klass);
  klass->setName(newStringFromCString(name));
  klass->setMro(createMro(layout_ids));
  klass->setInstanceLayout(layoutAt(layout_ids[0]));
  return *klass;
}

void Runtime::initializeHeapClasses() {
  initializeObjectClass();

  // Abstract classes.
  initializeStrClass();
  initializeHeapClass("int", IntrinsicLayoutId::kInteger);

  // Concrete classes.
  initializeHeapClass("bytearray", IntrinsicLayoutId::kByteArray);
  initializeClassMethodClass();
  initializeHeapClass("code", IntrinsicLayoutId::kCode);
  initializeDictClass();
  initializeHeapClass("ellipsis", IntrinsicLayoutId::kEllipsis);
  initializeFloatClass();
  initializeFunctionClass();
  initializeHeapClass(
      "largeint",
      IntrinsicLayoutId::kLargeInteger,
      IntrinsicLayoutId::kInteger);
  initializeHeapClass(
      "largestr", IntrinsicLayoutId::kLargeString, IntrinsicLayoutId::kString);
  initializeHeapClass("layout", IntrinsicLayoutId::kLayout);
  initializeListClass();
  initializeHeapClass("list_iterator", IntrinsicLayoutId::kListIterator);
  initializeHeapClass("method", IntrinsicLayoutId::kBoundMethod);
  initializeHeapClass("module", IntrinsicLayoutId::kModule);
  initializeHeapClass("NotImplementedType", IntrinsicLayoutId::kNotImplemented);
  initializeObjectArrayClass();
  initializeHeapClass("range", IntrinsicLayoutId::kRange);
  initializeHeapClass("range_iterator", IntrinsicLayoutId::kRangeIterator);
  initializeRefClass();
  initializeSetClass();
  initializeHeapClass("slice", IntrinsicLayoutId::kSlice);
  initializeStaticMethodClass();
  initializeSuperClass();
  initializeTypeClass();
  initializeHeapClass("valuecell", IntrinsicLayoutId::kValueCell);
}

void Runtime::initializeRefClass() {
  HandleScope scope;
  Handle<Class> ref(
      &scope, initializeHeapClass("ref", IntrinsicLayoutId::kWeakRef));

  classAddBuiltinFunction(
      ref,
      symbols()->DunderInit(),
      nativeTrampoline<builtinRefInit>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      ref,
      symbols()->DunderNew(),
      nativeTrampoline<builtinRefNew>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeFunctionClass() {
  HandleScope scope;
  Handle<Class> function(
      &scope, initializeHeapClass("function", IntrinsicLayoutId::kFunction));

  classAddBuiltinFunction(
      function,
      symbols()->DunderGet(),
      nativeTrampoline<functionDescriptorGet>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeObjectClass() {
  HandleScope scope;
  Handle<Class> object(&scope, initializeHeapClass("object"));

  classAddBuiltinFunction(
      object,
      symbols()->DunderInit(),
      nativeTrampoline<builtinObjectInit>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      object,
      symbols()->DunderNew(),
      nativeTrampoline<builtinObjectNew>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeStrClass() {
  HandleScope scope;
  Handle<Class> type(
      &scope, initializeHeapClass("str", IntrinsicLayoutId::kString));

  classAddBuiltinFunction(
      type,
      symbols()->DunderEq(),
      nativeTrampoline<builtinStringEq>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      type,
      symbols()->DunderGe(),
      nativeTrampoline<builtinStringGe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      type,
      symbols()->DunderGt(),
      nativeTrampoline<builtinStringGt>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      type,
      symbols()->DunderLe(),
      nativeTrampoline<builtinStringLe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      type,
      symbols()->DunderLt(),
      nativeTrampoline<builtinStringLt>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      type,
      symbols()->DunderNe(),
      nativeTrampoline<builtinStringNe>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeObjectArrayClass() {
  HandleScope scope;
  Handle<Class> type(
      &scope, initializeHeapClass("tuple", IntrinsicLayoutId::kObjectArray));
  classAddBuiltinFunction(
      type,
      symbols()->DunderEq(),
      nativeTrampoline<builtinTupleEq>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeDictClass() {
  HandleScope scope;
  Handle<Class> dict_type(
      &scope, initializeHeapClass("dict", IntrinsicLayoutId::kDictionary));
  classAddBuiltinFunction(
      dict_type,
      symbols()->DunderEq(),
      nativeTrampoline<builtinDictionaryEq>,
      unimplementedTrampoline,
      unimplementedTrampoline);
  classAddBuiltinFunction(
      dict_type,
      symbols()->DunderLen(),
      nativeTrampoline<builtinDictionaryLen>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeListClass() {
  HandleScope scope;
  Handle<Class> list(
      &scope, initializeHeapClass("list", IntrinsicLayoutId::kList));

  classAddBuiltinFunction(
      list,
      symbols()->Append(),
      nativeTrampoline<builtinListAppend>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      list,
      symbols()->Insert(),
      nativeTrampoline<builtinListInsert>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      list,
      symbols()->DunderLen(),
      nativeTrampoline<builtinListLen>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      list,
      symbols()->Pop(),
      nativeTrampoline<builtinListPop>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      list,
      symbols()->DunderNew(),
      nativeTrampoline<builtinListNew>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      list,
      symbols()->Remove(),
      nativeTrampoline<builtinListRemove>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  list->setFlag(Class::Flag::kListSubclass);
}

void Runtime::initializeClassMethodClass() {
  HandleScope scope;
  Handle<Class> classmethod(
      &scope,
      initializeHeapClass("classmethod", IntrinsicLayoutId::kClassMethod));

  classAddBuiltinFunction(
      classmethod,
      symbols()->DunderInit(),
      nativeTrampoline<builtinClassMethodInit>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      classmethod,
      symbols()->DunderNew(),
      nativeTrampoline<builtinClassMethodNew>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      classmethod,
      symbols()->DunderGet(),
      nativeTrampoline<classmethodDescriptorGet>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeTypeClass() {
  HandleScope scope;
  Handle<Class> type(
      &scope, initializeHeapClass("type", IntrinsicLayoutId::kType));

  classAddBuiltinFunction(
      type,
      symbols()->DunderCall(),
      builtinTypeCall,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      type,
      symbols()->DunderNew(),
      nativeTrampoline<builtinTypeNew>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      type,
      symbols()->DunderInit(),
      nativeTrampoline<builtinTypeInit>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeImmediateClasses() {
  initializeBooleanClass();
  initializeHeapClass("NoneType", IntrinsicLayoutId::kNone);
  initializeHeapClass(
      "smallstr", IntrinsicLayoutId::kSmallString, IntrinsicLayoutId::kString);
  initializeSmallIntClass();
}

void Runtime::initializeBooleanClass() {
  HandleScope scope;
  Handle<Class> type(
      &scope,
      initializeHeapClass(
          "bool", IntrinsicLayoutId::kBoolean, IntrinsicLayoutId::kInteger));

  classAddBuiltinFunction(
      type,
      symbols()->DunderBool(),
      nativeTrampoline<builtinBooleanBool>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeFloatClass() {
  HandleScope scope;
  Handle<Class> float_type(
      &scope, initializeHeapClass("float", IntrinsicLayoutId::kDouble));

  classAddBuiltinFunction(
      float_type,
      symbols()->DunderEq(),
      nativeTrampoline<builtinDoubleEq>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      float_type,
      symbols()->DunderGe(),
      nativeTrampoline<builtinDoubleGe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      float_type,
      symbols()->DunderGt(),
      nativeTrampoline<builtinDoubleGt>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      float_type,
      symbols()->DunderLe(),
      nativeTrampoline<builtinDoubleLe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      float_type,
      symbols()->DunderLt(),
      nativeTrampoline<builtinDoubleLt>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      float_type,
      symbols()->DunderNe(),
      nativeTrampoline<builtinDoubleNe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      float_type,
      symbols()->DunderSub(),
      nativeTrampoline<builtinDoubleSub>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeSetClass() {
  HandleScope scope;
  Handle<Class> set_type(
      &scope, initializeHeapClass("set", IntrinsicLayoutId::kSet));
  classAddBuiltinFunction(
      set_type,
      symbols()->DunderLen(),
      nativeTrampoline<builtinSetLen>,
      unimplementedTrampoline,
      unimplementedTrampoline);
  classAddBuiltinFunction(
      set_type,
      symbols()->Pop(),
      nativeTrampoline<builtinSetPop>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::initializeSmallIntClass() {
  HandleScope scope;
  Handle<Class> small_integer(
      &scope,
      initializeHeapClass(
          "smallint",
          IntrinsicLayoutId::kSmallInteger,
          IntrinsicLayoutId::kInteger));

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderBool(),
      nativeTrampoline<builtinSmallIntegerBool>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderInvert(),
      nativeTrampoline<builtinSmallIntegerInvert>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderNeg(),
      nativeTrampoline<builtinSmallIntegerNeg>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderPos(),
      nativeTrampoline<builtinSmallIntegerPos>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderSub(),
      nativeTrampoline<builtinSmallIntegerSub>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderEq(),
      nativeTrampoline<builtinSmallIntegerEq>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderGe(),
      nativeTrampoline<builtinSmallIntegerGe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderGt(),
      nativeTrampoline<builtinSmallIntegerGt>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderLe(),
      nativeTrampoline<builtinSmallIntegerLe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderLt(),
      nativeTrampoline<builtinSmallIntegerLt>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      small_integer,
      symbols()->DunderNe(),
      nativeTrampoline<builtinSmallIntegerNe>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  // We want to lookup the class of an immediate type by using the 5-bit tag
  // value as an index into the class table.  Replicate the class object for
  // SmallInteger to all locations that decode to a SmallInteger tag.
  for (word i = 1; i < 16; i++) {
    DCHECK(
        List::cast(layouts_)->at(i << 1) == None::object(), "list collision");
    List::cast(layouts_)->atPut(i << 1, *small_integer);
  }
}

void Runtime::initializeStaticMethodClass() {
  HandleScope scope;
  Handle<Class> staticmethod(
      &scope,
      initializeHeapClass("staticmethod", IntrinsicLayoutId::kStaticMethod));

  classAddBuiltinFunction(
      staticmethod,
      symbols()->DunderNew(),
      nativeTrampoline<builtinStaticMethodNew>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      staticmethod,
      symbols()->DunderInit(),
      nativeTrampoline<builtinStaticMethodInit>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      staticmethod,
      symbols()->DunderGet(),
      nativeTrampoline<staticmethodDescriptorGet>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

void Runtime::collectGarbage() {
  bool run_callback = callbacks_ == None::object();
  Object* cb = Scavenger(this).scavenge();
  callbacks_ = WeakRef::spliceQueue(callbacks_, cb);
  if (run_callback) {
    processCallbacks();
  }
}

void Runtime::processCallbacks() {
  Thread* thread = Thread::currentThread();
  Frame* frame = thread->currentFrame();
  Object** sp = frame->valueStackTop();
  HandleScope scope(thread);
  while (callbacks_ != None::object()) {
    Handle<WeakRef> weak(
        &scope, WeakRef::cast(WeakRef::dequeueReference(&callbacks_)));
    *--sp = weak->callback();
    *--sp = Object::cast(*weak);
    Interpreter::call(thread, frame, sp, 1);
    thread->ignorePendingException();
    *sp += 2;
    weak->setCallback(None::object());
  }
}

Object* Runtime::run(const char* buffer) {
  HandleScope scope;

  Handle<Module> main(&scope, createMainModule());
  return executeModule(buffer, main);
}

Object* Runtime::runFromCString(const char* c_string) {
  const char* buffer = compile(c_string);
  Object* result = run(buffer);
  delete[] buffer;
  return result;
}

Object* Runtime::executeModule(
    const char* buffer,
    const Handle<Module>& module) {
  HandleScope scope;
  Marshal::Reader reader(&scope, this, buffer);

  reader.readLong();
  reader.readLong();
  reader.readLong();

  Handle<Code> code(&scope, reader.readObject());
  DCHECK(code->argcount() == 0, "invalid argcount %ld", code->argcount());

  return Thread::currentThread()->runModuleFunction(*module, *code);
}

struct ModuleInitializer {
  const char* name;
  void* (*initfunc)();
};

extern struct ModuleInitializer kModuleInitializers[];

Object* Runtime::importModule(const Handle<Object>& name) {
  HandleScope scope;
  Handle<Object> cached_module(&scope, findModule(name));
  if (!cached_module->isNone()) {
    return *cached_module;
  } else {
    for (int i = 0; kModuleInitializers[i].name != nullptr; i++) {
      if (String::cast(*name)->equalsCString(kModuleInitializers[i].name)) {
        (*kModuleInitializers[i].initfunc)();
        cached_module = findModule(name);
        return *cached_module;
      }
    }
  }

  return Thread::currentThread()->throwRuntimeErrorFromCString(
      "importModule is unimplemented!");
}

// TODO: support fromlist and level. Ideally, we'll never implement that
// functionality in c++, instead using the pure-python importlib
// implementation that ships with cpython.
Object* Runtime::importModuleFromBuffer(
    const char* buffer,
    const Handle<Object>& name) {
  HandleScope scope;
  Handle<Object> cached_module(&scope, findModule(name));
  if (!cached_module->isNone()) {
    return *cached_module;
  }

  Handle<Module> module(&scope, newModule(name));
  addModule(module);
  executeModule(buffer, module);
  return *module;
}

void Runtime::initializeThreads() {
  auto main_thread = new Thread(Thread::kDefaultStackSize);
  threads_ = main_thread;
  main_thread->setRuntime(this);
  Thread::setCurrentThread(main_thread);
}

void Runtime::initializePrimitiveInstances() {
  empty_object_array_ = heap()->createObjectArray(0, None::object());
  empty_byte_array_ = heap()->createByteArray(0);
  ellipsis_ = heap()->createEllipsis();
  not_implemented_ = heap()->createNotImplemented();
  callbacks_ = None::object();
}

void Runtime::initializeInterned() {
  interned_ = newSet();
}

void Runtime::initializeRandom() {
  uword random_state[2];
  uword hash_secret[2];
  OS::secureRandom(
      reinterpret_cast<byte*>(&random_state), sizeof(random_state));
  OS::secureRandom(reinterpret_cast<byte*>(&hash_secret), sizeof(hash_secret));
  seedRandom(random_state, hash_secret);
}

void Runtime::initializeSymbols() {
  HandleScope scope;
  symbols_ = new Symbols(this);
  for (word i = 0; i < Symbols::kMaxSymbolId; i++) {
    Handle<Object> symbol(
        &scope, symbols_->at(static_cast<Symbols::SymbolId>(i)));
    internString(symbol);
  }
}

void Runtime::visitRoots(PointerVisitor* visitor) {
  visitRuntimeRoots(visitor);
  visitThreadRoots(visitor);
}

void Runtime::visitRuntimeRoots(PointerVisitor* visitor) {
  // Visit layouts
  visitor->visitPointer(&layouts_);

  // Visit instances
  visitor->visitPointer(&empty_byte_array_);
  visitor->visitPointer(&empty_object_array_);
  visitor->visitPointer(&ellipsis_);
  visitor->visitPointer(&not_implemented_);
  visitor->visitPointer(&build_class_);
  visitor->visitPointer(&print_default_end_);

  // Visit interned strings.
  visitor->visitPointer(&interned_);

  // Visit modules
  visitor->visitPointer(&modules_);

  // Visit C-API handles
  visitor->visitPointer(&api_handles_);

  // Visit Extension types
  visitor->visitPointer(&extension_types_);

  // Visit symbols
  symbols_->visit(visitor);

  // Visit GC callbacks
  visitor->visitPointer(&callbacks_);
}

void Runtime::visitThreadRoots(PointerVisitor* visitor) {
  for (Thread* thread = threads_; thread != nullptr; thread = thread->next()) {
    thread->visitRoots(visitor);
  }
}

void Runtime::addModule(const Handle<Module>& module) {
  HandleScope scope;
  Handle<Dictionary> dict(&scope, modules());
  Handle<Object> key(&scope, module->name());
  Handle<Object> value(&scope, *module);
  dictionaryAtPut(dict, key, value);
}

Object* Runtime::findModule(const Handle<Object>& name) {
  DCHECK(name->isString(), "name not a string");

  HandleScope scope;
  Handle<Dictionary> dict(&scope, modules());
  Object* value = dictionaryAt(dict, name);
  if (value->isError()) {
    return None::object();
  }
  return value;
}

Object* Runtime::moduleAt(
    const Handle<Module>& module,
    const Handle<Object>& key) {
  HandleScope scope;
  Handle<Dictionary> dict(&scope, module->dictionary());
  Handle<Object> value_cell(&scope, dictionaryAt(dict, key));
  if (value_cell->isError()) {
    return Error::object();
  }
  return ValueCell::cast(*value_cell)->value();
}

void Runtime::moduleAtPut(
    const Handle<Module>& module,
    const Handle<Object>& key,
    const Handle<Object>& value) {
  HandleScope scope;
  Handle<Dictionary> dict(&scope, module->dictionary());
  dictionaryAtPutInValueCell(dict, key, value);
}

void Runtime::initializeModules() {
  modules_ = newDictionary();
  createBuiltinsModule();
  createSysModule();
  createTimeModule();
  createWeakRefModule();
}

struct ExtensionTypeInitializer {
  void (*initfunc)();
};

extern struct ExtensionTypeInitializer kExtensionTypeInitializers[];

void Runtime::initializeApiHandles() {
  api_handles_ = newDictionary();
  extension_types_ = newDictionary();
  // Initialize the extension types
  for (int i = 0; kExtensionTypeInitializers[i].initfunc != nullptr; i++) {
    (*kExtensionTypeInitializers[i].initfunc)();
  }
}

Object* Runtime::classOf(Object* object) {
  HandleScope scope;
  Handle<Layout> layout(&scope, List::cast(layouts_)->at(object->layoutId()));
  return layout->describedClass();
}

Object* Runtime::layoutAt(word layout_id) {
  return List::cast(layouts_)->at(layout_id);
}

Object* Runtime::classAt(word layout_id) {
  return Layout::cast(layoutAt(layout_id))->describedClass();
}

word Runtime::newLayoutId() {
  HandleScope scope;
  Handle<List> list(&scope, layouts_);
  Handle<Object> value(&scope, None::object());
  word result = list->allocated();
  DCHECK(
      result <= Header::kMaxLayoutId,
      "exceeded layout id space in header word");
  listAdd(list, value);
  return result;
}

Object* Runtime::binaryOperationSelector(Interpreter::BinaryOp op) {
  switch (op) {
    case Interpreter::BinaryOp::ADD:
      return symbols()->DunderAdd();
    case Interpreter::BinaryOp::SUB:
      return symbols()->DunderSub();
    case Interpreter::BinaryOp::MUL:
      return symbols()->DunderMul();
    case Interpreter::BinaryOp::MATMUL:
      return symbols()->DunderMatmul();
    case Interpreter::BinaryOp::TRUEDIV:
      return symbols()->DunderTruediv();
    case Interpreter::BinaryOp::FLOORDIV:
      return symbols()->DunderFloordiv();
    case Interpreter::BinaryOp::MOD:
      return symbols()->DunderMod();
    case Interpreter::BinaryOp::DIVMOD:
      return symbols()->DunderDivmod();
    case Interpreter::BinaryOp::POW:
      return symbols()->DunderPow();
    case Interpreter::BinaryOp::LSHIFT:
      return symbols()->DunderLshift();
    case Interpreter::BinaryOp::RSHIFT:
      return symbols()->DunderRshift();
    case Interpreter::BinaryOp::AND:
      return symbols()->DunderAnd();
    case Interpreter::BinaryOp::XOR:
      return symbols()->DunderXor();
    case Interpreter::BinaryOp::OR:
      return symbols()->DunderOr();
    default:
      UNREACHABLE("unknown binary operation");
  }
}

Object* Runtime::swappedBinaryOperationSelector(Interpreter::BinaryOp op) {
  switch (op) {
    case Interpreter::BinaryOp::ADD:
      return symbols()->DunderRadd();
    case Interpreter::BinaryOp::SUB:
      return symbols()->DunderRsub();
    case Interpreter::BinaryOp::MUL:
      return symbols()->DunderRmul();
    case Interpreter::BinaryOp::MATMUL:
      return symbols()->DunderRmatmul();
    case Interpreter::BinaryOp::TRUEDIV:
      return symbols()->DunderRtruediv();
    case Interpreter::BinaryOp::FLOORDIV:
      return symbols()->DunderRfloordiv();
    case Interpreter::BinaryOp::MOD:
      return symbols()->DunderRmod();
    case Interpreter::BinaryOp::DIVMOD:
      return symbols()->DunderRdivmod();
    case Interpreter::BinaryOp::POW:
      return symbols()->DunderRpow();
    case Interpreter::BinaryOp::LSHIFT:
      return symbols()->DunderRlshift();
    case Interpreter::BinaryOp::RSHIFT:
      return symbols()->DunderRrshift();
    case Interpreter::BinaryOp::AND:
      return symbols()->DunderRand();
    case Interpreter::BinaryOp::XOR:
      return symbols()->DunderRxor();
    case Interpreter::BinaryOp::OR:
      return symbols()->DunderRor();
    default:
      UNREACHABLE("unknown binary operation");
  }
}

Object* Runtime::inplaceOperationSelector(Interpreter::BinaryOp op) {
  switch (op) {
    case Interpreter::BinaryOp::ADD:
      return symbols()->DunderIadd();
    case Interpreter::BinaryOp::SUB:
      return symbols()->DunderIsub();
    case Interpreter::BinaryOp::MUL:
      return symbols()->DunderImul();
    case Interpreter::BinaryOp::MATMUL:
      return symbols()->DunderImatmul();
    case Interpreter::BinaryOp::TRUEDIV:
      return symbols()->DunderItruediv();
    case Interpreter::BinaryOp::FLOORDIV:
      return symbols()->DunderIfloordiv();
    case Interpreter::BinaryOp::MOD:
      return symbols()->DunderImod();
    case Interpreter::BinaryOp::POW:
      return symbols()->DunderIpow();
    case Interpreter::BinaryOp::LSHIFT:
      return symbols()->DunderIlshift();
    case Interpreter::BinaryOp::RSHIFT:
      return symbols()->DunderIrshift();
    case Interpreter::BinaryOp::AND:
      return symbols()->DunderIand();
    case Interpreter::BinaryOp::XOR:
      return symbols()->DunderIxor();
    case Interpreter::BinaryOp::OR:
      return symbols()->DunderIor();
    default:
      UNREACHABLE("unknown inplace operation");
  }
}

Object* Runtime::comparisonSelector(CompareOp op) {
  DCHECK(op >= CompareOp::LT, "invalid compare op");
  DCHECK(op <= CompareOp::GE, "invalid compare op");
  switch (op) {
    case LT:
      return symbols()->DunderLt();
    case LE:
      return symbols()->DunderLe();
    case EQ:
      return symbols()->DunderEq();
    case NE:
      return symbols()->DunderNe();
    case GT:
      return symbols()->DunderGt();
    case GE:
      return symbols()->DunderGe();
    default:
      UNREACHABLE("bad comparison op");
  }
}

Object* Runtime::swappedComparisonSelector(python::CompareOp op) {
  DCHECK(op >= CompareOp::LT, "invalid compare op");
  DCHECK(op <= CompareOp::GE, "invalid compare op");
  CompareOp swapped_op = kSwappedCompareOp[op];
  return comparisonSelector(swapped_op);
}

void Runtime::moduleAddGlobal(
    const Handle<Module>& module,
    const Handle<Object>& key,
    const Handle<Object>& value) {
  HandleScope scope;
  Handle<Dictionary> dictionary(&scope, module->dictionary());
  dictionaryAtPutInValueCell(dictionary, key, value);
}

Object* Runtime::moduleAddBuiltinFunction(
    const Handle<Module>& module,
    Object* name,
    const Function::Entry entry,
    const Function::Entry entry_kw,
    const Function::Entry entry_ex) {
  HandleScope scope;
  Handle<Object> key(&scope, name);
  Handle<Dictionary> dictionary(&scope, module->dictionary());
  Handle<Object> value(&scope, newBuiltinFunction(entry, entry_kw, entry_ex));
  return dictionaryAtPutInValueCell(dictionary, key, value);
}

void Runtime::moduleAddBuiltinPrint(const Handle<Module>& module) {
  HandleScope scope;
  Handle<Function> print(
      &scope,
      newBuiltinFunction(
          nativeTrampoline<builtinPrint>,
          nativeTrampoline<builtinPrintKw>,
          unimplementedTrampoline));

  // Name
  Handle<Object> name(&scope, newStringFromCString("print"));
  print->setName(*name);

  Handle<Object> val(&scope, *print);
  moduleAddGlobal(module, name, val);
}

void Runtime::createBuiltinsModule() {
  HandleScope scope;
  Handle<Object> name(&scope, newStringFromCString("builtins"));
  Handle<Module> module(&scope, newModule(name));

  // Fill in builtins...
  build_class_ = moduleAddBuiltinFunction(
      module,
      symbols()->DunderBuildClass(),
      nativeTrampoline<builtinBuildClass>,
      nativeTrampoline<builtinBuildClassKw>,
      unimplementedTrampoline);
  moduleAddBuiltinPrint(module);
  moduleAddBuiltinFunction(
      module,
      symbols()->Ord(),
      nativeTrampoline<builtinOrd>,
      unimplementedTrampoline,
      unimplementedTrampoline);
  moduleAddBuiltinFunction(
      module,
      symbols()->Chr(),
      nativeTrampoline<builtinChr>,
      unimplementedTrampoline,
      unimplementedTrampoline);
  moduleAddBuiltinFunction(
      module,
      symbols()->Int(),
      nativeTrampoline<builtinInt>,
      unimplementedTrampoline,
      unimplementedTrampoline);
  moduleAddBuiltinFunction(
      module,
      symbols()->Range(),
      nativeTrampoline<builtinRange>,
      unimplementedTrampoline,
      unimplementedTrampoline);
  moduleAddBuiltinFunction(
      module,
      symbols()->IsInstance(),
      nativeTrampoline<builtinIsinstance>,
      unimplementedTrampoline,
      unimplementedTrampoline);
  moduleAddBuiltinFunction(
      module,
      symbols()->Len(),
      nativeTrampoline<builtinLen>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  // Add builtin types
  moduleAddBuiltinType(module, IntrinsicLayoutId::kDouble, symbols()->Float());
  moduleAddBuiltinType(
      module, IntrinsicLayoutId::kObject, symbols()->ObjectClassname());
  moduleAddBuiltinType(module, IntrinsicLayoutId::kList, symbols()->List());
  moduleAddBuiltinType(
      module, IntrinsicLayoutId::kClassMethod, symbols()->Classmethod());
  moduleAddBuiltinType(
      module, IntrinsicLayoutId::kStaticMethod, symbols()->StaticMethod());
  moduleAddBuiltinType(
      module, IntrinsicLayoutId::kDictionary, symbols()->Dict());
  moduleAddBuiltinType(module, IntrinsicLayoutId::kSuper, symbols()->Super());
  moduleAddBuiltinType(module, IntrinsicLayoutId::kType, symbols()->Type());

  Handle<Object> not_implemented_str(&scope, symbols()->NotImplemented());
  Handle<Object> not_implemented(&scope, notImplemented());
  moduleAddGlobal(module, not_implemented_str, not_implemented);

  addModule(module);
}

void Runtime::moduleAddBuiltinType(
    const Handle<Module>& module,
    IntrinsicLayoutId layout_id,
    Object* symbol) {
  HandleScope scope;
  Handle<Object> name(&scope, symbol);
  Handle<Object> value(&scope, classAt(layout_id));
  moduleAddGlobal(module, name, value);
}

void Runtime::createSysModule() {
  HandleScope scope;
  Handle<Object> name(&scope, symbols()->Sys());
  Handle<Module> module(&scope, newModule(name));

  Handle<Object> modules_id(&scope, newStringFromCString("modules"));
  Handle<Object> modules(&scope, modules_);
  moduleAddGlobal(module, modules_id, modules);

  // Fill in sys...
  moduleAddBuiltinFunction(
      module,
      symbols()->Exit(),
      nativeTrampoline<builtinSysExit>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  Handle<Object> stdout_id(&scope, symbols()->Stdout());
  Handle<Object> stdout_val(&scope, SmallInteger::fromWord(STDOUT_FILENO));
  moduleAddGlobal(module, stdout_id, stdout_val);

  Handle<Object> stderr_id(&scope, symbols()->Stderr());
  Handle<Object> stderr_val(&scope, SmallInteger::fromWord(STDERR_FILENO));
  moduleAddGlobal(module, stderr_id, stderr_val);
  addModule(module);
}

void Runtime::createWeakRefModule() {
  HandleScope scope;
  Handle<Object> name(&scope, symbols()->UnderWeakRef());
  Handle<Module> module(&scope, newModule(name));

  moduleAddBuiltinType(module, IntrinsicLayoutId::kWeakRef, symbols()->Ref());
  addModule(module);
}

void Runtime::createTimeModule() {
  HandleScope scope;
  Handle<Object> name(&scope, symbols()->Time());
  Handle<Module> module(&scope, newModule(name));

  // time.time
  Handle<Object> time(&scope, newStringFromCString("time"));
  moduleAddBuiltinFunction(
      module,
      *time,
      nativeTrampoline<builtinTime>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  addModule(module);
}

Object* Runtime::createMainModule() {
  HandleScope scope;
  Handle<Object> name(&scope, symbols()->DunderMain());
  Handle<Module> module(&scope, newModule(name));

  // Fill in __main__...

  addModule(module);

  return *module;
}

Object* Runtime::getIter(const Handle<Object>& iterable) {
  // TODO: Support other forms of iteration.
  if (iterable->isList()) {
    return newListIterator(iterable);
  } else if (iterable->isRange()) {
    return newRangeIterator(iterable);
  } else {
    UNIMPLEMENTED("GET_ITER only supported for List & Range");
  }
}

// List

void Runtime::listEnsureCapacity(const Handle<List>& list, word index) {
  if (index < list->capacity()) {
    return;
  }
  HandleScope scope;
  word new_capacity = (list->capacity() < kInitialEnsuredCapacity)
      ? kInitialEnsuredCapacity
      : list->capacity() << 1;
  Handle<ObjectArray> old_array(&scope, list->items());
  Handle<ObjectArray> new_array(&scope, newObjectArray(new_capacity));
  old_array->copyTo(*new_array);
  list->setItems(*new_array);
}

void Runtime::listAdd(const Handle<List>& list, const Handle<Object>& value) {
  HandleScope scope;
  word index = list->allocated();
  listEnsureCapacity(list, index);
  list->setAllocated(index + 1);
  list->atPut(index, *value);
}

void Runtime::listExtend(
    const Handle<List>& list,
    const Handle<Object>& iterable) {
  word index = list->allocated();
  HandleScope scope;
  if (iterable->isList()) {
    Handle<List> ext_list(&scope, *iterable);
    if (ext_list->allocated() > 0) {
      word new_capacity = index + ext_list->allocated();
      listEnsureCapacity(list, new_capacity);
      list->setAllocated(new_capacity);
      for (word i = 0; i < ext_list->allocated(); i++)
        list->atPut(index++, ext_list->at(i));
    }
  } else if (iterable->isListIterator()) {
    Handle<ListIterator> list_iter(&scope, *iterable);
    while (true) {
      Handle<Object> value(&scope, list_iter->next());
      if (value->isError())
        break;
      listAdd(list, value);
    }
  } else if (iterable->isObjectArray()) {
    Handle<ObjectArray> tuple(&scope, *iterable);
    if (tuple->length() > 0) {
      word new_capacity = index + tuple->length();
      listEnsureCapacity(list, new_capacity);
      list->setAllocated(new_capacity);
      for (word i = 0; i < tuple->length(); i++) {
        list->atPut(index++, tuple->at(i));
      }
    }
  } else {
    // TODO(T29780822): Add support for python iterators here.
    UNIMPLEMENTED(
        "List.extend only supports extending from "
        "List, ListIterator & Tuple");
  }
}

void Runtime::listInsert(
    const Handle<List>& list,
    const Handle<Object>& value,
    word index) {
  // TODO: Add insert(-x) where it inserts at pos: len(list) - x
  listAdd(list, value);
  word last_index = list->allocated() - 1;
  index =
      Utils::maximum(static_cast<word>(0), Utils::minimum(last_index, index));
  for (word i = last_index; i > index; i--) {
    list->atPut(i, list->at(i - 1));
  }
  list->atPut(index, *value);
}

Object* Runtime::listPop(const Handle<List>& list, word index) {
  HandleScope scope;
  Handle<Object> popped(&scope, list->at(index));
  list->atPut(index, None::object());
  word last_index = list->allocated() - 1;
  for (word i = index; i < last_index; i++) {
    list->atPut(i, list->at(i + 1));
  }
  list->setAllocated(list->allocated() - 1);
  return *popped;
}

Object*
Runtime::listReplicate(Thread* thread, const Handle<List>& list, word ntimes) {
  HandleScope scope(thread);
  word len = list->allocated();
  Handle<ObjectArray> items(&scope, newObjectArray(ntimes * len));
  for (word i = 0; i < ntimes; i++) {
    for (word j = 0; j < len; j++) {
      items->atPut((i * len) + j, list->at(j));
    }
  }
  Handle<List> result(&scope, newList());
  result->setItems(*items);
  result->setAllocated(items->length());
  return *result;
}

Object* Runtime::listSlice(
    const Handle<List>& list,
    const Handle<Slice>& slice) {
  word start, stop, step;
  slice->unpack(&start, &stop, &step);
  word length = Slice::adjustIndices(list->allocated(), &start, &stop, step);

  HandleScope scope;
  Handle<ObjectArray> items(&scope, newObjectArray(length));
  word index = start;
  for (word i = 0; i < length; i++) {
    items->atPut(i, list->at(index));
    index += step;
  }

  Handle<List> result(&scope, newList());
  result->setItems(*items);
  result->setAllocated(items->length());
  return *result;
}

char* Runtime::compile(const char* src) {
  // increment this if you change the caching code, to invalidate existing
  // cache entries.
  uint64_t seed[2] = {0, 1};
  word hash = 0;

  // Hash the input.
  ::siphash(
      reinterpret_cast<const uint8_t*>(src),
      strlen(src),
      reinterpret_cast<const uint8_t*>(seed),
      reinterpret_cast<uint8_t*>(&hash),
      sizeof(hash));

  const char* cache_env = OS::getenv("PYRO_CACHE_DIR");
  std::string cache_dir;
  if (cache_env != nullptr) {
    cache_dir = cache_env;
  } else {
    const char* home_env = OS::getenv("HOME");
    if (home_env != nullptr) {
      cache_dir = home_env;
      cache_dir += "/.pyro-compile-cache";
    }
  }

  char filename_buf[512] = {};
  snprintf(filename_buf, 512, "%s/%016zx", cache_dir.c_str(), hash);

  // Read compiled code from the cache
  if (!cache_dir.empty() && OS::fileExists(filename_buf)) {
    return OS::readFile(filename_buf);
  }

  // Cache miss, must run the compiler.
  std::unique_ptr<char[]> tmp_dir(OS::temporaryDirectory("python-tests"));
  const std::string dir(tmp_dir.get());
  const std::string py = dir + "/foo.py";
  const std::string pyc = dir + "/foo.pyc";
  const std::string cleanup = "rm -rf " + dir;
  std::ofstream output(py);
  output << src;
  output.close();
  const std::string command =
      "/usr/local/fbcode/gcc-5-glibc-2.23/bin/python3.6 -m compileall -q -b " +
      py;
  system(command.c_str());
  word len;
  char* result = OS::readFile(pyc.c_str(), &len);
  system(cleanup.c_str());

  // Cache the output if possible.
  if (!cache_dir.empty() && OS::dirExists(cache_dir.c_str())) {
    OS::writeFileExcl(filename_buf, result, len);
  }

  return result;
}

// Dictionary

// Helper class for manipulating buckets in the ObjectArray that backs the
// dictionary
class Bucket {
 public:
  Bucket(const Handle<ObjectArray>& data, word index)
      : data_(data), index_(index) {}

  Object* hash() {
    return data_->at(index_ + kHashOffset);
  }

  Object* key() {
    return data_->at(index_ + kKeyOffset);
  }

  Object* value() {
    return data_->at(index_ + kValueOffset);
  }

  void set(Object* hash, Object* key, Object* value) {
    data_->atPut(index_ + kHashOffset, hash);
    data_->atPut(index_ + kKeyOffset, key);
    data_->atPut(index_ + kValueOffset, value);
  }

  bool hasKey(Object* that_key) {
    return !hash()->isNone() && Object::equals(key(), that_key);
  }

  bool isTombstone() {
    return hash()->isNone() && !key()->isNone();
  }

  void setTombstone() {
    set(None::object(), Error::object(), None::object());
  }

  bool isEmpty() {
    return hash()->isNone() && key()->isNone();
  }

  bool isFilled() {
    return !(isEmpty() || isTombstone());
  }

  static word getIndex(Object* data, Object* hash) {
    word nbuckets = ObjectArray::cast(data)->length() / kNumPointers;
    DCHECK(Utils::isPowerOfTwo(nbuckets), "%ld is not a power of 2", nbuckets);
    word value = SmallInteger::cast(hash)->value();
    return (value & (nbuckets - 1)) * kNumPointers;
  }

  static const word kHashOffset = 0;
  static const word kKeyOffset = kHashOffset + 1;
  static const word kValueOffset = kKeyOffset + 1;
  static const word kNumPointers = kValueOffset + 1;

 private:
  const Handle<ObjectArray>& data_;
  word index_;

  DISALLOW_HEAP_ALLOCATION();
};

Object* Runtime::newDictionary() {
  HandleScope scope;
  Handle<Dictionary> result(&scope, heap()->createDictionary());
  result->setNumItems(0);
  result->setData(empty_object_array_);
  return *result;
}

Object* Runtime::newDictionary(word initial_size) {
  HandleScope scope;
  // TODO: initialSize should be scaled up by a load factor.
  word initial_capacity = Utils::nextPowerOfTwo(initial_size);
  Handle<ObjectArray> array(
      &scope,
      newObjectArray(
          Utils::maximum(
              static_cast<word>(kInitialDictionaryCapacity), initial_capacity) *
          Bucket::kNumPointers));
  Handle<Dictionary> result(&scope, newDictionary());
  result->setData(*array);
  return *result;
}

void Runtime::dictionaryAtPut(
    const Handle<Dictionary>& dict,
    const Handle<Object>& key,
    const Handle<Object>& value) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, dict->data());
  word index = -1;
  Handle<Object> key_hash(&scope, hash(*key));
  bool found = dictionaryLookup(data, key, key_hash, &index);
  if (index == -1) {
    // TODO(mpage): Grow at a predetermined load factor, rather than when full
    Handle<ObjectArray> new_data(&scope, dictionaryGrow(data));
    dictionaryLookup(new_data, key, key_hash, &index);
    DCHECK(index != -1, "invalid index %ld", index);
    dict->setData(*new_data);
    Bucket bucket(new_data, index);
    bucket.set(*key_hash, *key, *value);
  } else {
    Bucket bucket(data, index);
    bucket.set(*key_hash, *key, *value);
  }
  if (!found) {
    dict->setNumItems(dict->numItems() + 1);
  }
}

ObjectArray* Runtime::dictionaryGrow(const Handle<ObjectArray>& data) {
  HandleScope scope;
  word new_length = data->length() * kDictionaryGrowthFactor;
  if (new_length == 0) {
    new_length = kInitialDictionaryCapacity * Bucket::kNumPointers;
  }
  Handle<ObjectArray> new_data(&scope, newObjectArray(new_length));
  // Re-insert items
  for (word i = 0; i < data->length(); i += Bucket::kNumPointers) {
    Bucket old_bucket(data, i);
    if (old_bucket.isEmpty() || old_bucket.isTombstone()) {
      continue;
    }
    Handle<Object> key(&scope, old_bucket.key());
    Handle<Object> hash(&scope, old_bucket.hash());
    word index = -1;
    dictionaryLookup(new_data, key, hash, &index);
    DCHECK(index != -1, "invalid index %ld", index);
    Bucket new_bucket(new_data, index);
    new_bucket.set(*hash, *key, old_bucket.value());
  }
  return *new_data;
}

Object* Runtime::dictionaryAt(
    const Handle<Dictionary>& dict,
    const Handle<Object>& key) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, dict->data());
  word index = -1;
  Handle<Object> key_hash(&scope, hash(*key));
  bool found = dictionaryLookup(data, key, key_hash, &index);
  if (found) {
    DCHECK(index != -1, "invalid index %ld", index);
    return Bucket(data, index).value();
  }
  return Error::object();
}

Object* Runtime::dictionaryAtIfAbsentPut(
    const Handle<Dictionary>& dict,
    const Handle<Object>& key,
    Callback<Object*>* thunk) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, dict->data());
  word index = -1;
  Handle<Object> key_hash(&scope, hash(*key));
  bool found = dictionaryLookup(data, key, key_hash, &index);
  if (found) {
    DCHECK(index != -1, "invalid index %ld", index);
    return Bucket(data, index).value();
  }
  Handle<Object> value(&scope, thunk->call());
  if (index == -1) {
    // TODO(mpage): Grow at a predetermined load factor, rather than when full
    Handle<ObjectArray> new_data(&scope, dictionaryGrow(data));
    dictionaryLookup(new_data, key, key_hash, &index);
    DCHECK(index != -1, "invalid index %ld", index);
    dict->setData(*new_data);
    Bucket bucket(new_data, index);
    bucket.set(*key_hash, *key, *value);
  } else {
    Bucket bucket(data, index);
    bucket.set(*key_hash, *key, *value);
  }
  dict->setNumItems(dict->numItems() + 1);
  return *value;
}

Object* Runtime::dictionaryAtPutInValueCell(
    const Handle<Dictionary>& dict,
    const Handle<Object>& key,
    const Handle<Object>& value) {
  Object* result = dictionaryAtIfAbsentPut(dict, key, newValueCellCallback());
  ValueCell::cast(result)->setValue(*value);
  return result;
}

bool Runtime::dictionaryIncludes(
    const Handle<Dictionary>& dict,
    const Handle<Object>& key) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, dict->data());
  Handle<Object> key_hash(&scope, hash(*key));
  word ignore;
  return dictionaryLookup(data, key, key_hash, &ignore);
}

bool Runtime::dictionaryRemove(
    const Handle<Dictionary>& dict,
    const Handle<Object>& key,
    Object** value) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, dict->data());
  word index = -1;
  Handle<Object> key_hash(&scope, hash(*key));
  bool found = dictionaryLookup(data, key, key_hash, &index);
  if (found) {
    DCHECK(index != -1, "unexpected index %ld", index);
    Bucket bucket(data, index);
    *value = bucket.value();
    bucket.setTombstone();
    dict->setNumItems(dict->numItems() - 1);
  }
  return found;
}

bool Runtime::dictionaryLookup(
    const Handle<ObjectArray>& data,
    const Handle<Object>& key,
    const Handle<Object>& key_hash,
    word* index) {
  word start = Bucket::getIndex(*data, *key_hash);
  word current = start;
  word next_free_index = -1;

  // TODO(mpage) - Quadratic probing?
  word length = data->length();
  if (length == 0) {
    *index = -1;
    return false;
  }

  do {
    Bucket bucket(data, current);
    if (bucket.hasKey(*key)) {
      *index = current;
      return true;
    } else if (next_free_index == -1 && bucket.isTombstone()) {
      next_free_index = current;
    } else if (bucket.isEmpty()) {
      if (next_free_index == -1) {
        next_free_index = current;
      }
      break;
    }
    current = (current + Bucket::kNumPointers) % length;
  } while (current != start);

  *index = next_free_index;

  return false;
}

ObjectArray* Runtime::dictionaryKeys(const Handle<Dictionary>& dict) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, dict->data());
  Handle<ObjectArray> keys(&scope, newObjectArray(dict->numItems()));
  word num_keys = 0;
  for (word i = 0; i < data->length(); i += Bucket::kNumPointers) {
    Bucket bucket(data, i);
    if (bucket.isFilled()) {
      DCHECK(
          num_keys < keys->length(), "%ld ! < %ld", num_keys, keys->length());
      keys->atPut(num_keys, bucket.key());
      num_keys++;
    }
  }
  DCHECK(num_keys == keys->length(), "%ld != %ld", num_keys, keys->length());
  return *keys;
}

Object* Runtime::newSet() {
  HandleScope scope;
  Handle<Set> result(&scope, heap()->createSet());
  result->setNumItems(0);
  result->setData(empty_object_array_);
  return *result;
}

bool Runtime::setLookup(
    const Handle<ObjectArray>& data,
    const Handle<Object>& key,
    const Handle<Object>& key_hash,
    word* index) {
  word start = SetBucket::getIndex(*data, *key_hash);
  word current = start;
  word next_free_index = -1;

  // TODO(mpage) - Quadratic probing?
  word length = data->length();
  if (length == 0) {
    *index = -1;
    return false;
  }

  do {
    SetBucket bucket(data, current);
    if (bucket.hasKey(*key)) {
      *index = current;
      return true;
    } else if (next_free_index == -1 && bucket.isTombstone()) {
      next_free_index = current;
    } else if (bucket.isEmpty()) {
      if (next_free_index == -1) {
        next_free_index = current;
      }
      break;
    }
    current = (current + SetBucket::kNumPointers) % length;
  } while (current != start);

  *index = next_free_index;

  return false;
}

ObjectArray* Runtime::setGrow(const Handle<ObjectArray>& data) {
  HandleScope scope;
  word new_length = data->length() * kSetGrowthFactor;
  if (new_length == 0) {
    new_length = kInitialSetCapacity * SetBucket::kNumPointers;
  }
  Handle<ObjectArray> new_data(&scope, newObjectArray(new_length));
  // Re-insert items
  for (word i = 0; i < data->length(); i += SetBucket::kNumPointers) {
    SetBucket old_bucket(data, i);
    if (old_bucket.isEmpty() || old_bucket.isTombstone()) {
      continue;
    }
    Handle<Object> key(&scope, old_bucket.key());
    Handle<Object> hash(&scope, old_bucket.hash());
    word index = -1;
    setLookup(new_data, key, hash, &index);
    DCHECK(index != -1, "unexpected index %ld", index);
    SetBucket new_bucket(new_data, index);
    new_bucket.set(*hash, *key);
  }
  return *new_data;
}

Object* Runtime::setAdd(const Handle<Set>& set, const Handle<Object>& value) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, set->data());
  word index = -1;
  Handle<Object> key_hash(&scope, hash(*value));
  bool found = setLookup(data, value, key_hash, &index);
  if (found) {
    DCHECK(index != -1, "unexpected index %ld", index);
    return SetBucket(data, index).key();
  }
  if (index == -1) {
    // TODO(mpage): Grow at a predetermined load factor, rather than when full
    Handle<ObjectArray> new_data(&scope, setGrow(data));
    setLookup(new_data, value, key_hash, &index);
    DCHECK(index != -1, "unexpected index %ld", index);
    set->setData(*new_data);
    SetBucket bucket(new_data, index);
    bucket.set(*key_hash, *value);
  } else {
    SetBucket bucket(data, index);
    bucket.set(*key_hash, *value);
  }
  set->setNumItems(set->numItems() + 1);
  return *value;
}

bool Runtime::setIncludes(const Handle<Set>& set, const Handle<Object>& value) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, set->data());
  Handle<Object> key_hash(&scope, hash(*value));
  word ignore;
  return setLookup(data, value, key_hash, &ignore);
}

bool Runtime::setRemove(const Handle<Set>& set, const Handle<Object>& value) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, set->data());
  Handle<Object> key_hash(&scope, hash(*value));
  word index = -1;
  bool found = setLookup(data, value, key_hash, &index);
  if (found) {
    DCHECK(index != -1, "unexpected index %ld", index);
    SetBucket bucket(data, index);
    bucket.setTombstone();
    set->setNumItems(set->numItems() - 1);
  }
  return found;
}

Object* Runtime::newValueCell() {
  return heap()->createValueCell();
}

Object* Runtime::newWeakRef() {
  return heap()->createWeakRef();
}

void Runtime::collectAttributes(
    const Handle<Code>& code,
    const Handle<Dictionary>& attributes) {
  HandleScope scope;
  Handle<ByteArray> bc(&scope, code->code());
  Handle<ObjectArray> names(&scope, code->names());

  word len = bc->length();
  for (word i = 0; i < len - 3; i += 2) {
    // If the current instruction is EXTENDED_ARG we must skip it and the next
    // instruction.
    if (bc->byteAt(i) == Bytecode::EXTENDED_ARG) {
      i += 2;
      continue;
    }
    // Check for LOAD_FAST 0 (self)
    if (bc->byteAt(i) != Bytecode::LOAD_FAST || bc->byteAt(i + 1) != 0) {
      continue;
    }
    // Followed by a STORE_ATTR
    if (bc->byteAt(i + 2) != Bytecode::STORE_ATTR) {
      continue;
    }
    word name_index = bc->byteAt(i + 3);
    Handle<Object> name(&scope, names->at(name_index));
    dictionaryAtPut(attributes, name, name);
  }
}

Object* Runtime::classConstructor(const Handle<Class>& klass) {
  HandleScope scope;
  Handle<Dictionary> klass_dict(&scope, klass->dictionary());
  Handle<Object> init(&scope, symbols()->DunderInit());
  Object* value = dictionaryAt(klass_dict, init);
  if (value->isError()) {
    return None::object();
  }
  return ValueCell::cast(value)->value();
}

Object* Runtime::computeInitialLayout(
    Thread* thread,
    const Handle<Class>& klass) {
  HandleScope scope(thread);
  Handle<ObjectArray> mro(&scope, klass->mro());
  Handle<Dictionary> attrs(&scope, newDictionary());

  // Collect set of in-object attributes by scanning the __init__ method of
  // each class in the MRO
  for (word i = 0; i < mro->length(); i++) {
    Handle<Class> mro_klass(&scope, mro->at(i));
    Handle<Object> maybe_init(&scope, classConstructor(mro_klass));
    if (!maybe_init->isFunction()) {
      continue;
    }
    Handle<Function> init(maybe_init);
    Object* maybe_code = init->code();
    if (!maybe_code->isCode()) {
      continue;
    }
    Handle<Code> code(&scope, maybe_code);
    collectAttributes(code, attrs);
  }

  // Create the layout
  Handle<Layout> layout(&scope, newLayout());
  layout->setNumInObjectAttributes(attrs->numItems());

  return *layout;
}

Object* Runtime::lookupNameInMro(
    Thread* thread,
    const Handle<Class>& klass,
    const Handle<Object>& name) {
  HandleScope scope(thread);
  Handle<ObjectArray> mro(&scope, klass->mro());
  for (word i = 0; i < mro->length(); i++) {
    Handle<Class> mro_klass(&scope, mro->at(i));
    Handle<Dictionary> dict(&scope, mro_klass->dictionary());
    Handle<Object> value_cell(&scope, dictionaryAt(dict, name));
    if (!value_cell->isError()) {
      return ValueCell::cast(*value_cell)->value();
    }
  }
  return Error::object();
}

Object* Runtime::attributeAt(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name) {
  // A minimal implementation of getattr needed to get richards running.
  Object* result;
  if (receiver->isClass()) {
    result = classGetAttr(thread, receiver, name);
  } else if (receiver->isModule()) {
    result = moduleGetAttr(thread, receiver, name);
  } else if (receiver->isSuper()) {
    // TODO(T27518836): remove when we support __getattro__
    result = superGetAttr(thread, receiver, name);
  } else {
    // everything else should fallback to instance
    result = instanceGetAttr(thread, receiver, name);
  }
  return result;
}

Object* Runtime::attributeAtPut(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name,
    const Handle<Object>& value) {
  HandleScope scope(thread);
  Handle<Object> interned_name(&scope, internString(name));
  // A minimal implementation of setattr needed to get richards running.
  Object* result;
  if (receiver->isClass()) {
    result = classSetAttr(thread, receiver, interned_name, value);
  } else if (receiver->isModule()) {
    result = moduleSetAttr(thread, receiver, interned_name, value);
  } else {
    // everything else should fallback to instance
    result = instanceSetAttr(thread, receiver, interned_name, value);
  }
  return result;
}

Object* Runtime::stringConcat(
    const Handle<String>& left,
    const Handle<String>& right) {
  HandleScope scope;

  const word llen = left->length();
  const word rlen = right->length();
  const word new_len = llen + rlen;

  if (new_len <= SmallString::kMaxLength) {
    byte buffer[SmallString::kMaxLength];
    left->copyTo(buffer, llen);
    right->copyTo(buffer + llen, rlen);
    return SmallString::fromBytes(View<byte>(buffer, new_len));
  }

  Handle<String> result(
      &scope, LargeString::cast(heap()->createLargeString(new_len)));
  DCHECK(result->isLargeString(), "not a large string");
  const word address = HeapObject::cast(*result)->address();

  left->copyTo(reinterpret_cast<byte*>(address), llen);
  right->copyTo(reinterpret_cast<byte*>(address) + llen, rlen);
  return *result;
}

static word stringFormatBufferLength(
    const Handle<String>& fmt,
    const Handle<ObjectArray>& args) {
  word arg_idx = 0;
  word len = 0;
  for (word fmt_idx = 0; fmt_idx < fmt->length(); fmt_idx++, len++) {
    byte ch = fmt->charAt(fmt_idx);
    if (ch != '%') {
      continue;
    }
    switch (fmt->charAt(++fmt_idx)) {
      case 'd': {
        len--;
        CHECK(args->at(arg_idx)->isInteger(), "Argument mismatch");
        len += snprintf(
            nullptr, 0, "%ld", Integer::cast(args->at(arg_idx))->asWord());
        arg_idx++;
      } break;
      case 'g': {
        len--;
        CHECK(args->at(arg_idx)->isDouble(), "Argument mismatch");
        len += snprintf(
            nullptr, 0, "%g", Double::cast(args->at(arg_idx))->value());
        arg_idx++;
      } break;
      case 's': {
        len--;
        CHECK(args->at(arg_idx)->isString(), "Argument mismatch");
        len += String::cast(args->at(arg_idx))->length();
        arg_idx++;
      } break;
      case '%':
        break;
      default:
        UNIMPLEMENTED("Unsupported format specifier");
    }
  }
  return len;
}

static void stringFormatToBuffer(
    const Handle<String>& fmt,
    const Handle<ObjectArray>& args,
    char* dst,
    word len) {
  word arg_idx = 0;
  word dst_idx = 0;
  for (word fmt_idx = 0; fmt_idx < fmt->length(); fmt_idx++) {
    byte ch = fmt->charAt(fmt_idx);
    if (ch != '%') {
      dst[dst_idx++] = ch;
      continue;
    }
    switch (fmt->charAt(++fmt_idx)) {
      case 'd': {
        word value = Integer::cast(args->at(arg_idx++))->asWord();
        dst_idx += snprintf(&dst[dst_idx], len - dst_idx + 1, "%ld", value);
      } break;
      case 'g': {
        double value = Double::cast(args->at(arg_idx++))->value();
        dst_idx += snprintf(&dst[dst_idx], len - dst_idx + 1, "%g", value);
      } break;
      case 's': {
        String* value = String::cast(args->at(arg_idx));
        value->copyTo(reinterpret_cast<byte*>(&dst[dst_idx]), value->length());
        dst_idx += value->length();
        arg_idx++;
      } break;
      case '%':
        dst[dst_idx++] = '%';
        break;
      default:
        UNIMPLEMENTED("Unsupported format specifier");
    }
  }
  dst[len] = '\0';
}

// Initial implementation to support '%' operator for pystone.
Object* Runtime::stringFormat(
    Thread* thread,
    const Handle<String>& fmt,
    const Handle<ObjectArray>& args) {
  if (fmt->length() == 0) {
    return *fmt;
  }
  word len = stringFormatBufferLength(fmt, args);
  char* dst = static_cast<char*>(std::malloc(len + 1));
  CHECK(dst != nullptr, "Buffer allocation failure");
  stringFormatToBuffer(fmt, args, dst, len);
  Object* result = thread->runtime()->newStringFromCString(dst);
  std::free(dst);
  return result;
}

Object* Runtime::stringToInt(Thread* thread, const Handle<Object>& arg) {
  if (arg->isInteger()) {
    return *arg;
  }

  CHECK(arg->isString(), "not string type");
  HandleScope scope(thread);
  Handle<String> s(&scope, *arg);
  if (s->length() == 0) {
    return thread->throwValueErrorFromCString("invalid literal");
  }
  char* c_string = s->toCString(); // for strtol()
  char* end_ptr;
  errno = 0;
  long res = std::strtol(c_string, &end_ptr, 10);
  int saved_errno = errno;
  bool is_complete = (*end_ptr == '\0');
  free(c_string);
  if (!is_complete || (res == 0 && saved_errno == EINVAL)) {
    return thread->throwValueErrorFromCString("invalid literal");
  } else if ((res == LONG_MAX || res == LONG_MIN) && saved_errno == ERANGE) {
    return thread->throwValueErrorFromCString("invalid literal (range)");
  } else if (!SmallInteger::isValid(res)) {
    return thread->throwValueErrorFromCString("unsupported type");
  }
  return SmallInteger::fromWord(res);
}

Object* Runtime::computeFastGlobals(
    const Handle<Code>& code,
    const Handle<Dictionary>& globals,
    const Handle<Dictionary>& builtins) {
  HandleScope scope;
  Handle<ByteArray> bytes(&scope, code->code());
  Handle<ObjectArray> names(&scope, code->names());
  Handle<ObjectArray> fast_globals(&scope, newObjectArray(names->length()));
  for (word i = 0; i < bytes->length(); i += 2) {
    Bytecode bc = static_cast<Bytecode>(bytes->byteAt(i));
    word arg = bytes->byteAt(i + 1);
    while (bc == EXTENDED_ARG) {
      i += 2;
      bc = static_cast<Bytecode>(bytes->byteAt(i));
      arg = (arg << 8) | bytes->byteAt(i + 1);
    }
    if (bc != LOAD_GLOBAL && bc != STORE_GLOBAL && bc != DELETE_GLOBAL) {
      continue;
    }
    Handle<Object> key(&scope, names->at(arg));
    Object* value = dictionaryAt(globals, key);
    if (value->isError()) {
      value = dictionaryAt(builtins, key);
      if (value->isError()) {
        // insert a place holder to allow {STORE|DELETE}_GLOBAL
        Handle<Object> handle(&scope, value);
        value = dictionaryAtPutInValueCell(builtins, key, handle);
        ValueCell::cast(value)->makeUnbound();
      }
      Handle<Object> handle(&scope, value);
      value = dictionaryAtPutInValueCell(globals, key, handle);
    }
    DCHECK(value->isValueCell(), "not  value cell");
    fast_globals->atPut(arg, value);
  }
  return *fast_globals;
}

// See https://github.com/python/cpython/blob/master/Objects/lnotab_notes.txt
// for details about the line number table format
word Runtime::codeOffsetToLineNum(
    Thread* thread,
    const Handle<Code>& code,
    word offset) {
  HandleScope scope(thread);
  Handle<ByteArray> table(&scope, code->lnotab());
  word line = code->firstlineno();
  word cur_offset = 0;
  for (word i = 0; i < table->length(); i += 2) {
    cur_offset += table->byteAt(i);
    if (cur_offset > offset) {
      break;
    }
    line += static_cast<sbyte>(table->byteAt(i + 1));
  }
  return line;
}

Object* Runtime::isSubClass(
    const Handle<Class>& subclass,
    const Handle<Class>& superclass) {
  HandleScope scope;
  Handle<ObjectArray> mro(&scope, subclass->mro());
  for (word i = 0; i < mro->length(); i++) {
    if (mro->at(i) == *superclass) {
      return Boolean::trueObj();
    }
  }
  return Boolean::falseObj();
}

Object* Runtime::isInstance(
    const Handle<Object>& obj,
    const Handle<Class>& klass) {
  HandleScope scope;
  Handle<Class> obj_class(&scope, classOf(*obj));
  return isSubClass(obj_class, klass);
}

Object* Runtime::newClassMethod() {
  return heap()->createClassMethod();
}

Object* Runtime::newSuper() {
  return heap()->createSuper();
}

Object* Runtime::computeBuiltinBaseClass(const Handle<Class>& klass) {
  // The delegate class can only be one of the builtin bases including object.
  // We use the first non-object builtin base if any, throw if multiple.
  HandleScope scope;
  Handle<ObjectArray> mro(&scope, klass->mro());
  Handle<Class> object_klass(&scope, classAt(IntrinsicLayoutId::kObject));
  Handle<Class> candidate(&scope, *object_klass);
  for (word i = 0; i < mro->length(); i++) {
    Handle<Class> mro_klass(&scope, mro->at(i));
    if (!mro_klass->isIntrinsicOrExtension()) {
      continue;
    }
    if (*candidate == *object_klass) {
      candidate = *mro_klass;
    } else if (*mro_klass != *object_klass) {
      // TODO: throw TypeError
      CHECK(false, "multiple bases have instance lay-out conflict.");
    }
  }
  return *candidate;
}

Object* Runtime::instanceDelegate(const Handle<Object>& instance) {
  HandleScope scope;
  Handle<Layout> layout(&scope, layoutAt(instance->layoutId()));
  DCHECK(layout->hasDelegateSlot(), "instance layout missing delegate");
  return Instance::cast(*instance)->instanceVariableAt(
      layout->delegateOffset());
}

void Runtime::setInstanceDelegate(
    const Handle<Object>& instance,
    const Handle<Object>& delegate) {
  HandleScope scope;
  Handle<Layout> layout(&scope, layoutAt(instance->layoutId()));
  DCHECK(layout->hasDelegateSlot(), "instance layout missing delegate");
  return Instance::cast(*instance)->instanceVariableAtPut(
      layout->delegateOffset(), *delegate);
}

Object* Runtime::instanceAt(
    Thread* thread,
    const Handle<HeapObject>& instance,
    const Handle<Object>& name) {
  HandleScope scope(thread->handles());

  // Figure out where the attribute lives in the instance
  Handle<Layout> layout(&scope, layoutAt(instance->layoutId()));
  AttributeInfo info;
  if (!layoutFindAttribute(thread, layout, name, &info)) {
    return Error::object();
  }

  // Retrieve the attribute
  Object* result;
  if (info.isInObject()) {
    result = instance->instanceVariableAt(info.offset());
  } else {
    Handle<ObjectArray> overflow(
        &scope, instance->instanceVariableAt(layout->overflowOffset()));
    result = overflow->at(info.offset());
  }

  return result;
}

Object* Runtime::instanceAtPut(
    Thread* thread,
    const Handle<HeapObject>& instance,
    const Handle<Object>& name,
    const Handle<Object>& value) {
  HandleScope scope(thread->handles());

  // If the attribute doesn't exist we'll need to transition the layout
  bool has_new_layout_id = false;
  Handle<Layout> layout(&scope, layoutAt(instance->layoutId()));
  AttributeInfo info;
  if (!layoutFindAttribute(thread, layout, name, &info)) {
    // Transition the layout
    layout = layoutAddAttribute(thread, layout, name, 0);
    has_new_layout_id = true;

    bool found = layoutFindAttribute(thread, layout, name, &info);
    CHECK(found, "couldn't find attribute on new layout");
  }

  // Store the attribute
  if (info.isInObject()) {
    instance->instanceVariableAtPut(info.offset(), *value);
  } else {
    // Build the new overflow array
    Handle<ObjectArray> overflow(
        &scope, instance->instanceVariableAt(layout->overflowOffset()));
    Handle<ObjectArray> new_overflow(
        &scope, newObjectArray(overflow->length() + 1));
    overflow->copyTo(*new_overflow);
    new_overflow->atPut(info.offset(), *value);
    instance->instanceVariableAtPut(layout->overflowOffset(), *new_overflow);
  }

  if (has_new_layout_id) {
    instance->setHeader(instance->header()->withLayoutId(layout->id()));
  }

  return None::object();
}

Object* Runtime::layoutFollowEdge(
    const Handle<List>& edges,
    const Handle<Object>& label) {
  DCHECK(
      edges->allocated() % 2 == 0,
      "edges must contain an even number of elements");
  for (word i = 0; i < edges->allocated(); i++) {
    if (edges->at(i) == *label) {
      return edges->at(i + 1);
    }
  }
  return Error::object();
}

void Runtime::layoutAddEdge(
    const Handle<List>& edges,
    const Handle<Object>& label,
    const Handle<Object>& layout) {
  DCHECK(
      edges->allocated() % 2 == 0,
      "edges must contain an even number of elements");
  listAdd(edges, label);
  listAdd(edges, layout);
}

bool Runtime::layoutFindAttribute(
    Thread* thread,
    const Handle<Layout>& layout,
    const Handle<Object>& name,
    AttributeInfo* info) {
  HandleScope scope(thread->handles());
  Handle<Object> iname(&scope, internString(name));

  // Check in-object attributes
  Handle<ObjectArray> in_object(&scope, layout->inObjectAttributes());
  for (word i = 0; i < in_object->length(); i++) {
    Handle<ObjectArray> entry(&scope, in_object->at(i));
    if (entry->at(0) == *iname) {
      *info = AttributeInfo(entry->at(1));
      return true;
    }
  }

  // Check overflow attributes
  Handle<ObjectArray> overflow(&scope, layout->overflowAttributes());
  for (word i = 0; i < overflow->length(); i++) {
    Handle<ObjectArray> entry(&scope, overflow->at(i));
    if (entry->at(0) == *iname) {
      *info = AttributeInfo(entry->at(1));
      return true;
    }
  }

  return false;
}

Object* Runtime::layoutCreateChild(
    Thread* thread,
    const Handle<Layout>& layout) {
  HandleScope scope(thread->handles());
  Handle<Layout> new_layout(&scope, newLayout());
  std::memcpy(
      reinterpret_cast<byte*>(new_layout->address()),
      reinterpret_cast<byte*>(layout->address()),
      Layout::kSize);
  return *new_layout;
}

Object* Runtime::layoutAddAttributeEntry(
    Thread* thread,
    const Handle<ObjectArray>& entries,
    const Handle<Object>& name,
    AttributeInfo info) {
  HandleScope scope(thread);
  Handle<ObjectArray> new_entries(
      &scope, newObjectArray(entries->length() + 1));
  entries->copyTo(*new_entries);

  Handle<ObjectArray> entry(&scope, newObjectArray(2));
  entry->atPut(0, *name);
  entry->atPut(1, info.asSmallInteger());
  new_entries->atPut(entries->length(), *entry);

  return *new_entries;
}

Object* Runtime::layoutAddAttribute(
    Thread* thread,
    const Handle<Layout>& layout,
    const Handle<Object>& name,
    word flags) {
  HandleScope scope(thread->handles());
  Handle<Object> iname(&scope, internString(name));

  // Check if a edge for the attribute addition already exists
  Handle<List> edges(&scope, layout->additions());
  Object* result = layoutFollowEdge(edges, iname);
  if (!result->isError()) {
    return result;
  }

  // Create a new layout and figure out where to place the attribute
  Handle<Layout> new_layout(&scope, layoutCreateChild(thread, layout));
  Handle<ObjectArray> inobject(&scope, layout->inObjectAttributes());
  if (inobject->length() < layout->numInObjectAttributes()) {
    AttributeInfo info(
        inobject->length() * kPointerSize,
        flags | AttributeInfo::Flag::kInObject);
    new_layout->setInObjectAttributes(
        layoutAddAttributeEntry(thread, inobject, name, info));
  } else {
    Handle<ObjectArray> overflow(&scope, layout->overflowAttributes());
    AttributeInfo info(overflow->length(), flags);
    new_layout->setOverflowAttributes(
        layoutAddAttributeEntry(thread, overflow, name, info));
  }

  // Add the edge to the existing layout
  Handle<Object> value(&scope, *new_layout);
  layoutAddEdge(edges, iname, value);

  return *new_layout;
}

Object* Runtime::layoutDeleteAttribute(
    Thread* thread,
    const Handle<Layout>& layout,
    const Handle<Object>& name) {
  HandleScope scope(thread->handles());

  // See if the attribute exists
  AttributeInfo info;
  if (!layoutFindAttribute(thread, layout, name, &info)) {
    return Error::object();
  }

  // Check if an edge exists for removing the attribute
  Handle<Object> iname(&scope, internString(name));
  Handle<List> edges(&scope, layout->deletions());
  Object* next_layout = layoutFollowEdge(edges, iname);
  if (!next_layout->isError()) {
    return next_layout;
  }

  // No edge was found, create a new layout and add an edge
  Handle<Layout> new_layout(&scope, layoutCreateChild(thread, layout));
  if (info.isInObject()) {
    // The attribute to be deleted was an in-object attribute, mark it as
    // deleted
    Handle<ObjectArray> old_inobject(&scope, layout->inObjectAttributes());
    Handle<ObjectArray> new_inobject(
        &scope, newObjectArray(old_inobject->length()));
    for (word i = 0; i < old_inobject->length(); i++) {
      Handle<ObjectArray> entry(&scope, old_inobject->at(i));
      if (entry->at(0) == *iname) {
        entry = newObjectArray(2);
        entry->atPut(0, None::object());
        entry->atPut(
            1,
            AttributeInfo(0, AttributeInfo::Flag::kDeleted).asSmallInteger());
      }
      new_inobject->atPut(i, *entry);
    }
    new_layout->setInObjectAttributes(*new_inobject);
  } else {
    // The attribute to be deleted was an overflow attribute, omit it from the
    // new overflow array
    Handle<ObjectArray> old_overflow(&scope, layout->overflowAttributes());
    Handle<ObjectArray> new_overflow(
        &scope, newObjectArray(old_overflow->length() - 1));
    bool is_deleted = false;
    for (word i = 0, j = 0; i < old_overflow->length(); i++) {
      Handle<ObjectArray> entry(&scope, old_overflow->at(i));
      if (entry->at(0) == *iname) {
        is_deleted = true;
        continue;
      }
      if (is_deleted) {
        // Need to shift everything down by 1 once we've deleted the attribute
        entry = newObjectArray(2);
        entry->atPut(0, ObjectArray::cast(old_overflow->at(i))->at(0));
        entry->atPut(1, AttributeInfo(j, info.flags()).asSmallInteger());
      }
      new_overflow->atPut(j, *entry);
      j++;
    }
    new_layout->setOverflowAttributes(*new_overflow);
  }

  // Add the edge to the existing layout
  Handle<Object> value(&scope, *new_layout);
  layoutAddEdge(edges, iname, value);

  return *new_layout;
}

void Runtime::initializeSuperClass() {
  HandleScope scope;
  Handle<Class> super(
      &scope, initializeHeapClass("super", IntrinsicLayoutId::kSuper));

  classAddBuiltinFunction(
      super,
      symbols()->DunderNew(),
      nativeTrampoline<builtinSuperNew>,
      unimplementedTrampoline,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      super,
      symbols()->DunderInit(),
      nativeTrampoline<builtinSuperInit>,
      unimplementedTrampoline,
      unimplementedTrampoline);
}

Object* Runtime::superGetAttr(
    Thread* thread,
    const Handle<Object>& receiver,
    const Handle<Object>& name) {
  HandleScope scope(thread);
  Handle<Super> super(&scope, *receiver);
  Handle<Class> start_type(&scope, super->objectType());
  Handle<ObjectArray> mro(&scope, start_type->mro());
  word i;
  for (i = 0; i < mro->length(); i++) {
    if (super->type() == mro->at(i)) {
      // skip super->type (if any)
      i++;
      break;
    }
  }
  for (; i < mro->length(); i++) {
    Handle<Class> klass(&scope, mro->at(i));
    Handle<Dictionary> dict(&scope, klass->dictionary());
    Handle<Object> value_cell(&scope, dictionaryAt(dict, name));
    if (value_cell->isError()) {
      continue;
    }
    Handle<Object> value(&scope, ValueCell::cast(*value_cell)->value());
    if (!isNonDataDescriptor(thread, value)) {
      return *value;
    } else {
      Handle<Object> self(&scope, None::object());
      if (super->object() != *start_type) {
        self = super->object();
      }
      Handle<Object> owner(&scope, *start_type);
      return Interpreter::callDescriptorGet(
          thread, thread->currentFrame(), value, self, owner);
    }
  }
  // fallback to normal instance getattr
  return instanceGetAttr(thread, receiver, name);
}

ApiHandle* Runtime::asApiHandle(Object* obj) {
  HandleScope scope;
  Handle<Object> key(&scope, obj);
  Handle<Dictionary> dict(&scope, apiHandles());
  Object* value = dictionaryAt(dict, key);
  if (value->isError()) {
    ApiHandle* handle = ApiHandle::New(obj);
    Handle<Object> object(
        &scope, newIntegerFromCPointer(static_cast<void*>(handle)));
    dictionaryAtPut(dict, key, object);
    return handle;
  }
  return static_cast<ApiHandle*>(Integer::cast(value)->asCPointer());
}

ApiHandle* Runtime::asBorrowedApiHandle(Object* obj) {
  HandleScope scope;
  Handle<Object> key(&scope, obj);
  Handle<Dictionary> dict(&scope, apiHandles());
  Object* value = dictionaryAt(dict, key);
  if (value->isError()) {
    ApiHandle* handle = ApiHandle::NewBorrowed(obj);
    Handle<Object> object(
        &scope, newIntegerFromCPointer(static_cast<void*>(handle)));
    dictionaryAtPut(dict, key, object);
    return handle;
  }
  return static_cast<ApiHandle*>(Integer::cast(value)->asCPointer());
}

Object* Runtime::asObject(PyObject* py_obj) {
  return reinterpret_cast<ApiHandle*>(py_obj)->asObject();
}

void Runtime::deallocApiHandles() {
  HandleScope scope;
  Handle<Dictionary> dict(&scope, apiHandles());
  Handle<ObjectArray> keys(&scope, dictionaryKeys(dict));
  for (word i = 0; i < keys->length(); i++) {
    Handle<Object> key(&scope, keys->at(i));
    Object* value = dictionaryAt(dict, key);
    delete static_cast<ApiHandle*>(Integer::cast(value)->asCPointer());
  }
}

} // namespace python
