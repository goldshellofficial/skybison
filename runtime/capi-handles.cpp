#include "capi-handles.h"

#include "runtime.h"

namespace python {

ApiHandle* ApiHandle::create(Object* reference, long refcnt) {
  Thread* thread = Thread::currentThread();
  Runtime* runtime = thread->runtime();

  ApiHandle* handle = static_cast<ApiHandle*>(std::malloc(sizeof(ApiHandle)));
  handle->reference_ = reference;
  handle->ob_refcnt = refcnt;
  Object* reftype = runtime->typeOf(reference);
  if (reference == reftype) {
    handle->ob_type = reinterpret_cast<PyTypeObject*>(handle);
  } else {
    handle->ob_type =
        reinterpret_cast<PyTypeObject*>(ApiHandle::fromObject(reftype));
  }
  return handle;
}

ApiHandle* ApiHandle::fromObject(Object* obj) {
  Thread* thread = Thread::currentThread();
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  Handle<Object> key(&scope, obj);
  Handle<Dict> dict(&scope, runtime->apiHandles());
  Object* value = runtime->dictAt(dict, key);

  // Fast path: All initialized builtin objects
  if (!value->isError()) {
    return castFromObject(value, false);
  }

  // Get the PyObject pointer from extension instances
  Object* extension_ptr = getExtensionPtrAttr(thread, key);
  if (!extension_ptr->isError()) {
    return castFromObject(extension_ptr, false);
  }

  // Initialize an ApiHandle for a builtin object or runtime instance
  ApiHandle* handle = ApiHandle::create(obj, 1);
  Handle<Object> object(&scope,
                        runtime->newIntFromCPtr(static_cast<void*>(handle)));
  runtime->dictAtPut(dict, key, object);
  return handle;
}

ApiHandle* ApiHandle::fromBorrowedObject(Object* obj) {
  Thread* thread = Thread::currentThread();
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  Handle<Object> key(&scope, obj);
  Handle<Dict> dict(&scope, runtime->apiHandles());
  Object* value = runtime->dictAt(dict, key);

  // Fast path: All initialized builtin objects
  if (!value->isError()) {
    return castFromObject(value, true);
  }

  // Get the PyObject pointer from extension instances
  Object* extension_ptr = getExtensionPtrAttr(thread, key);
  if (!extension_ptr->isError()) {
    return castFromObject(extension_ptr, true);
  }

  // Initialize a Borrowed ApiHandle for a builtin object or runtime instance
  ApiHandle* handle = ApiHandle::create(obj, kBorrowedBit);
  Handle<Object> object(&scope,
                        runtime->newIntFromCPtr(static_cast<void*>(handle)));
  runtime->dictAtPut(dict, key, object);
  return handle;
}

ApiHandle* ApiHandle::castFromObject(Object* value, bool borrowed) {
  ApiHandle* handle = static_cast<ApiHandle*>(Int::cast(value)->asCPtr());
  if (borrowed) handle->setBorrowed();
  return handle;
}

Object* ApiHandle::getExtensionPtrAttr(Thread* thread,
                                       const Handle<Object>& obj) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  if (!obj->isInstance()) return Error::object();

  Handle<HeapObject> instance(&scope, *obj);
  Handle<Object> attr_name(&scope, runtime->symbols()->ExtensionPtr());
  return runtime->instanceAt(thread, instance, attr_name);
}

Object* ApiHandle::asInstance(Object* obj) {
  Thread* thread = Thread::currentThread();
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  DCHECK(obj->isType(), "not a Type object");
  Handle<Type> klass(&scope, obj);
  Handle<Layout> layout(&scope, klass->instanceLayout());
  Handle<HeapObject> instance(&scope, runtime->newInstance(layout));
  Handle<Object> object_ptr(&scope,
                            runtime->newIntFromCPtr(static_cast<void*>(this)));
  Handle<Object> attr_name(&scope, runtime->symbols()->ExtensionPtr());
  runtime->instanceAtPut(thread, instance, attr_name, object_ptr);

  return *instance;
}

Object* ApiHandle::asObject() {
  // Fast path: All builtin objects except Types
  // TODO(T32474474): Handle the special case of Int values
  if (reference_ != nullptr) {
    return static_cast<Object*>(reference_);
  }

  DCHECK(type(), "ApiHandles must contain a type pointer");
  // TODO(eelizondo): Add a way to check for builtin objects

  // Create a runtime instance to hold the PyObject pointer
  return asInstance(type()->asObject());
}

bool ApiHandle::isType() {
  // This works under the assumption only PyType_Type's metaType is itself
  return this->type() == ApiHandle::fromPyObject(type())->type();
}

ApiHandle* ApiHandle::type() {
  return ApiHandle::fromPyObject(reinterpret_cast<PyObject*>(ob_type));
}

bool ApiHandle::isSubClass(Thread* thread, LayoutId layout_id) {
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);
  Handle<Type> superclass(&scope, runtime->typeAt(layout_id));
  Handle<Type> subclass(&scope, type()->asObject());
  return runtime->isSubClass(subclass, superclass) == Bool::trueObj();
}

void ApiHandle::dispose() {
  Thread* thread = Thread::currentThread();
  Runtime* runtime = thread->runtime();
  HandleScope scope(thread);

  Handle<Object> key(&scope, asObject());
  Handle<Dict> dict(&scope, runtime->apiHandles());
  runtime->dictRemove(dict, key);
  std::free(this);
}

}  // namespace python
