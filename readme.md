# tiny gc

目前仅支持手动垃圾回收, 后续需要实现垃圾回收线程自动自动进行内存回收
```c++
GC.collect();
```

## example
[example.cpp](https://github.com/likooooo/tiny_gc/blob/main/example.cpp)

## tiny gc 有效性验证
[test.reuse_rate.cpp](https://github.com/likooooo/tiny_gc/blob/main/test.reuse_rate.cpp)
```c++
// 正态分布均值
constexpr double mean = 1 << 10;
// 内存存活率
constexpr double alive_rate = 0.3;
```
通过修改上述两个参数, 可以模拟不同情况下 tiny gc 的有效性


