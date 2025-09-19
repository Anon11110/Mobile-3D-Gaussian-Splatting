// rhi/include/common/ref_count.h
#pragma once

#include <atomic>       // For std::atomic in RefCounter
#include <cassert>      // For assert in Attach method
#include <cstddef>      // For std::nullptr_t
#include <type_traits>  // For std::enable_if, std::is_convertible

namespace rhi {

//////////////////////////////////////////////////////////////////////////
// IRefCounted
// Base interface for reference-counted objects following the COM model.
// All RHI resources will inherit from this interface.
//////////////////////////////////////////////////////////////////////////
class IRefCounted {
protected:
    IRefCounted() = default;
    virtual ~IRefCounted() = default;

public:
    // Reference counting interface
    // IMPORTANT: All implementations MUST be thread-safe using atomic operations
    // Multiple threads may call AddRef/Release simultaneously on the same object
    virtual unsigned long AddRef() = 0;    // Must use atomic increment
    virtual unsigned long Release() = 0;   // Must use atomic decrement
    virtual unsigned long GetRefCount() = 0; // Must return atomic load

    // Non-copyable and non-movable
    IRefCounted(const IRefCounted&) = delete;
    IRefCounted(IRefCounted&&) = delete;
    IRefCounted& operator=(const IRefCounted&) = delete;
    IRefCounted& operator=(IRefCounted&&) = delete;
};

//////////////////////////////////////////////////////////////////////////
// RefCounter<T>
// A CRTP (Curiously Recurring Template Pattern) mixin that implements
// reference counting for any IRefCounted-derived interface.
// Usage: class VulkanBuffer : public RefCounter<IRHIBuffer> { ... }
//////////////////////////////////////////////////////////////////////////
template<class T>
class RefCounter : public T
{
private:
    std::atomic<unsigned long> m_refCount = 1;  // Start with 1 (creation reference)

public:
    virtual unsigned long AddRef() override {
        return ++m_refCount;  // memory_order_relaxed is sufficient for increment
    }

    virtual unsigned long Release() override {
        unsigned long result = --m_refCount;  // memory_order_acq_rel for decrement
        if (result == 0) {
            delete this;  // Virtual destructor from IRefCounted ensures proper cleanup
        }
        return result;
    }

    virtual unsigned long GetRefCount() override {
        return m_refCount.load();  // memory_order_relaxed for simple read
    }
};

//////////////////////////////////////////////////////////////////////////
// RefCntPtr<T>
// Smart pointer that manages reference counting automatically.
// Designed to work with the intrusive reference counting pattern where
// objects start with refCount = 1.
//////////////////////////////////////////////////////////////////////////
template <typename T>
class RefCntPtr {
public:
    typedef T InterfaceType;

protected:
    InterfaceType* ptr_;
    template<class U> friend class RefCntPtr;

    void InternalAddRef() const noexcept {
        if (ptr_ != nullptr) {
            ptr_->AddRef();
        }
    }

    unsigned long InternalRelease() noexcept {
        unsigned long ref = 0;
        T* temp = ptr_;

        if (temp != nullptr) {
            ptr_ = nullptr;
            ref = temp->Release();
        }

        return ref;
    }

public:
    // Constructors
    RefCntPtr() noexcept : ptr_(nullptr) {}
    RefCntPtr(std::nullptr_t) noexcept : ptr_(nullptr) {}

    // Constructor for existing references - DOES call AddRef
    template<class U>
    RefCntPtr(U* other) noexcept : ptr_(other) {
        InternalAddRef();  // Increments reference count
    }

    RefCntPtr(const RefCntPtr& other) noexcept : ptr_(other.ptr_) {
        InternalAddRef();
    }

    // Copy ctor for convertible types
    template<class U>
    RefCntPtr(const RefCntPtr<U>& other,
              typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
        : ptr_(other.ptr_) {
        InternalAddRef();
    }

    // Move constructors
    RefCntPtr(RefCntPtr&& other) noexcept : ptr_(nullptr) {
        Swap(other);
    }

    template<class U>
    RefCntPtr(RefCntPtr<U>&& other,
              typename std::enable_if<std::is_convertible<U*, T*>::value, void*>::type* = nullptr) noexcept
        : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ~RefCntPtr() noexcept {
        InternalRelease();
    }

    // Assignment operators
    RefCntPtr& operator=(std::nullptr_t) noexcept {
        InternalRelease();
        return *this;
    }

    RefCntPtr& operator=(T* other) noexcept {
        if (ptr_ != other) {
            RefCntPtr(other).Swap(*this);
        }
        return *this;
    }

    template <typename U>
    RefCntPtr& operator=(U* other) noexcept {
        RefCntPtr(other).Swap(*this);
        return *this;
    }

    RefCntPtr& operator=(const RefCntPtr& other) noexcept {
        if (ptr_ != other.ptr_) {
            RefCntPtr(other).Swap(*this);
        }
        return *this;
    }

    template<class U>
    RefCntPtr& operator=(const RefCntPtr<U>& other) noexcept {
        RefCntPtr(other).Swap(*this);
        return *this;
    }

    RefCntPtr& operator=(RefCntPtr&& other) noexcept {
        RefCntPtr(static_cast<RefCntPtr&&>(other)).Swap(*this);
        return *this;
    }

    template<class U>
    RefCntPtr& operator=(RefCntPtr<U>&& other) noexcept {
        RefCntPtr(static_cast<RefCntPtr<U>&&>(other)).Swap(*this);
        return *this;
    }

    // Utility methods
    void Swap(RefCntPtr&& r) noexcept {
        T* tmp = ptr_;
        ptr_ = r.ptr_;
        r.ptr_ = tmp;
    }

    void Swap(RefCntPtr& r) noexcept {
        T* tmp = ptr_;
        ptr_ = r.ptr_;
        r.ptr_ = tmp;
    }

    [[nodiscard]] T* Get() const noexcept {
        return ptr_;
    }

    operator T*() const {
        return ptr_;
    }

    InterfaceType* operator->() const noexcept {
        return ptr_;
    }

    // COM-style out parameter support
    // WARNING: This bypasses reference counting - use with caution!
    // Primarily for COM-style APIs: void CreateBuffer(IRHIBuffer** ppBuffer)
    T** operator&() {
        return &ptr_;
    }

    [[nodiscard]] T* const* GetAddressOf() const noexcept {
        return &ptr_;
    }

    [[nodiscard]] T** GetAddressOf() noexcept {
        return &ptr_;
    }

    [[nodiscard]] T** ReleaseAndGetAddressOf() noexcept {
        InternalRelease();
        return &ptr_;
    }

    T* Detach() noexcept {
        T* ptr = ptr_;
        ptr_ = nullptr;
        return ptr;
    }

    // Set the pointer while keeping the object's reference count unchanged
    void Attach(InterfaceType* other) {
        if (ptr_ != nullptr) {
            auto ref = ptr_->Release();
            (void)ref;
            assert(ref != 0 || ptr_ != other);
        }
        ptr_ = other;
    }

    // Create a wrapper around a raw object while keeping the object's reference count unchanged
    static RefCntPtr<T> Create(T* other) {
        RefCntPtr<T> Ptr;
        Ptr.Attach(other);
        return Ptr;
    }

    unsigned long Reset() {
        return InternalRelease();
    }
};

} // namespace rhi