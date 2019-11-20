#pragma once

#include "frame.h"
#include "globals.h"
#include "objects.h"
#include "runtime.h"
#include "thread.h"

namespace py {

// Prepare `name_obj` to be used as an attribute name: Raise a TypeError if it
// is not a string; reject some string subclasses. Otherwise return an
// interned string that can be used with attribute accessors.
RawObject attributeName(Thread* thread, const Object& name_obj);

RawObject attributeNameNoException(Thread* thread, const Object& name_obj);

// Convert an CPython's extension slot ints into a RawType::Slot
Type::Slot slotToTypeSlot(int slot);

// Inherit slots defined by a C Extension
RawObject addInheritedSlots(const Type& type);

// A version of Dict::Bucket::nextItem for type dict to filter out
// placeholders.
bool nextTypeDictItem(RawTuple data, word* idx);

void typeAddDocstring(Thread* thread, const Type& type);

RawObject typeAt(Thread* thread, const Type& type, const Object& name);

// Optimized version for situation in which the hash is already known so we
// can skip reading it from the string object.
RawObject typeAtWithHash(Thread* thread, const Type& type, const Object& name,
                         word hash);

RawObject typeAtSetLocation(Thread* thread, const Type& type,
                            const Object& name, word hash, Object* location);

RawObject typeAtById(Thread* thread, const Type& type, SymbolId id);

RawObject typeAtPut(Thread* thread, const Type& type, const Object& name,
                    const Object& value);

RawObject typeAtPutById(Thread* thread, const Type& type, SymbolId id,
                        const Object& value);

RawObject typeRemove(Thread* thread, const Type& type, const Object& name);

RawObject typeKeys(Thread* thread, const Type& type);

RawObject typeLen(Thread* thread, const Type& type);

RawObject typeValues(Thread* thread, const Type& type);

RawObject typeGetAttribute(Thread* thread, const Type& type,
                           const Object& name);

RawObject typeGetAttributeSetLocation(Thread* thread, const Type& type,
                                      const Object& name, Object* location_out);

// Returns true if the type defines a __set__ method.
bool typeIsDataDescriptor(Thread* thread, const Type& type);

// Returns true if the type defines a __get__ method.
bool typeIsNonDataDescriptor(Thread* thread, const Type& type);

// If descr's Type has __get__(), call it with the appropriate arguments and
// return the result. Otherwise, return descr.
RawObject resolveDescriptorGet(Thread* thread, const Object& descr,
                               const Object& instance,
                               const Object& instance_type);

RawObject typeInit(Thread* thread, const Type& type, const Str& name,
                   const Dict& dict, const Tuple& mro);

// Looks up `key` in the dict of each entry in type's MRO. Returns
// `Error::notFound()` if the name was not found.
RawObject typeLookupInMro(Thread* thread, const Type& type, const Object& name);

RawObject typeLookupInMroSetLocation(Thread* thread, const Type& type,
                                     const Object& name, Object* location);

// Looks up `id` in the dict of each entry in type's MRO. Returns
// `Error::notFound()` if the name was not found.
RawObject typeLookupInMroById(Thread* thread, const Type& type, SymbolId id);

RawObject typeNew(Thread* thread, LayoutId metaclass_id, const Str& name,
                  const Tuple& bases, const Dict& dict, Type::Flag flags);

RawObject typeSetAttr(Thread* thread, const Type& type, const Object& name,
                      const Object& value);

// Terminate the process if cache invalidation for updating attr_name in type
// objects is unimplemented.
void terminateIfUnimplementedTypeAttrCacheInvalidation(Thread* thread,
                                                       const Object& attr_name);

class TypeBuiltins
    : public Builtins<TypeBuiltins, SymbolId::kType, LayoutId::kType> {
 public:
  static void postInitialize(Runtime* runtime, const Type& new_type);

  static RawObject dunderCall(Thread* thread, Frame* frame, word nargs);
  static RawObject dunderGetattribute(Thread* thread, Frame* frame, word nargs);
  static RawObject dunderSetattr(Thread* thread, Frame* frame, word nargs);
  static RawObject dunderSubclasses(Thread* thread, Frame* frame, word nargs);
  static RawObject mro(Thread* thread, Frame* frame, word nargs);

  static const BuiltinAttribute kAttributes[];
  static const BuiltinMethod kBuiltinMethods[];

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(TypeBuiltins);
};

class TypeProxyBuiltins
    : public Builtins<TypeProxyBuiltins, SymbolId::kTypeProxy,
                      LayoutId::kTypeProxy> {
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(TypeProxyBuiltins);
};

}  // namespace py
