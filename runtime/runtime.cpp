#include "runtime.h"

#include "globals.h"
#include "handles.h"
#include "heap.h"
#include "visitor.h"

namespace python {

Runtime::Runtime() : heap_(64 * MiB) {
  initializeClasses();
  initializeInstances();
  initializeModules();
}

Runtime::~Runtime() {}

Object* Runtime::createByteArray(intptr_t length) {
  if (length == 0) {
    return empty_byte_array_;
  }
  return heap()->createByteArray(Runtime::byte_array_class_, length);
}

Object* Runtime::createCode(
    int argcount,
    int kwonlyargcount,
    int nlocals,
    int stacksize,
    int flags,
    Object* code,
    Object* consts,
    Object* names,
    Object* varnames,
    Object* freevars,
    Object* cellvars,
    Object* filename,
    Object* name,
    int firstlineno,
    Object* lnotab) {
  return heap()->createCode(
      code_class_,
      argcount,
      kwonlyargcount,
      nlocals,
      stacksize,
      flags,
      code,
      consts,
      names,
      varnames,
      freevars,
      cellvars,
      filename,
      name,
      firstlineno,
      lnotab);
}

Object* Runtime::createList() {
  return heap()->createList(list_class_, empty_object_array_);
}

Object* Runtime::createObjectArray(intptr_t length) {
  if (length == 0) {
    return empty_object_array_;
  }
  return heap()->createObjectArray(object_array_class_, length, None::object());
}

Object* Runtime::createString(intptr_t length) {
  return heap()->createString(string_class_, length);
}

void Runtime::initializeClasses() {
  class_class_ = heap()->createClassClass();

  byte_array_class_ = heap()->createClass(
      class_class_,
      Layout::BYTE_ARRAY,
      ByteArray::kElementSize,
      true /* isArray */,
      false /* isRoot */);

  code_class_ = heap()->createClass(
      class_class_,
      Layout::CODE,
      Code::kSize,
      false /* isArray */,
      true /* isRoot */);

  dictionary_class_ = heap()->createClass(
      class_class_,
      Layout::DICTIONARY,
      Dictionary::kSize,
      false /* isArray */,
      true /* isRoot */);

  function_class_ = heap()->createClass(
      class_class_,
      Layout::FUNCTION,
      Function::kSize,
      false /* isArray */,
      true /* isRoot */);

  list_class_ = heap()->createClass(
      class_class_,
      Layout::LIST,
      List::kSize,
      false /* isArray */,
      true /* isRoot */);

  module_class_ = heap()->createClass(
      class_class_,
      Layout::MODULE,
      Module::kSize,
      false /* isArray */,
      true /* isRoot */);

  object_array_class_ = heap()->createClass(
      class_class_,
      Layout::OBJECT_ARRAY,
      ObjectArray::kElementSize,
      true /* isArray */,
      true /* isRoot */);

  string_class_ = heap()->createClass(
      class_class_,
      Layout::STRING,
      String::kElementSize,
      true /* isArray */,
      false /* isRoot */);
}

class ScavengeVisitor : public PointerVisitor {
 public:
  explicit ScavengeVisitor(Heap* heap) : heap_(heap) {}

  void visitPointer(Object** pointer) override {
    heap_->scavengePointer(pointer);
  }

 private:
  Heap* heap_;
};

void Runtime::collectGarbage() {
  heap()->flip();
  ScavengeVisitor visitor(heap());
  visitRoots(&visitor);
  heap()->scavenge();
}

void Runtime::initializeInstances() {
  empty_byte_array_ = heap()->createByteArray(byte_array_class_, 0);
  empty_object_array_ =
      heap()->createObjectArray(object_array_class_, 0, None::object());
}

void Runtime::initializeModules() {
  modules_ = createList();
}

void Runtime::visitRoots(PointerVisitor* visitor) {
  // Visit classes
  visitor->visitPointer(&byte_array_class_);
  visitor->visitPointer(&class_class_);
  visitor->visitPointer(&code_class_);
  visitor->visitPointer(&dictionary_class_);
  visitor->visitPointer(&function_class_);
  visitor->visitPointer(&list_class_);
  visitor->visitPointer(&module_class_);
  visitor->visitPointer(&object_array_class_);
  visitor->visitPointer(&string_class_);

  // Visit instances
  visitor->visitPointer(&empty_byte_array_);
  visitor->visitPointer(&empty_object_array_);

  // Visit modules
  visitor->visitPointer(&modules_);
}

} // namespace python
