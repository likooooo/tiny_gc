#pragma once
#include <stdio.h>
#include <memory>
#include <algorithm>
#include <vector>
#include <map>
#include <assert.h>
#include <mutex>
#include <malloc.h>
#include <atomic>

#ifndef GC_LOG
#   define GC_LOG(...) printf(__VA_ARGS__);
#endif
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


    //template <class _Yty, class = void>
    //struct _Can_array_delete : false_type {};
    //template <class _Yty>
    //struct _Can_array_delete<_Yty, void_t<decltype(delete[] _STD declval<_Yty*>())>> : true_type {};

    template<class T, class = void>
    struct has_destructor : std::false_type {};
    template<class T>
    struct has_destructor<T, decltype(std::declval<T&>().~T())> : std::true_type {};
    template<class T>
    constexpr bool has_destructor_v = has_destructor<T>::value;

    template<class T, class = void>
    struct is_cpp_default_type : std::false_type {};
    template<class T>
    struct is_cpp_default_type <T, std::enable_if_t<
        std::is_arithmetic_v<T> &&  // 算数类型
        std::is_pointer_v<T>    &&  // 指针类型
        true
        >>: std::true_type{};
    template<class T>
    constexpr bool is_cpp_default_type_v = is_cpp_default_type<T>::value;
}

namespace tiny_gc
{
    using std::vector;
    using std::mutex;
    using std::atomic;
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
    struct gc_debugger
    {
#ifndef DISABLE_GC_DEBUG
        void record_reuse_mem_success()
        {
            reuse_success_times++;
            reuse_require_times++;
        }
        void record_reuse_mem_failed()
        {
            reuse_require_times++;
        }
        atomic<size_t> reuse_success_times{ 0 };
        atomic<size_t> reuse_require_times{ 0 };
#else
        void record_reuse_mem_success() {}
        void record_reuse_mem_failed() {}
#endif
    };
    // 32 个block, 每个 block 的 index 为 [0, 32), 代表内存块大小为 : 1 << index
    constexpr int max_block_count = 8 * sizeof(int);
    // GC 内存管理
    class gc_manager final : public gc_debugger
    {
        struct thread_save_vector
        {
            vector<void*> container; 
            mutex container_lock;   // 如果 container 能预设大小, 可以用原子操作替换 lock

            void push_back(void* obj) 
            {
                gc_lock lock(container_lock);
                container.push_back(obj);
            }
            void* take_last_out()
            {
                gc_lock lock(container_lock);
                if (container.size())
                {
                    void* p = *container.rbegin();
                    container.pop_back();
                    return p;
                }
                return nullptr;
            }
        };
        thread_save_vector memory_block_available[max_block_count];
    public:
        ~gc_manager()
        {
            collect();
        }
        // 尝试返回单个元素 T 的内存块指针, 如果 GC 没有符合条件的空闲内存, 会通过 malloc 构建
        template<class T> _NODISCARD void* take_mem_out()
        {
            constexpr int index = private_space::hightest_bit_index<sizeof(T)>() - 1;
            constexpr int block_index = sizeof(T) <= 1 << index ? index : index + 1;
            constexpr int current_max_mem_size = 1 << block_index;
            static_assert(block_index < max_block_count, "out of range");

            void* ptr;
            if (memory_block_available[block_index].container.size())
            {
                ptr = memory_block_available[block_index].take_last_out();
                gc_debugger::record_reuse_mem_success();
                GC_LOG("[ block-%2d ] take memory out from GC %p\n", block_index, ptr);
                return ptr;
            }
            ptr = malloc(current_max_mem_size);
            GC_LOG("[ block-%2d ] malloc new memory by GC %p\n", block_index, ptr);
            gc_debugger::record_reuse_mem_failed();

            return ptr;
        }
        // 尝试返回多个元素 T 的内存块指针, 如果 GC 没有符合条件的空闲内存, 会通过 malloc 构建
        template<class T, enable_if_t<is_array_v<T>&& extent_v<T> == 0, int> = 0> 
        _NODISCARD void* take_mem_out(const size_t elementCount)
        {
            using ElementType = std::remove_extent_t<T>;

            int requireBytes, block_index;
            if constexpr (
                private_space::has_destructor_v<T> &&
                !std::is_pod_v<T>)
            {
                requireBytes = elementCount * sizeof(ElementType) + sizeof(array_ptr_header);
            }
            else
            {
                requireBytes = elementCount * sizeof(ElementType);
            }
            int index = private_space::hightest_bit_index(requireBytes) - 1;
            block_index = requireBytes <= 1 << index ? index : index + 1;
            int current_max_mem_size = 1 << block_index;

            void* ptr;
            if (memory_block_available[block_index].container.size())
            {
                ptr = memory_block_available[block_index].take_last_out();
                gc_debugger::record_reuse_mem_success();
                GC_LOG("[ block-%2d ] take memory out from GC %p\n", block_index, ptr);
                return ptr;
            }
            ptr = malloc(current_max_mem_size);
            GC_LOG("[ block-%2d ] malloc new memory by GC %p\n", block_index, ptr);
            gc_debugger::record_reuse_mem_failed();

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
        void push_unused_mem(void* ptr, const int totalByte)
        {
            int block_index = private_space::hightest_bit_index(totalByte) - 1;
            GC_ASSERT(block_index < max_block_count);

            memory_block_available[block_index].push_back(ptr);
            GC_LOG("[ block-%2d ] push unused memory to GC %p, mem bytes : %d\n", block_index, ptr, totalByte);
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
            GC.push_unused_mem(ptr, reinterpret_cast<int*>(ptr)[-4]);
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

            if constexpr (
                (private_space::has_destructor_v<TFrom> ||  private_space::has_destructor_v<T>) &&
                !std::is_pod_v<TFrom>)
            {
                array_ptr_header* pObjCount = reinterpret_cast<array_ptr_header*>(ptr) - 1;
                std::for_each(ptr, ptr + *pObjCount, [](TFrom& t) {t.~TFrom(); });
                GC.push_unused_mem(pObjCount, reinterpret_cast<int*>(pObjCount)[-4]);
            }
            else
            {
                GC.push_unused_mem(ptr, reinterpret_cast<int*>(ptr)[-4]);
            }

        }
    };
    // 优先尝试从 GC 空闲内存中构建智能指针, 若相应内存块没有空闲内存, 则从堆上分配内存后构建.
#define __DECLARE_MAKE_GC_PTR(type)    \
    template<class T, class... Args, enable_if_t<!is_array_v<T>, int> = 0>  \
    _NODISCARD gc_##type##_ptr<T> make_gc_##type(Args&&... args){           \
        void* ptr = GC.take_mem_out<T>();                                   \
        assert(ptr);                                                        \
        return gc_##type##_ptr<T>(new(ptr)T(std::forward<Args>(args)...));  \
    }                                                                       \
    template <class T, enable_if_t<is_array_v<T>&& extent_v<T> == 0, int> = 0>  \
    _NODISCARD gc_##type##_ptr<T> make_gc_##type(const size_t elementCount){    \
        using ElementType = std::remove_extent_t<T>;                            \
        void* ptr = GC.take_mem_out<T>(elementCount);                           \
        assert(ptr);                                                            \
        return gc_##type##_ptr<T>(new(ptr)ElementType[elementCount]);           \
    }
    __DECLARE_MAKE_GC_PTR(unique);
    __DECLARE_MAKE_GC_PTR(shared);
}