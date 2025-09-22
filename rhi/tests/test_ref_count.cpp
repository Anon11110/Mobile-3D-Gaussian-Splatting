#include "test_framework.h"
#include "../include/common/ref_count.h"
#include <thread>
#include <vector>
#include <atomic>

namespace rhi {
namespace test {

// Test classes for reference counting
class TestObject : public RefCounter<IRefCounted> {
public:
    static std::atomic<int> s_instanceCount;
    int value;

    explicit TestObject(int val = 0) : value(val) {
        s_instanceCount++;
    }

    ~TestObject() {
        s_instanceCount--;
    }
};

std::atomic<int> TestObject::s_instanceCount{0};

// Derived class for testing conversions
class DerivedTestObject : public TestObject {
public:
    int derivedValue;

    explicit DerivedTestObject(int val = 0, int dval = 0)
        : TestObject(val), derivedValue(dval) {}
};

/**
 * Test: Default-constructed RefCntPtr is null
 * AC: ptr.Get() returns nullptr
 */
RHI_TEST(RefCntPtr_DefaultConstruction) {
    RefCntPtr<TestObject> ptr;
    RHI_ASSERT_NULL(ptr.Get());
    return true;
}

/**
 * Test: RefCntPtr constructed with nullptr
 * AC: ptr.Get() returns nullptr
 */
RHI_TEST(RefCntPtr_NullptrConstruction) {
    RefCntPtr<TestObject> ptr(nullptr);
    RHI_ASSERT_NULL(ptr.Get());
    return true;
}

/**
 * Test: Constructor with raw pointer calls AddRef
 * AC: RefCount increases to 2, object destroyed when ptr out of scope
 */
RHI_TEST(RefCntPtr_RawPointerConstruction) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(42);
        RHI_ASSERT_EQ(1, TestObject::s_instanceCount);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());

        RefCntPtr<TestObject> ptr(raw);
        RHI_ASSERT_EQ(2u, raw->GetRefCount());  // Constructor calls AddRef
        RHI_ASSERT_EQ(42, ptr->value);

        // Need to release the extra reference from raw pointer
        raw->Release();
        RHI_ASSERT_EQ(1u, raw->GetRefCount());
    }
    // Object should be destroyed when RefCntPtr goes out of scope
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Create() attaches without AddRef (takes ownership)
 * AC: RefCount stays at 1, object properly destroyed
 */
RHI_TEST(RefCntPtr_CreateMethod) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(100);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());

        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());  // Create doesn't AddRef
        RHI_ASSERT_EQ(100, ptr->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Copy constructor increases refcount
 * AC: RefCount == 2, both pointers valid and equal
 */
RHI_TEST(RefCntPtr_CopyConstruction) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(50);
        RefCntPtr<TestObject> ptr1 = RefCntPtr<TestObject>::Create(raw);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());

        RefCntPtr<TestObject> ptr2(ptr1);  // Copy constructor
        RHI_ASSERT_EQ(2u, raw->GetRefCount());
        RHI_ASSERT_EQ(ptr1.Get(), ptr2.Get());
        RHI_ASSERT_EQ(50, ptr2->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Move constructor transfers ownership
 * AC: Source ptr becomes null, refcount unchanged, target owns object
 */
RHI_TEST(RefCntPtr_MoveConstruction) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(75);
        RefCntPtr<TestObject> ptr1 = RefCntPtr<TestObject>::Create(raw);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());

        RefCntPtr<TestObject> ptr2(std::move(ptr1));  // Move constructor
        RHI_ASSERT_EQ(1u, raw->GetRefCount());  // No ref count change
        RHI_ASSERT_NULL(ptr1.Get());  // ptr1 should be null
        RHI_ASSERT_EQ(raw, ptr2.Get());
        RHI_ASSERT_EQ(75, ptr2->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Copy assignment manages refcounts correctly
 * AC: Old object released, new object refcount increased
 */
RHI_TEST(RefCntPtr_CopyAssignment) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw1 = new TestObject(10);
        TestObject* raw2 = new TestObject(20);

        RefCntPtr<TestObject> ptr1 = RefCntPtr<TestObject>::Create(raw1);
        RefCntPtr<TestObject> ptr2 = RefCntPtr<TestObject>::Create(raw2);

        RHI_ASSERT_EQ(1u, raw1->GetRefCount());
        RHI_ASSERT_EQ(1u, raw2->GetRefCount());

        ptr2 = ptr1;  // Copy assignment

        RHI_ASSERT_EQ(2u, raw1->GetRefCount());
        RHI_ASSERT_EQ(ptr1.Get(), ptr2.Get());
        RHI_ASSERT_EQ(10, ptr2->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Move assignment transfers ownership
 * AC: Source ptr null, old object released, target owns object
 */
RHI_TEST(RefCntPtr_MoveAssignment) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw1 = new TestObject(30);
        TestObject* raw2 = new TestObject(40);

        RefCntPtr<TestObject> ptr1 = RefCntPtr<TestObject>::Create(raw1);
        RefCntPtr<TestObject> ptr2 = RefCntPtr<TestObject>::Create(raw2);

        ptr2 = std::move(ptr1);  // Move assignment

        RHI_ASSERT_NULL(ptr1.Get());
        RHI_ASSERT_EQ(raw1, ptr2.Get());
        RHI_ASSERT_EQ(1u, raw1->GetRefCount());
        RHI_ASSERT_EQ(30, ptr2->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Self-assignment is safe
 * AC: No crash, refcount unchanged, object still valid
 */
RHI_TEST(RefCntPtr_SelfAssignment) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(99);
        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);

        unsigned long refCountBefore = raw->GetRefCount();
        ptr = ptr;  // Self-assignment
        RHI_ASSERT_EQ(refCountBefore, raw->GetRefCount());
        RHI_ASSERT_EQ(99, ptr->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Reset() releases reference and nullifies pointer
 * AC: ptr becomes null, object destroyed when refcount hits 0
 */
RHI_TEST(RefCntPtr_Reset) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(55);
        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());
        RHI_ASSERT_EQ(1, TestObject::s_instanceCount);

        ptr.Reset();
        RHI_ASSERT_NULL(ptr.Get());
        RHI_ASSERT_EQ(0, TestObject::s_instanceCount);  // Object should be destroyed
    }
    return true;
}

/**
 * Test: Detach() returns raw pointer without Release
 * AC: ptr becomes null, raw pointer valid with unchanged refcount
 */
RHI_TEST(RefCntPtr_Detach) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(66);
        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());

        TestObject* detached = ptr.Detach();
        RHI_ASSERT_NULL(ptr.Get());
        RHI_ASSERT_EQ(raw, detached);
        RHI_ASSERT_EQ(1u, detached->GetRefCount());
        RHI_ASSERT_EQ(1, TestObject::s_instanceCount);  // Object still alive

        // Clean up manually
        detached->Release();
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Attach() takes ownership without AddRef
 * AC: Old object released, new object attached with unchanged refcount
 */
RHI_TEST(RefCntPtr_Attach) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw1 = new TestObject(77);
        TestObject* raw2 = new TestObject(88);

        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw1);
        RHI_ASSERT_EQ(1u, raw1->GetRefCount());
        RHI_ASSERT_EQ(1u, raw2->GetRefCount());

        ptr.Attach(raw2);  // Attach without AddRef
        RHI_ASSERT_EQ(raw2, ptr.Get());
        RHI_ASSERT_EQ(1u, raw2->GetRefCount());  // No AddRef
        RHI_ASSERT_EQ(88, ptr->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Derived-to-base pointer conversions
 * AC: Base ptr valid, refcount managed correctly through conversions
 */
RHI_TEST(RefCntPtr_DerivedToBase) {
    TestObject::s_instanceCount = 0;
    {
        DerivedTestObject* derived = new DerivedTestObject(100, 200);
        RefCntPtr<DerivedTestObject> derivedPtr = RefCntPtr<DerivedTestObject>::Create(derived);

        // Convert to base
        RefCntPtr<TestObject> basePtr(derivedPtr);
        RHI_ASSERT_EQ(2u, derived->GetRefCount());
        RHI_ASSERT_EQ(100, basePtr->value);

        // Move conversion
        RefCntPtr<TestObject> basePtr2(std::move(derivedPtr));
        RHI_ASSERT_NULL(derivedPtr.Get());
        RHI_ASSERT_EQ(2u, derived->GetRefCount());
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Arrow and implicit conversion operators
 * AC: operator-> accesses members, implicit T* conversion works
 */
RHI_TEST(RefCntPtr_Operators) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(123);
        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);

        // Test operator->
        RHI_ASSERT_EQ(123, ptr->value);
        ptr->value = 456;
        RHI_ASSERT_EQ(456, raw->value);

        // Test implicit conversion to T*
        TestObject* rawPtr = ptr;
        RHI_ASSERT_EQ(raw, rawPtr);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Assignment from nullptr releases object
 * AC: ptr becomes null, object destroyed
 */
RHI_TEST(RefCntPtr_NullptrAssignment) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(789);
        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);
        RHI_ASSERT_EQ(1, TestObject::s_instanceCount);

        ptr = nullptr;
        RHI_ASSERT_NULL(ptr.Get());
        RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    }
    return true;
}

/**
 * Test: Concurrent AddRef/Release operations are thread-safe
 * AC: No race conditions with 10 threads × 1000 operations, final refcount == 1
 */
RHI_TEST(RefCntPtr_ThreadSafety) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(999);
        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);

        const int numThreads = 10;
        const int numIterations = 1000;
        std::vector<std::thread> threads;

        // Create threads that will AddRef and Release
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&ptr, numIterations]() {
                for (int j = 0; j < numIterations; ++j) {
                    RefCntPtr<TestObject> localPtr(ptr);  // AddRef
                    // localPtr goes out of scope, Release
                }
            });
        }

        // Wait for all threads
        for (auto& t : threads) {
            t.join();
        }

        // Should still have exactly 1 reference
        RHI_ASSERT_EQ(1u, raw->GetRefCount());
        RHI_ASSERT_EQ(1, TestObject::s_instanceCount);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Swap() exchanges pointers between RefCntPtrs
 * AC: Pointers exchanged, refcounts unchanged
 */
RHI_TEST(RefCntPtr_Swap) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw1 = new TestObject(111);
        TestObject* raw2 = new TestObject(222);

        RefCntPtr<TestObject> ptr1 = RefCntPtr<TestObject>::Create(raw1);
        RefCntPtr<TestObject> ptr2 = RefCntPtr<TestObject>::Create(raw2);

        ptr1.Swap(ptr2);

        RHI_ASSERT_EQ(raw2, ptr1.Get());
        RHI_ASSERT_EQ(raw1, ptr2.Get());
        RHI_ASSERT_EQ(222, ptr1->value);
        RHI_ASSERT_EQ(111, ptr2->value);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: GetAddressOf() and ReleaseAndGetAddressOf() methods
 * AC: Returns correct pointer addresses, ReleaseAndGetAddressOf nullifies ptr
 */
RHI_TEST(RefCntPtr_GetAddressOf) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(333);
        RefCntPtr<TestObject> ptr = RefCntPtr<TestObject>::Create(raw);

        // Test GetAddressOf
        TestObject** addr = ptr.GetAddressOf();
        RHI_ASSERT_NOT_NULL(addr);
        RHI_ASSERT_EQ(raw, *addr);

        // Test const GetAddressOf
        const RefCntPtr<TestObject>& constPtr = ptr;
        TestObject* const* constAddr = constPtr.GetAddressOf();
        RHI_ASSERT_NOT_NULL(constAddr);
        RHI_ASSERT_EQ(raw, *constAddr);

        // Test ReleaseAndGetAddressOf
        TestObject** relAddr = ptr.ReleaseAndGetAddressOf();
        RHI_ASSERT_NOT_NULL(relAddr);
        RHI_ASSERT_NULL(*relAddr);
    }
    RHI_ASSERT_EQ(0, TestObject::s_instanceCount);
    return true;
}

/**
 * Test: Complex multi-handle scenario with nested scopes
 * AC: Refcount correctly managed through multiple operations, all objects destroyed
 */
RHI_TEST(RefCntPtr_ComplexScenario) {
    TestObject::s_instanceCount = 0;
    {
        TestObject* raw = new TestObject(500);
        RHI_ASSERT_EQ(1u, raw->GetRefCount());

        {
            RefCntPtr<TestObject> ptr1 = RefCntPtr<TestObject>::Create(raw);
            RHI_ASSERT_EQ(1u, raw->GetRefCount());

            {
                RefCntPtr<TestObject> ptr2(ptr1);
                RHI_ASSERT_EQ(2u, raw->GetRefCount());

                RefCntPtr<TestObject> ptr3;
                ptr3 = ptr2;
                RHI_ASSERT_EQ(3u, raw->GetRefCount());

                ptr2 = nullptr;
                RHI_ASSERT_EQ(2u, raw->GetRefCount());
            }
            RHI_ASSERT_EQ(1u, raw->GetRefCount());
        }
        RHI_ASSERT_EQ(0, TestObject::s_instanceCount);  // Object destroyed
    }
    return true;
}

} // namespace test
} // namespace rhi