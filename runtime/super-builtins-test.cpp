#include "gtest/gtest.h"

#include "runtime.h"
#include "super-builtins.h"
#include "test-utils.h"

namespace python {

using namespace testing;

TEST(SuperBuiltinsTest, DunderCallWorksInTypesWithNonDefaultMetaclass) {
  Runtime runtime;
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class M(type): pass
class A(metaclass=M):
    x = 2
class B(A):
    x = 4
    def getsuper(self):
        return super()
result = B().getsuper().x
)")
                   .isError());
  HandleScope scope;
  Object result(&scope, moduleAt(&runtime, "__main__", "result"));
  EXPECT_TRUE(isIntEqualsWord(*result, 2));
}

TEST(SuperBuiltinsTest, SuperTest1) {
  Runtime runtime;
  std::string output = compileAndRunToString(&runtime, R"(
class A:
    def f(self):
        return 1

class B(A):
    def f(self):
        return super(B, self).f() + 2

class C(A):
    def f(self):
        return super(C, self).f() + 3

class D(C, B):
    def f(self):
        return super(D, self).f() + 4

class E(D):
    pass

class F(E):
    f = E.f

class G(A):
    pass

print(D().f())
print(D.f(D()))
print(E().f())
print(E.f(E()))
print(F().f())
print(F.f(F()))
)");
  EXPECT_EQ(output, "10\n10\n10\n10\n10\n10\n");
}

TEST(SuperBuiltinsTest, SuperTest2) {
  Runtime runtime;
  std::string output = compileAndRunToString(&runtime, R"(
class A:
    @classmethod
    def cm(cls):
        return (cls, 1)

class B(A):
    @classmethod
    def cm(cls):
        return (cls, super(B, cls).cm(), 2)

class C(A):
    @classmethod
    def cm(cls):
        return (cls, super(C, cls).cm(), 3)

class D(C, B):
    def cm(cls):
        return (cls, super(D, cls).cm(), 4)

class E(D):
    pass

class G(A):
    pass

print(A.cm() == (A, 1))
print(A().cm() == (A, 1))
print(G.cm() == (G, 1))
print(G().cm() == (G, 1))
d = D()
print(d.cm() == (d, (D, (D, (D, 1), 2), 3), 4))
e = E()
print(e.cm() == (e, (E, (E, (E, 1), 2), 3), 4))
)");
  EXPECT_EQ(output, "True\nTrue\nTrue\nTrue\nTrue\nTrue\n");
}

TEST(SuperBuiltinsTest, SuperTestNoArgument) {
  Runtime runtime;
  compileAndRunToString(&runtime, R"(
class A:
    @classmethod
    def cm(cls):
        return (cls, 1)

    def f(self):
        return 1

class B(A):
    @classmethod
    def cm(cls):
        return (cls, super().cm(), 2)

    def f(self):
        return super().f() + 2

class C(A):
    @classmethod
    def cm(cls):
        return (cls, super().cm(), 3)

    def f(self):
        return super().f() + 3

class D(C, B):
    def cm(cls):
        return (cls, super().cm(), 4)

    def f(self):
        return super().f() + 4

a = B().f()
b = D().f()
c = B.cm() == (B, (B, 1), 2)
d = D()
e = d.cm() == (d, (D, (D, (D, 1), 2), 3), 4)
)");
  HandleScope scope;
  Object a(&scope, moduleAt(&runtime, "__main__", "a"));
  Object b(&scope, moduleAt(&runtime, "__main__", "b"));
  Bool c(&scope, moduleAt(&runtime, "__main__", "c"));
  Bool e(&scope, moduleAt(&runtime, "__main__", "e"));
  EXPECT_TRUE(isIntEqualsWord(*a, 3));
  EXPECT_TRUE(isIntEqualsWord(*b, 10));
  EXPECT_EQ(*c, Bool::trueObj());
  EXPECT_EQ(*e, Bool::trueObj());
}

TEST(SuperBuiltinsTest,
     SuperCalledFromFunctionWithCellVarReturnsSuperInstance) {
  Runtime runtime;
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class MetaA(type):
    x = 42
class MetaB(MetaA):
    def __new__(metacls, cls, bases, classdict):
        cellvar = None
        def foobar():
            return cellvar
        return super().__new__(metacls, cls, bases, classdict)
class C(metaclass=MetaB): pass
result = type(C()).x
)")
                   .isError());
  HandleScope scope;
  Object result(&scope, moduleAt(&runtime, "__main__", "result"));
  EXPECT_TRUE(isIntEqualsWord(*result, 42));
}

TEST(SuperTest, NoArgumentRaisesRuntimeError) {
  Runtime runtime;
  EXPECT_TRUE(raisedWithStr(runFromCStr(&runtime, "super()"),
                            LayoutId::kRuntimeError, "super(): no arguments"));
  Thread::current()->clearPendingException();

  EXPECT_TRUE(raisedWithStr(runFromCStr(&runtime, R"(
def f(a):
    super()
f(1)
)"),
                            LayoutId::kRuntimeError,
                            "super(): __class__ cell not found"));
}

TEST(SuperBuiltinsTest, SuperGetAttributeReturnsAttributeInSuperClass) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class A:
  x = 13
class B(A):
  x = 42
  def getsuper(self):
    return super()
s = B().getsuper()
)")
                   .isError());
  Object s_obj(&scope, moduleAt(&runtime, "__main__", "s"));
  ASSERT_TRUE(s_obj.isSuper());
  Super s(&scope, *s_obj);
  Object name(&scope, runtime.newStrFromCStr("x"));
  EXPECT_TRUE(isIntEqualsWord(superGetAttribute(thread, s, name), 13));
}

TEST(SuperBuiltinsTest, SuperGetAttributeWithMissingAttributeReturnsError) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class A: pass
class B(A):
  x = 42
  def getsuper(self):
    return super()
s = B().getsuper()
)")
                   .isError());
  Object s_obj(&scope, moduleAt(&runtime, "__main__", "s"));
  ASSERT_TRUE(s_obj.isSuper());
  Super s(&scope, *s_obj);
  Object name(&scope, runtime.newStrFromCStr("x"));
  EXPECT_TRUE(superGetAttribute(thread, s, name).isError());
  EXPECT_FALSE(thread->hasPendingException());
}

TEST(SuperBuiltinsTest, SuperGetAttributeCallsDunderGetOnDataDescriptor) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class D:
  def __set__(self, instance, value): pass
  def __get__(self, instance, owner): return (self, instance, owner)
d = D()
class A:
  x = d
class B(A):
  x = 42
  def getsuper(self):
    return super()
i = B()
s = i.getsuper()
)")
                   .isError());
  Object d(&scope, moduleAt(&runtime, "__main__", "d"));
  Object b(&scope, moduleAt(&runtime, "__main__", "B"));
  Object i(&scope, moduleAt(&runtime, "__main__", "i"));
  Object s_obj(&scope, moduleAt(&runtime, "__main__", "s"));
  ASSERT_TRUE(s_obj.isSuper());
  Super s(&scope, *s_obj);
  Object name(&scope, runtime.newStrFromCStr("x"));
  Object result_obj(&scope, superGetAttribute(thread, s, name));
  ASSERT_TRUE(result_obj.isTuple());
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 3);
  EXPECT_EQ(result.at(0), d);
  EXPECT_EQ(result.at(1), i);
  EXPECT_EQ(result.at(2), b);
}

TEST(SuperBuiltinsTest, SuperGetAttributeCallsDunderGetOnNonDataDescriptor) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class D:
  def __get__(self, instance, owner): return (self, instance, owner)
d = D()
class A:
  x = d
class B(A):
  x = 42
  def getsuper(self):
    return super()
i = B()
s = i.getsuper()
)")
                   .isError());
  Object d(&scope, moduleAt(&runtime, "__main__", "d"));
  Object b(&scope, moduleAt(&runtime, "__main__", "B"));
  Object i(&scope, moduleAt(&runtime, "__main__", "i"));
  Object s_obj(&scope, moduleAt(&runtime, "__main__", "s"));
  ASSERT_TRUE(s_obj.isSuper());
  Super s(&scope, *s_obj);
  Object name(&scope, runtime.newStrFromCStr("x"));
  Object result_obj(&scope, superGetAttribute(thread, s, name));
  ASSERT_TRUE(result_obj.isTuple());
  Tuple result(&scope, *result_obj);
  ASSERT_EQ(result.length(), 3);
  EXPECT_EQ(result.at(0), d);
  EXPECT_EQ(result.at(1), i);
  EXPECT_EQ(result.at(2), b);
}

TEST(SuperBuiltinsTest, SuperGetAttributeDunderClassReturnsSuper) {
  Runtime runtime;
  Thread* thread = Thread::current();
  HandleScope scope(thread);
  ASSERT_FALSE(runFromCStr(&runtime, R"(
class C:
  def foo(self):
    return super()
s = C().foo()
)")
                   .isError());
  Object s_obj(&scope, moduleAt(&runtime, "__main__", "s"));
  ASSERT_TRUE(s_obj.isSuper());
  Super s(&scope, *s_obj);
  Object name(&scope, runtime.newStrFromCStr("__class__"));
  Type super_type(&scope, runtime.typeAt(LayoutId::kSuper));
  EXPECT_EQ(superGetAttribute(thread, s, name), super_type);
}

}  // namespace python
