#include "tiny_gc.hpp"
struct A
{
    A() : A(0) {}
    A(uintptr_t _d) : d(_d)
    {
        printf("constructed\n");
    }
    uintptr_t d;
    ~A()
    {
        printf("destructed\n");
    }
};

int main()
{
    {
        constexpr unsigned int val = ~(32 - 1);
        std::allocator<A> alloc;
        alloc.allocate(10);
    }
    using namespace tiny_gc;
    // ��ϵͳ�����ڴ�, ���� GC �����ͷ�
	{
        // gc_shared_ptr �����Ӷѷ����ڴ�, �ڴ�������ύ�� GC ����
        auto p = gc_shared_ptr<A>(new A(1));
        auto p1 = gc_shared_ptr<A[]>(new A[3]{ 10, 20, 30});

        // gc_unique_ptr �����Ӷѷ����ڴ�, �ڴ�������ύ�� GC ����
        auto p2 = gc_unique_ptr<A[]>(new A[3]{ 1, 2, 3});
        auto p3 = gc_unique_ptr<A>(new A(2));

        // ԭ�� shared_ptr �� unique_ptr ��ʾָ���ڴ潻�� GC ����
        std::shared_ptr<A> p4(new A(3), garbage_collector<A>());
        std::unique_ptr<A[], garbage_collector<A[]>> p5(new A[3]{1, 2, 3});
    }
    // �� GC �����ڴ�, ���� GC �����ͷ�
    {
        auto p  = make_gc_shared<A>(1);
        auto p1 = make_gc_shared<A[]>(3);

        auto p2 = make_gc_unique<A[]>(3);
        auto p3 = make_gc_unique<A>(3);

        void* buf = GC.take_mem_out<A>();
        assert(buf);
        std::shared_ptr<A> p4(new (buf)A(3), garbage_collector<A>());
        
        constexpr int element_count = 3;
        buf = GC.take_mem_out<A>(element_count);
        assert(buf);
        std::unique_ptr<A[], garbage_collector<A[]>> p5(new (buf)A[element_count]{ 1, 2, 3 });
    }
    GC.collect();
    return 0;
}