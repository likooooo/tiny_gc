#include "tiny_gc.hpp"
struct A
{
    A() : A(0) {}
    /* explicit */ A(uintptr_t _d) : d(_d)
    {
        printf("constructed\n");
    }
    ~A()
    {
        printf("destructed\n");
    }
    uintptr_t d;
};
int main()
{    
    using namespace tiny_gc;
    // 从系统分配内存, 交由 GC 管理释放
	{
        // gc_shared_ptr 主动从堆分配内存, 内存析构后会交给 GC 管理
        auto p = gc_shared_ptr<A>(new A(1));
        auto p1 = gc_shared_ptr<A[]>(new A[3]{ 10, 20, 30});

        // gc_unique_ptr 主动从堆分配内存, 内存析构后会交给 GC 管理
        auto p2 = gc_unique_ptr<A[]>(new A[3]{ 1, 2, 3});
        auto p3 = gc_unique_ptr<A>(new A(2));

        // 原生 shared_ptr 和 unique_ptr 显示指定内存交给 GC 回收
        std::shared_ptr<A> p4(new A(3), garbage_collector<A>());
        std::unique_ptr<A[], garbage_collector<A[]>> p5(new A[3]{1, 2, 3});
    }
    // 从 GC 分配内存, 并由 GC 管理释放
    {
        using A = uintptr_t;
        // 1. 类似 std::make_unique 方式构建两种智能指针, make_gc_shared<T[]>(size_t) 必须要 T 有无参构造函数
        auto p  = make_gc_shared<A>(11);
        auto p1 = make_gc_shared<A[]>(3);

        auto p2 = make_gc_unique<A[]>(3);
        auto p3 = make_gc_unique<A>(12);

        // 2. 主动从 GC 拿空闲内存, 通过 placement new 来构建智能指针, 第 2 种方式更灵活, 可以对数组对象进行初始化构造.  
        void* buf = GC.take_mem_out<A>();
        assert(buf);
        std::shared_ptr<A> p4(new (buf)A(13), garbage_collector<A>());
        
        buf = GC.take_mem_out<A[]>(3);
        assert(buf);
        std::unique_ptr<A[], garbage_collector<A[]>> p5(new (buf)A[3]{ 11, 12, 13 });
    }
    // 回收所有空闲内存
    GC.collect();
    return 0;
}