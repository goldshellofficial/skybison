#include "runtime.h"

#include <cstring>
#include <fstream>
#include <memory>

#include "builtins.h"
#include "bytecode.h"
#include "callback.h"
#include "globals.h"
#include "handles.h"
#include "heap.h"
#include "interpreter.h"
#include "layout.h"
#include "marshal.h"
#include "os.h"
#include "scavenger.h"
#include "siphash.h"
#include "thread.h"
#include "trampolines-inl.h"
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
}

Runtime::Runtime() : Runtime(64 * MiB) {}

Runtime::~Runtime() {
  for (Thread* thread = threads_; thread != nullptr;) {
    if (thread == Thread::currentThread()) {
      Thread::setCurrentThread(nullptr);
    } else {
      assert(0); // Not implemented.
    }
    auto prev = thread;
    thread = thread->next();
    delete prev;
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
  CHECK(
      layout_id >= static_cast<word>(IntrinsicLayoutId::kObject) ||
          layout_id == IntrinsicLayoutId::kSmallInteger || (layout_id & 1) == 1,
      "kSmallInteger must be the only even immediate layout id");
  HandleScope scope;
  Handle<Layout> layout(&scope, heap()->createLayout(layout_id));
  layout->setInObjectAttributes(empty_object_array_);
  layout->setOverflowAttributes(empty_object_array_);
  layout->setAdditions(newList());
  layout->setDeletions(newList());
  List::cast(layouts_)->atPut(layout_id, *layout);
  return *layout;
}

Object* Runtime::newByteArray(word length, byte fill) {
  assert(length >= 0);
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
    CHECK(false, "custom descriptors are unsupported");
  }

  // No data descriptor found on the meta class, look in the mro of the klass
  Handle<Object> attr(&scope, lookupNameInMro(thread, klass, name));
  if (!attr->isError()) {
    if (attr->isFunction()) {
      Handle<Object> none(&scope, None::object());
      return functionDescriptorGet(thread, attr, none, receiver);
    } else if (attr->isClassMethod()) {
      Handle<Object> none(&scope, None::object());
      return classmethodDescriptorGet(thread, attr, none, receiver);
    } else if (isNonDataDescriptor(thread, attr)) {
      // TODO(T25692531): Call __get__ from meta_attr
      CHECK(false, "custom descriptors are unsupported");
    }
    return *attr;
  }

  // No attr found in klass or its mro, use the non-data descriptor found in
  // the metaclass (if any).
  if (isNonDataDescriptor(thread, meta_attr)) {
    if (meta_attr->isFunction()) {
      Handle<Object> mk(&scope, *meta_klass);
      return functionDescriptorGet(thread, meta_attr, receiver, mk);
    } else if (meta_attr->isClassMethod()) {
      Handle<Object> mk(&scope, *meta_klass);
      return classmethodDescriptorGet(thread, meta_attr, receiver, mk);
    } else {
      // TODO(T25692531): Call __get__ from meta_attr
      CHECK(false, "custom descriptors are unsupported");
    }
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
    CHECK(false, "custom descriptors are unsupported");
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
    if (klass_attr->isFunction()) {
      Handle<Object> k(&scope, *klass);
      return functionDescriptorGet(thread, klass_attr, receiver, k);
    } else if (klass_attr->isClassMethod()) {
      Handle<Object> k(&scope, *klass);
      return classmethodDescriptorGet(thread, klass_attr, receiver, k);
    }
    // TODO(T25692531): Call __get__ from klass_attr
    UNIMPLEMENTED("custom descriptors are unsupported");
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
  if (object->isFunction() || object->isClassMethod() || object->isError()) {
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
  if (object->isFunction() || object->isClassMethod()) {
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
    Function::Entry entryKw) {
  Object* result = heap()->createFunction();
  assert(result != nullptr);
  auto function = Function::cast(result);
  function->setEntry(entry);
  function->setEntryKw(entryKw);
  return result;
}

Object* Runtime::newFunction() {
  Object* object = heap()->createFunction();
  assert(object != nullptr);
  auto function = Function::cast(object);
  function->setEntry(unimplementedTrampoline);
  function->setEntryKw(unimplementedTrampoline);
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
      (num_words - 1) * kPointerSize, empty_object_array_);
  return *instance;
}

void Runtime::classAddBuiltinFunction(
    const Handle<Class>& klass,
    Object* name,
    Function::Entry entry,
    Function::Entry entryKw) {
  HandleScope scope;
  Handle<Object> key(&scope, name);
  Handle<Object> value(&scope, newBuiltinFunction(entry, entryKw));
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
  CHECK(
      start->isNone() && stop->isNone() && step->isNone(),
      "Only empty slice supported.");
  HandleScope scope;
  Handle<Slice> slice(&scope, heap()->createSlice());
  slice->setStart(*start);
  slice->setStop(*stop);
  slice->setStep(*step);
  return *slice;
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
  assert(result != nullptr);
  byte* dst = reinterpret_cast<byte*>(LargeString::cast(result)->address());
  const byte* src = code_units.data();
  memcpy(dst, src, length);
  return result;
}

Object* Runtime::internString(const Handle<Object>& string) {
  HandleScope scope;
  Handle<Set> set(&scope, interned());
  Handle<Object> key(&scope, *string);
  assert(string->isString());
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
    assert(code == src->header()->hashCode());
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
  assert(allocated < array->length());
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
  initializeHeapClass("object");
  initializeHeapClass("byteArray", IntrinsicLayoutId::kByteArray);
  initializeHeapClass("code", IntrinsicLayoutId::kCode);
  initializeHeapClass("dictionary", IntrinsicLayoutId::kDictionary);
  initializeHeapClass("double", IntrinsicLayoutId::kDouble);
  initializeHeapClass("ellipsis", IntrinsicLayoutId::kEllipsis);
  initializeHeapClass("function", IntrinsicLayoutId::kFunction);
  initializeHeapClass("integer", IntrinsicLayoutId::kLargeInteger);
  initializeHeapClass("layout", IntrinsicLayoutId::kLayout);
  initializeHeapClass("list_iterator", IntrinsicLayoutId::kListIterator);
  initializeHeapClass("method", IntrinsicLayoutId::kBoundMethod);
  initializeHeapClass("module", IntrinsicLayoutId::kModule);
  initializeHeapClass("objectarray", IntrinsicLayoutId::kObjectArray);
  initializeHeapClass("str", IntrinsicLayoutId::kLargeString);
  initializeHeapClass("range", IntrinsicLayoutId::kRange);
  initializeHeapClass("range_iterator", IntrinsicLayoutId::kRangeIterator);
  initializeHeapClass("slice", IntrinsicLayoutId::kSlice);
  initializeHeapClass("type", IntrinsicLayoutId::kType);
  initializeHeapClass("valuecell", IntrinsicLayoutId::kValueCell);
  initializeHeapClass("weakref", IntrinsicLayoutId::kWeakRef);
  initializeListClass();
  initializeClassMethodClass();
}

void Runtime::initializeListClass() {
  HandleScope scope;
  Handle<Class> list(
      &scope, initializeHeapClass("list", IntrinsicLayoutId::kList));

  classAddBuiltinFunction(
      list,
      symbols()->Append(),
      nativeTrampoline<builtinListAppend>,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      list,
      symbols()->Insert(),
      nativeTrampoline<builtinListInsert>,
      unimplementedTrampoline);

  classAddBuiltinFunction(
      list,
      symbols()->DunderNew(),
      nativeTrampoline<builtinListNew>,
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
      unimplementedTrampoline);

  classAddBuiltinFunction(
      classmethod,
      symbols()->DunderNew(),
      nativeTrampoline<builtinClassMethodNew>,
      unimplementedTrampoline);
}

void Runtime::initializeImmediateClasses() {
  initializeHeapClass(
      "bool", IntrinsicLayoutId::kBoolean, IntrinsicLayoutId::kLargeInteger);
  initializeHeapClass("NoneType", IntrinsicLayoutId::kNone);
  initializeHeapClass(
      "smallstr",
      IntrinsicLayoutId::kSmallString,
      IntrinsicLayoutId::kLargeInteger);
  initializeSmallIntClass();
}

void Runtime::initializeSmallIntClass() {
  HandleScope scope;
  Handle<Class> small_integer(
      &scope,
      initializeHeapClass(
          "smallint",
          IntrinsicLayoutId::kSmallInteger,
          IntrinsicLayoutId::kLargeInteger,
          IntrinsicLayoutId::kObject));
  // We want to lookup the class of an immediate type by using the 5-bit tag
  // value as an index into the class table.  Replicate the class object for
  // SmallInteger to all locations that decode to a SmallInteger tag.
  for (word i = 1; i < 16; i++) {
    assert(List::cast(layouts_)->at(i << 1) == None::object());
    List::cast(layouts_)->atPut(i << 1, *small_integer);
  }
}

void Runtime::collectGarbage() {
  Scavenger(this).scavenge();
}

Object* Runtime::run(const char* buffer) {
  HandleScope scope;

  Handle<Module> main(&scope, createMainModule());
  return executeModule(buffer, main);
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
  assert(code->argcount() == 0);

  return Thread::currentThread()->runModuleFunction(*module, *code);
}

Object* Runtime::importModule(const Handle<Object>& name) {
  HandleScope scope;
  Handle<Object> cached_module(&scope, findModule(name));
  if (!cached_module->isNone()) {
    return *cached_module;
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
  visitor->visitPointer(&build_class_);
  visitor->visitPointer(&print_default_end_);

  // Visit interned strings.
  visitor->visitPointer(&interned_);

  // Visit modules
  visitor->visitPointer(&modules_);

  // Visit symbols
  symbols_->visit(visitor);
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
  assert(name->isString());

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
  CHECK(
      result <= Header::kMaxLayoutId,
      "exceeded layout id space in header word");
  listAdd(list, value);
  return result;
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
    const Function::Entry entryKw) {
  HandleScope scope;
  Handle<Object> key(&scope, name);
  Handle<Dictionary> dictionary(&scope, module->dictionary());
  Handle<Object> value(&scope, newBuiltinFunction(entry, entryKw));
  return dictionaryAtPutInValueCell(dictionary, key, value);
}

void Runtime::moduleAddBuiltinPrint(const Handle<Module>& module) {
  HandleScope scope;
  Handle<Function> print(
      &scope,
      newBuiltinFunction(
          nativeTrampoline<builtinPrint>, nativeTrampoline<builtinPrintKw>));

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
      nativeTrampoline<unimplementedTrampoline>);
  moduleAddBuiltinPrint(module);
  moduleAddBuiltinFunction(
      module,
      symbols()->Ord(),
      nativeTrampoline<builtinOrd>,
      nativeTrampoline<unimplementedTrampoline>);
  moduleAddBuiltinFunction(
      module,
      symbols()->Chr(),
      nativeTrampoline<builtinChr>,
      nativeTrampoline<unimplementedTrampoline>);
  moduleAddBuiltinFunction(
      module,
      symbols()->Range(),
      nativeTrampoline<builtinRange>,
      nativeTrampoline<unimplementedTrampoline>);
  moduleAddBuiltinFunction(
      module,
      symbols()->IsInstance(),
      nativeTrampoline<builtinIsinstance>,
      nativeTrampoline<unimplementedTrampoline>);
  moduleAddBuiltinFunction(
      module,
      symbols()->Len(),
      nativeTrampoline<builtinLen>,
      nativeTrampoline<unimplementedTrampoline>);

  // Add builtin types
  moduleAddBuiltinType(
      module, IntrinsicLayoutId::kObject, symbols()->ObjectClassname());
  moduleAddBuiltinType(module, IntrinsicLayoutId::kList, symbols()->List());
  moduleAddBuiltinType(
      module, IntrinsicLayoutId::kClassMethod, symbols()->Classmethod());
  moduleAddBuiltinType(
      module, IntrinsicLayoutId::kDictionary, symbols()->Dict());

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
  Handle<Object> name(&scope, newStringFromCString("sys"));
  Handle<Module> module(&scope, newModule(name));

  Handle<Object> modules_id(&scope, newStringFromCString("modules"));
  Handle<Object> modules(&scope, modules_);
  moduleAddGlobal(module, modules_id, modules);

  // Fill in sys...
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

ObjectArray* Runtime::ensureCapacity(
    const Handle<ObjectArray>& array,
    word index) {
  HandleScope scope;
  word capacity = array->length();
  if (index < capacity) {
    return *array;
  }
  word newCapacity = (capacity == 0) ? kInitialEnsuredCapacity : capacity << 1;
  Handle<ObjectArray> newArray(&scope, newObjectArray(newCapacity));
  array->copyTo(*newArray);
  return *newArray;
}

void Runtime::listAdd(const Handle<List>& list, const Handle<Object>& value) {
  HandleScope scope;
  word index = list->allocated();
  Handle<ObjectArray> items(&scope, list->items());
  Handle<ObjectArray> newItems(&scope, ensureCapacity(items, index));
  if (*items != *newItems) {
    list->setItems(*newItems);
  }
  list->setAllocated(index + 1);
  list->atPut(index, *value);
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

void Runtime::listPop(const Handle<List>& list, word index) {
  word last_index = list->allocated() - 1;
  if (index < 0) {
    index = last_index + index;
  }
  if (index < 0 || index > last_index) {
    // TODO(T27365047): Raise an exception
    UNIMPLEMENTED("Throw an IndexError for an out of range list index.");
  }
  for (word i = index; i < last_index; i++) {
    list->atPut(i, list->at(i + 1));
  }
  list->setAllocated(list->allocated() - 1);
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
    Thread* thread,
    const Handle<List>& list,
    const Handle<Slice>& slice) {
  CHECK(
      slice->start()->isNone() && slice->stop()->isNone() &&
          slice->step()->isNone(),
      "Only empty slice supported.");
  return thread->runtime()->listReplicate(thread, list, 1);
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
  std::unique_ptr<char[]> tmpDir(OS::temporaryDirectory("python-tests"));
  const std::string dir(tmpDir.get());
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

  inline Object* hash() {
    return data_->at(index_ + kHashOffset);
  }

  inline Object* key() {
    return data_->at(index_ + kKeyOffset);
  }

  inline Object* value() {
    return data_->at(index_ + kValueOffset);
  }

  inline void set(Object* hash, Object* key, Object* value) {
    data_->atPut(index_ + kHashOffset, hash);
    data_->atPut(index_ + kKeyOffset, key);
    data_->atPut(index_ + kValueOffset, value);
  }

  inline bool hasKey(Object* thatKey) {
    return !hash()->isNone() && Object::equals(key(), thatKey);
  }

  inline bool isTombstone() {
    return hash()->isNone() && !key()->isNone();
  }

  inline void setTombstone() {
    set(None::object(), Error::object(), None::object());
  }

  inline bool isEmpty() {
    return hash()->isNone() && key()->isNone();
  }

  bool isFilled() {
    return !(isEmpty() || isTombstone());
  }

  static inline word getIndex(Object* data, Object* hash) {
    word nbuckets = ObjectArray::cast(data)->length() / kNumPointers;
    assert(Utils::isPowerOfTwo(nbuckets));
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

// Set

// Helper class for manipulating buckets in the ObjectArray that backs the
// Set, it has one less slot than Bucket.
class SetBucket {
 public:
  SetBucket(const Handle<ObjectArray>& data, word index)
      : data_(data), index_(index) {}

  inline Object* hash() {
    return data_->at(index_ + kHashOffset);
  }

  inline Object* key() {
    return data_->at(index_ + kKeyOffset);
  }

  inline void set(Object* hash, Object* key) {
    data_->atPut(index_ + kHashOffset, hash);
    data_->atPut(index_ + kKeyOffset, key);
  }

  inline bool hasKey(Object* thatKey) {
    return !hash()->isNone() && Object::equals(key(), thatKey);
  }

  inline bool isTombstone() {
    return hash()->isNone() && !key()->isNone();
  }

  inline void setTombstone() {
    set(None::object(), Error::object());
  }

  inline bool isEmpty() {
    return hash()->isNone() && key()->isNone();
  }

  static inline word getIndex(Object* data, Object* hash) {
    word nbuckets = ObjectArray::cast(data)->length() / kNumPointers;
    assert(Utils::isPowerOfTwo(nbuckets));
    word value = SmallInteger::cast(hash)->value();
    return (value & (nbuckets - 1)) * kNumPointers;
  }

  static const word kHashOffset = 0;
  static const word kKeyOffset = kHashOffset + 1;
  static const word kNumPointers = kKeyOffset + 1;

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

Object* Runtime::newDictionary(word initialSize) {
  HandleScope scope;
  // TODO: initialSize should be scaled up by a load factor.
  word initialCapacity = Utils::nextPowerOfTwo(initialSize);
  Handle<ObjectArray> array(
      &scope,
      newObjectArray(
          Utils::maximum(
              static_cast<word>(kInitialDictionaryCapacity), initialCapacity) *
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
    Handle<ObjectArray> newData(&scope, dictionaryGrow(data));
    dictionaryLookup(newData, key, key_hash, &index);
    assert(index != -1);
    dict->setData(*newData);
    Bucket bucket(newData, index);
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
  word newLength = data->length() * kDictionaryGrowthFactor;
  if (newLength == 0) {
    newLength = kInitialDictionaryCapacity * Bucket::kNumPointers;
  }
  Handle<ObjectArray> newData(&scope, newObjectArray(newLength));
  // Re-insert items
  for (word i = 0; i < data->length(); i += Bucket::kNumPointers) {
    Bucket oldBucket(data, i);
    if (oldBucket.isEmpty() || oldBucket.isTombstone()) {
      continue;
    }
    Handle<Object> key(&scope, oldBucket.key());
    Handle<Object> hash(&scope, oldBucket.hash());
    word index = -1;
    dictionaryLookup(newData, key, hash, &index);
    assert(index != -1);
    Bucket newBucket(newData, index);
    newBucket.set(*hash, *key, oldBucket.value());
  }
  return *newData;
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
    assert(index != -1);
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
    assert(index != -1);
    return Bucket(data, index).value();
  }
  Handle<Object> value(&scope, thunk->call());
  if (index == -1) {
    // TODO(mpage): Grow at a predetermined load factor, rather than when full
    Handle<ObjectArray> new_data(&scope, dictionaryGrow(data));
    dictionaryLookup(new_data, key, key_hash, &index);
    assert(index != -1);
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
    assert(index != -1);
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
  word nextFreeIndex = -1;

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
    } else if (nextFreeIndex == -1 && bucket.isTombstone()) {
      nextFreeIndex = current;
    } else if (bucket.isEmpty()) {
      if (nextFreeIndex == -1) {
        nextFreeIndex = current;
      }
      break;
    }
    current = (current + Bucket::kNumPointers) % length;
  } while (current != start);

  *index = nextFreeIndex;

  return false;
}

ObjectArray* Runtime::dictionaryKeys(const Handle<Dictionary>& dict) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, dict->data());
  Handle<ObjectArray> keys(&scope, newObjectArray(dict->numItems()));
  word numKeys = 0;
  for (word i = 0; i < data->length(); i += Bucket::kNumPointers) {
    Bucket bucket(data, i);
    if (bucket.isFilled()) {
      assert(numKeys < keys->length());
      keys->atPut(numKeys, bucket.key());
      numKeys++;
    }
  }
  assert(numKeys == keys->length());
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
  word nextFreeIndex = -1;

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
    } else if (nextFreeIndex == -1 && bucket.isTombstone()) {
      nextFreeIndex = current;
    } else if (bucket.isEmpty()) {
      if (nextFreeIndex == -1) {
        nextFreeIndex = current;
      }
      break;
    }
    current = (current + SetBucket::kNumPointers) % length;
  } while (current != start);

  *index = nextFreeIndex;

  return false;
}

ObjectArray* Runtime::setGrow(const Handle<ObjectArray>& data) {
  HandleScope scope;
  word newLength = data->length() * kSetGrowthFactor;
  if (newLength == 0) {
    newLength = kInitialSetCapacity * SetBucket::kNumPointers;
  }
  Handle<ObjectArray> newData(&scope, newObjectArray(newLength));
  // Re-insert items
  for (word i = 0; i < data->length(); i += SetBucket::kNumPointers) {
    SetBucket oldBucket(data, i);
    if (oldBucket.isEmpty() || oldBucket.isTombstone()) {
      continue;
    }
    Handle<Object> key(&scope, oldBucket.key());
    Handle<Object> hash(&scope, oldBucket.hash());
    word index = -1;
    setLookup(newData, key, hash, &index);
    assert(index != -1);
    SetBucket newBucket(newData, index);
    newBucket.set(*hash, *key);
  }
  return *newData;
}

Object* Runtime::setAdd(const Handle<Set>& set, const Handle<Object>& value) {
  HandleScope scope;
  Handle<ObjectArray> data(&scope, set->data());
  word index = -1;
  Handle<Object> key_hash(&scope, hash(*value));
  bool found = setLookup(data, value, key_hash, &index);
  if (found) {
    assert(index != -1);
    return SetBucket(data, index).key();
  }
  if (index == -1) {
    // TODO(mpage): Grow at a predetermined load factor, rather than when full
    Handle<ObjectArray> newData(&scope, setGrow(data));
    setLookup(newData, value, key_hash, &index);
    assert(index != -1);
    set->setData(*newData);
    SetBucket bucket(newData, index);
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
    assert(index != -1);
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
  Handle<ObjectArray> names(&scope, dictionaryKeys(attrs));
  layoutInitializeInObjectAttributes(thread, layout, names);

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

bool Runtime::isTruthy(Object* object) {
  if (object->isBoolean()) {
    return Boolean::cast(object)->value();
  } else if (object->isInteger()) {
    return Integer::cast(object)->asWord() > 0;
  }
  UNIMPLEMENTED("Unsupported type");
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
  assert(result->isLargeString());
  const word address = HeapObject::cast(*result)->address();

  left->copyTo(reinterpret_cast<byte*>(address), llen);
  right->copyTo(reinterpret_cast<byte*>(address) + llen, rlen);
  return *result;
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
    if (bc != LOAD_GLOBAL && bc != STORE_GLOBAL && bc != DELETE_GLOBAL &&
        bc != LOAD_NAME) {
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
    assert(value->isValueCell());
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
      return Boolean::fromBool(true);
    }
  }
  return Boolean::fromBool(false);
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
  CHECK(layout->hasDelegateSlot(), "instance layout missing delegate");
  return Instance::cast(*instance)->instanceVariableAt(
      layout->delegateOffset());
}

void Runtime::setInstanceDelegate(
    const Handle<Object>& instance,
    const Handle<Object>& delegate) {
  HandleScope scope;
  Handle<Layout> layout(&scope, layoutAt(instance->layoutId()));
  CHECK(layout->hasDelegateSlot(), "instance layout missing delegate");
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
  Object* result = layoutFindAttribute(thread, layout, name);
  if (result->isError()) {
    return result;
  }

  // Retrieve the attribute
  AttributeInfo info(result);
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

  // If the attribute doesn't exist, we'll need to grow the overflow array and
  // transition the layout
  Handle<Layout> layout(&scope, layoutAt(instance->layoutId()));
  Object* result = layoutFindAttribute(thread, layout, name);
  if (result->isError()) {
    // Transition the layout
    layout = layoutAddAttribute(thread, layout, name, 0);
    result = layoutFindAttribute(thread, layout, name);
    CHECK(!result->isError(), "couldn't find attribute on new layout");

    // Build the new overflow array
    Handle<ObjectArray> overflow(
        &scope, instance->instanceVariableAt(layout->overflowOffset()));
    Handle<ObjectArray> new_overflow(
        &scope, newObjectArray(overflow->length() + 1));
    overflow->copyTo(*new_overflow);
    instance->instanceVariableAtPut(layout->overflowOffset(), *new_overflow);

    // Update the instance's layout
    instance->setHeader(instance->header()->withLayoutId(layout->id()));
  }

  // Store the attribute
  AttributeInfo info(result);
  if (info.isInObject()) {
    instance->instanceVariableAtPut(info.offset(), *value);
  } else {
    Handle<ObjectArray> overflow(
        &scope, instance->instanceVariableAt(layout->overflowOffset()));
    overflow->atPut(info.offset(), *value);
  }

  return None::object();
}

Object* Runtime::layoutFollowEdge(
    const Handle<List>& edges,
    const Handle<Object>& label) {
  CHECK(
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
  CHECK(
      edges->allocated() % 2 == 0,
      "edges must contain an even number of elements");
  listAdd(edges, label);
  listAdd(edges, layout);
}

void Runtime::layoutInitializeInObjectAttributes(
    Thread* thread,
    const Handle<Layout>& layout,
    const Handle<ObjectArray>& names) {
  HandleScope scope(thread);
  Handle<ObjectArray> attributes(&scope, newObjectArray(names->length()));
  for (word i = 0; i < names->length(); i++) {
    Handle<ObjectArray> info(&scope, newObjectArray(2));
    Handle<Object> name(&scope, names->at(i));
    info->atPut(0, internString(name));
    AttributeInfo data(i * kPointerSize, AttributeInfo::Flag::kInObject);
    info->atPut(1, data.asSmallInteger());
    attributes->atPut(i, *info);
  }
  layout->setInObjectAttributes(*attributes);
}

Object* Runtime::layoutFindAttribute(
    Thread* thread,
    const Handle<Layout>& layout,
    const Handle<Object>& name) {
  HandleScope scope(thread->handles());
  Handle<Object> iname(&scope, internString(name));

  // Check in-object attributes
  Handle<ObjectArray> in_object(&scope, layout->inObjectAttributes());
  for (word i = 0; i < in_object->length(); i++) {
    Handle<ObjectArray> entry(&scope, in_object->at(i));
    if (entry->at(0) == *iname) {
      return entry->at(1);
    }
  }

  // Check overflow attributes
  Handle<ObjectArray> overflow(&scope, layout->overflowAttributes());
  for (word i = 0; i < overflow->length(); i++) {
    Handle<ObjectArray> entry(&scope, overflow->at(i));
    if (entry->at(0) == *iname) {
      return entry->at(1);
    }
  }

  return Error::object();
}

Object* Runtime::layoutCreateChild(
    Thread* thread,
    const Handle<Layout>& layout) {
  HandleScope scope(thread->handles());
  Handle<Layout> new_layout(&scope, newLayout());
  new_layout->setDescribedClass(layout->describedClass());
  new_layout->setInObjectAttributes(layout->inObjectAttributes());
  new_layout->setOverflowAttributes(layout->overflowAttributes());
  return *new_layout;
}

Object* Runtime::layoutAddAttribute(
    Thread* thread,
    const Handle<Layout>& layout,
    const Handle<Object>& name,
    word flags) {
  CHECK(
      !(flags & AttributeInfo::Flag::kInObject),
      "cannot add in-object properties");
  HandleScope scope(thread->handles());
  Handle<Object> iname(&scope, internString(name));

  // Check if a edge for the attribute addition already exists
  Handle<List> edges(&scope, layout->additions());
  Object* result = layoutFollowEdge(edges, iname);
  if (!result->isError()) {
    return result;
  }

  // Create the new overflow array by copying the old
  Handle<ObjectArray> overflow(&scope, layout->overflowAttributes());
  Handle<ObjectArray> new_overflow(
      &scope, newObjectArray(overflow->length() + 1));
  overflow->copyTo(*new_overflow);

  // Add the new attribute to the overflow array
  Handle<ObjectArray> entry(&scope, newObjectArray(2));
  entry->atPut(0, *iname);
  entry->atPut(1, AttributeInfo(overflow->length(), flags).asSmallInteger());
  new_overflow->atPut(overflow->length(), *entry);

  // Create the new layout
  Handle<Layout> new_layout(&scope, layoutCreateChild(thread, layout));
  new_layout->setOverflowAttributes(*new_overflow);

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
  Object* result = layoutFindAttribute(thread, layout, name);
  if (result->isError()) {
    return result;
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
  AttributeInfo info(result);
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

} // namespace python
