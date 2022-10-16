// 按照正态分布,不停分配内存,看内存复用率,在每一测试帧,每个资源都有 30% 概率释放
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <iostream>
#include <stdlib.h>
#include <random>
#include <thread>
#include <vector>
#include <numeric>

// 禁用 GC log
#define GC_LOG
#include "tiny_gc.hpp"

#pragma comment(lib, "psapi.lib")

#define DISABLE_LOG
#ifndef DISABLE_LOG
#	define GC_LOG(...) printf(__VA_ARGS__)
#else
#	define GC_LOG(...)
#endif

using namespace std;
using namespace tiny_gc;
namespace private_space
{
	template<int bit_count>
	constexpr auto test_data_container()
	{
		if constexpr (bit_count < (8 * sizeof(int8_t)))
		{
			return int8_t();
		}
		else if constexpr (bit_count < (8 * sizeof(int16_t)))
		{
			return int16_t();
		}
		else if constexpr (bit_count < (8 * sizeof(int32_t)))
		{
			return int32_t();
		}
		else if constexpr (bit_count < (8 * sizeof(int32_t)))
		{
			return int64_t();
		}
		else
		{
			return;
		}
	}
}


// 正态分布均值
constexpr double mean = 1 << 10;
// gen_mem_size 输出结果以均值为中心, 上下浮动 range_scala / 2 作为最大最小值 
constexpr double range_scala = 0.7;
static_assert(0 < range_scala && range_scala < 1, "out of range");
constexpr double sigma = mean * range_scala / 3;
constexpr double may_min = mean - 3 * sigma;
constexpr double may_max = mean + 3 * sigma;
int gen_mem_size()
{
	using namespace chrono;

	std::normal_distribution<double> dist(mean, sigma);	
	const auto res = dist(default_random_engine(system_clock::now().time_since_epoch().count()));
	//GC_LOG("normal distribution  min : %lf, max : %lf, value : %lf\n", may_min, may_max, res);
	if (res < may_min) return static_cast<int>(may_min);
	else if (res > may_max) return static_cast<int>(may_max);
	return static_cast<int>(res);
}

// 内存存活率
constexpr double alive_rate = 0.6;
static_assert(0 < range_scala && range_scala < 1, "out of range");
bool is_mem_still_alive()
{
	using namespace chrono;
	constexpr int threashold = 100 * alive_rate;

	std::uniform_int_distribution<int> dist(0, 100);
	const auto res =  dist(default_random_engine(system_clock::now().time_since_epoch().count()));
	GC_LOG("uniform distribution  min : %d, max : %d, value : %d\n", dist.min(), dist.max(), res);
	return (res < threashold);
}

size_t/* byte */ get_proc_total_mem()
{
	PROCESS_MEMORY_COUNTERS info;
	GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
	return info.WorkingSetSize;
}

// 分配用于容纳 may_max 数据的 bit 位数
constexpr int data_bit_count = 14;
static_assert(1 << (data_bit_count + 1) - 1 > may_max, " data bit count is small");
using data_container = decltype(private_space::test_data_container<data_bit_count>());
constexpr int flag_bit_count = (8 * sizeof(data_container) - data_bit_count) / 2;
// 一帧测试状态
struct test_frame_record
{
	data_container  mem_size		: data_bit_count;
	data_container  reuse_success	: flag_bit_count;
	data_container  is_alive		: flag_bit_count;
	test_frame_record(data_container size, bool reused, bool alive) : mem_size(size), reuse_success(reused), is_alive(alive){}
};
int main()
{
	using test_ptr = gc_unique_ptr<char[]>;
	constexpr int test_times = 1000000;

	vector<test_ptr> user;
	vector<test_frame_record> frame_datas;
	user.reserve(test_times);
	frame_datas.reserve(test_times);

	void* ptr;
	int success_times = 0, mem_die_count = 0;

	auto mem_snapshoot_begin = get_proc_total_mem();
	for (int i = 0, mem_size = 0, is_alive = 0; i < test_times; i++)
	{
		mem_size = (int)gen_mem_size();
		printf("begin malloc mem size : %d\n", mem_size);
		assert(0 < mem_size);

		ptr = GC.take_mem_out<char[]>(mem_size);

		if (ptr)
		{
			success_times++;
			user.emplace_back(new (ptr)char[mem_size] {0});
		}
		else
		{
			user.emplace_back(new char[mem_size] {0});
		}
		is_alive = is_mem_still_alive();
		if (!is_alive)
		{
			test_ptr empty_ptr;
			user.rbegin()->swap(empty_ptr);
			mem_die_count++;
		}
		frame_datas.emplace_back(mem_size, static_cast<bool>(ptr), is_alive);
	}
	auto mem_snapshoot_end = get_proc_total_mem();
	
	size_t byte_sum = std::accumulate(frame_datas.begin(), frame_datas.end(), 0, 
		[](size_t current, const test_frame_record& frame) {
			return current + frame.mem_size; 
		}
	);
	printf(
		"memory reuse rate : %lf\n"
		"%memory increase actually : %lf kb, total memory alloc : %lf kb\n",
		static_cast<double>(success_times) / mem_die_count, 
		static_cast<double>(mem_snapshoot_end - mem_snapshoot_begin) / 1000, static_cast<double>(byte_sum) / 1000
	);
	system("pause");
}