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

#include <omp.h>
#include <stdio.h>
#include <iostream>

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
constexpr double alive_rate = 0.3;
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
constexpr int data_bit_count = 15;
static_assert(1 << (data_bit_count + 1) - 1 > may_max, " data bit count is small");
using data_container = decltype(private_space::test_data_container<data_bit_count>());
constexpr int flag_bit_count = (8 * sizeof(data_container) - data_bit_count);
// 一帧测试状态
struct test_frame_record
{
	data_container  mem_size		: data_bit_count;
	data_container  is_alive		: flag_bit_count;
	test_frame_record(data_container size, bool alive) : mem_size(size),  is_alive(alive){}
};
int main()
{
	using test_ptr = gc_unique_ptr<char[]>;
	constexpr int test_times = 100000;

	vector<test_ptr> user;
	mutex user_lock;
	vector<test_frame_record> frame_datas;
	user.reserve(test_times);
	frame_datas.reserve(test_times);


	auto mem_snapshoot_begin = get_proc_total_mem();
#	pragma omp parallel for num_threads(4)
	{
		for (int i = 0; i < test_times; i++)
		{
			int mem_size = (int)gen_mem_size();
			assert(0 < mem_size);
			void* ptr = GC.take_mem_out<char[]>(mem_size);
			assert(ptr);
			{
				std::lock_guard<std::mutex> lock(user_lock);
				user.emplace_back(new (ptr)char[mem_size] {0});
				if (!is_mem_still_alive()) test_ptr().swap(*user.rbegin());
				frame_datas.emplace_back(mem_size, nullptr != *user.rbegin());
			}

		}
	}
	auto mem_snapshoot_end = get_proc_total_mem();
	
	size_t byte_sum = std::accumulate(frame_datas.begin(), frame_datas.end(), 0, 
		[](size_t current, const test_frame_record& frame) {
			return current + frame.mem_size; 
		}
	);
	size_t alive_sum = std::accumulate(frame_datas.begin(), frame_datas.end(), 0,
		[](size_t current, const test_frame_record& frame) {
			return frame.is_alive ? current + 1 : current;
		}
	);
	printf("system :\n    memory grouth actually : %lf mb\n",
		static_cast<double>(mem_snapshoot_end - mem_snapshoot_begin) / (1 << 20)
	);
	printf("user :\n    memory death rate : %lf\n    memory used : %lf mb\n", 
		1.0 - static_cast<double>(alive_sum) / test_times,
		static_cast<double>(byte_sum) / (1 << 20)
	);
	printf("gc :\n    pointer reuse rate : %lf\n    memory reuse rate : %lf\n",
		static_cast<double>(GC.reuse_success_times) / GC.reuse_require_times,
		static_cast<double>(mem_snapshoot_end - mem_snapshoot_begin) / byte_sum
	);
	GC.collect();
	system("pause");
}