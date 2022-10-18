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
        using A = uintptr_t;
        // 1. ���� std::make_unique ��ʽ������������ָ��, make_gc_shared<T[]>(size_t) ����Ҫ T ���޲ι��캯��
        auto p  = make_gc_shared<A>(11);
        auto p1 = make_gc_shared<A[]>(3);

        auto p2 = make_gc_unique<A[]>(3);
        auto p3 = make_gc_unique<A>(12);

        // 2. ������ GC �ÿ����ڴ�, ͨ�� placement new ����������ָ��, �� 2 �ַ�ʽ�����, ���Զ����������г�ʼ������.  
        void* buf = GC.take_mem_out<A>();
        assert(buf);
        std::shared_ptr<A> p4(new (buf)A(13), garbage_collector<A>());
        
        buf = GC.take_mem_out<A[]>(3);
        assert(buf);
        std::unique_ptr<A[], garbage_collector<A[]>> p5(new (buf)A[3]{ 11, 12, 13 });
    }
    // �������п����ڴ�
    GC.collect();
    return 0;
}