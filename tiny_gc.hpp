#pragma once
#include <stdio.h>
#include <memory>
#include <algorithm>
#include <vector>
#include <map>
#include <assert.h>
#include <mutex>

#define GC_LOG(...) printf(__VA_ARGS__);
#define GC_ASSERT(condition) assert(condition)

#if defined(ENABLE_HOOK_NEW_DELETE) && defined(_DEBUG)
    void* operator new(std::size_t sz) {
        void* p = std::malloc(sz);
        std::printf("global op new called, size = %zu , address %p\n", sz, p);
        return p;
    }
    void operator delete(void* ptr) noexcept
    {
        std::printf("global op delete called, address %p\n", ptr);
        std::free(ptr);
    }
#endif

namespace private_space
{
    template<int N, int Index = 0>
    constexpr int hightest_bit_index()
    {
        if constexpr (0 < N)
        {
            return hightest_bit_index < N / 2, Index + 1 >();
        }
        return Index;
    }

    int hightest_bit_index(int n, int index = 0)
    {
        if (n)
        {
            return hightest_bit_index(n / 2, index + 1);
        }
        return index;
    }
}

namespace tiny_gc
{
    using std::vector;
    using std::mutex;
    using std::enable_if_t;
    using std::unique_ptr;
    using std::shared_ptr;
    using std::is_array_v;
    using std::extent_v;
    using gc_lock = std::lock_guard<std::mutex>;
    using array_ptr_header = uintptr_t;

    template <class T>
    struct garbage_collector;

    // GC 智能指针数据结构
    template<class T> using gc_unique_ptr = unique_ptr<T, tiny_gc::garbage_collector< T>>;
    template<class T> struct gc_shared_ptr : public shared_ptr<T>
    {
        template <class TFrom>
        gc_shared_ptr(TFrom* ptr) : shared_ptr<T>(ptr, tiny_gc::garbage_collector<T>()) {}
    };
    // 32 个block, 每个 block 的 index 为 [0, 32), 代表内存块大小为 : 1 << index
    constexpr int max_block_count = 8 * sizeof(int);
    // GC 内存管理
    class gc_manager final
    {
        struct thread_save_vector
        {
            vector<void*> container;
            mutex container_lock;

            void push_back(void* obj) 
            {
                gc_lock lock(container_lock);
                container.push_back(obj);
            }
            void* take_last_out()
            {
                gc_lock lock(container_lock);
                void* p = nullptr;
                if (container.size())
                {
                    p = *container.rbegin();
                    container.erase(container.end() - 1);
                }
                return p;
            }
        };

        thread_save_vector memory_block_available[max_block_count];
    public:
        ~gc_manager()
        {
            collect();
        }
        // 尝试返回单个元素 T 的内存块指针, 如果 GC 没有符合条件的空闲内存, 会返回 nullptr;
        template<class T> _NODISCARD void* take_mem_out()
        {
            constexpr int index = private_space::hightest_bit_index<sizeof(T)>() - 1;
            constexpr int block_index = sizeof(T) == 1 << index ? index : index + 1;
            static_assert(block_index < max_block_count, "out of range");

            void* ptr = memory_block_available[block_index].take_last_out();
            GC_LOG("[ block-%2d ] take memory out from GC %p\n", block_index, ptr);
            return ptr;
        }
        // 尝试返回多个元素 T 的内存块指针, 如果 GC 没有符合条件的空闲内存, 会返回 nullptr;
        template<class T> _NODISCARD void* take_mem_out(const size_t elementCount)
        {
            int requireBytes = elementCount * sizeof(T) + sizeof(array_ptr_header);
            int index = private_space::hightest_bit_index(requireBytes) - 1;
            int block_index = requireBytes == 1 << index ? index : index + 1;

            void* ptr = memory_block_available[block_index].take_last_out();
            GC_LOG("[ block-%2d ] take memory out from GC %p\n", block_index, ptr);
            return ptr;
        }
        // 将内存交给 GC 管理
        template<class T> void push_unused_mem(void* ptr)
        {
            constexpr int block_index = private_space::hightest_bit_index<sizeof(T)>() - 1;
            static_assert(block_index < max_block_count, "out of range");

            memory_block_available[block_index].push_back(ptr);
            GC_LOG("[ block-%2d ] push unused memory to GC %p, mem bytes : %zd\n", block_index, ptr, sizeof(T));
        }
        // 将数组内存交给 GC 管理
        template<class T> void push_unused_mem(void* ptr, int elementCount)
        {
            const int totalByte = elementCount * sizeof(T) + sizeof(array_ptr_header);
            int block_index = private_space::hightest_bit_index(totalByte) - 1;
            GC_ASSERT(block_index < max_block_count);

            memory_block_available[block_index].push_back(ptr);
            GC_LOG("[ block-%2d ] push unused memory to GC %p, mem bytes : %d, array count : %d\n", block_index, ptr, totalByte, elementCount);
        }
        // 回收所有空闲内存
        void collect()
        {
            for (int i = 0; i < max_block_count; i++)
            {
                gc_lock lock(memory_block_available[i].container_lock);

                // 后进先出
                std::for_each(
                    memory_block_available[i].container.rbegin(),
                    memory_block_available[i].container.rend(), 
                    [i](void* p) {
                        free(p);
                        GC_LOG("[ block-%2d ] collect %p\n", i, p);
                    }
                );
                memory_block_available[i].container.clear();
            }
        }
    }GC;
    // 智能指针垃圾回收的扩展
    template <class T> struct garbage_collector 
    {
        constexpr garbage_collector() noexcept = default;
        template <class T1, enable_if_t<std::is_convertible_v<T1*, T*>, int> = 0>
        garbage_collector(const garbage_collector<T1>&) noexcept {}

        void operator()(T* ptr) const noexcept 
        { 
            static_assert(0 < sizeof(T), "can't delete an incomplete type");

            ptr->~T();
            GC.push_unused_mem<T>(ptr);
        }
    };
    // 数组类型智能指针垃圾回收的扩展
    template <class T> struct garbage_collector<T[]> 
    {
        constexpr garbage_collector() noexcept = default;
        template <class TFrom, enable_if_t<std::is_convertible_v<TFrom(*)[], T(*)[]>, int> = 0>
        garbage_collector(const garbage_collector<TFrom[]>&) noexcept {}

        template <class TFrom, enable_if_t<std::is_convertible_v<TFrom(*)[], T(*)[]>, int> = 0>
        void operator()(TFrom* ptr) const noexcept
        {
            static_assert(0 < sizeof(T), "can't delete an incomplete type");

            array_ptr_header* pObjCount = reinterpret_cast<array_ptr_header*>(ptr) - 1;
            for (int i = 0; i < *pObjCount; i++) ptr[i].~TFrom();
            GC.push_unused_mem<TFrom>(pObjCount, *pObjCount);
        }
    };
    // 优先尝试从 GC 空闲内存中构建智能指针, 若相应内存块没有空闲内存, 则从堆上分配内存后构建.
#define __DECLARE_MAKE_GC_PTR(type)    \
    template<class T, class... Args, enable_if_t<!is_array_v<T>, int> = 0>                                                                  \
    _NODISCARD gc_##type##_ptr<T> make_gc_##type(Args&&... args){                                                                           \
        void* ptr = GC.take_mem_out<T>();                                                                                                   \
        return ptr ? gc_##type##_ptr<T>(new(ptr)T(std::forward<Args>(args)...)) : gc_##type##_ptr<T>(new T(std::forward<Args>(args)...));   \
    }                                                                                                                                       \
    template <class T, enable_if_t<is_array_v<T>&& extent_v<T> == 0, int> = 0>                                                  \
    _NODISCARD gc_##type##_ptr<T> make_gc_##type(const size_t elementCount){                                                    \
        using ElementType = std::remove_extent_t<T>;                                                                            \
        void* ptr = GC.take_mem_out<ElementType>(elementCount);                                                                 \
        return ptr ? gc_##type##_ptr<T>(new(ptr)ElementType[elementCount]) : gc_##type##_ptr<T>(new ElementType[elementCount]); \
    }
    __DECLARE_MAKE_GC_PTR(unique);
    __DECLARE_MAKE_GC_PTR(shared);
}
/* struct gc_allocate_traits {
     _DECLSPEC_ALLOCATOR static void* _Allocate(const size_t _Bytes) {

         return ::operator new(_Bytes);
     }
 };*/

 /* template<class T>
  struct gc_allocator : public std::allocator<T>
  {
      using alloc_type = std::allocator<T>;
      T* allocate(const size_t count)
      {
          return alloc_type::allocate(count);
      }
  };*/