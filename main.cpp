#include "include/order_matching/OrderBook.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

namespace {

using order_matching::OrderBook;
using order_matching::OrderRequest;
using order_matching::OrderType;
using order_matching::Quantity;
using order_matching::Side;
using Clock = std::chrono::high_resolution_clock;

constexpr std::size_t kRequestCount = 1'000'000;
constexpr std::size_t kMaxPriceLevels = 64;
constexpr order_matching::Price kMidPrice = 100'000;

struct BenchmarkResult {
	std::uint64_t total_nanoseconds{0};
	std::size_t processed{0};
	std::size_t rejected{0};
	std::size_t resting_orders{0};
	std::size_t bid_levels{0};
	std::size_t ask_levels{0};
};

std::vector<OrderRequest> generate_orders() {
	std::vector<OrderRequest> requests;
	requests.reserve(kRequestCount);

	std::mt19937_64 generator(0x5EED'2026ULL);
	std::uniform_int_distribution<int> price_offset(-8, 8);
	std::uniform_int_distribution<Quantity> quantity(1, 100);

	for (std::size_t index = 0; index < kRequestCount; ++index) {
		requests.push_back(OrderRequest{
			static_cast<order_matching::OrderId>(index + 1),
			kMidPrice + price_offset(generator),
			quantity(generator),
			index % 2 == 0 ? Side::Buy : Side::Sell,
			OrderType::Limit,
			order_matching::TimeInForce::GoodTillCancel,
		});
	}
	return requests;
}

double percentile_ns(const std::vector<std::uint64_t>& samples, double percentile) {
	const std::size_t index = static_cast<std::size_t>(
		percentile * static_cast<double>(samples.size() - 1));
	return static_cast<double>(samples[index]);
}

void print_summary(const BenchmarkResult& result,
				   const std::vector<std::uint64_t>& latencies) {
	const double seconds = static_cast<double>(result.total_nanoseconds) / 1'000'000'000.0;
	const double throughput = seconds == 0.0
								  ? 0.0
								  : static_cast<double>(result.processed) / seconds;
	const double average_ns = result.processed == 0
								  ? 0.0
								  : static_cast<double>(result.total_nanoseconds) /
										static_cast<double>(result.processed);

	std::cout << "\nOrder Matching Engine Benchmark\n"
			  << "--------------------------------\n"
			  << "Requests generated : " << kRequestCount << '\n'
			  << "Requests processed : " << result.processed << '\n'
			  << "Requests rejected  : " << result.rejected << '\n'
			  << "Elapsed time       : " << std::fixed << std::setprecision(3)
			  << seconds << " s\n"
			  << "Throughput         : " << std::fixed << std::setprecision(0)
			  << throughput << " req/s\n"
			  << "Average latency    : " << std::fixed << std::setprecision(2)
			  << average_ns << " ns/order\n"
			  << "p50 latency        : " << percentile_ns(latencies, 0.500) << " ns\n"
			  << "p99 latency        : " << percentile_ns(latencies, 0.990) << " ns\n"
			  << "p99.9 latency      : " << percentile_ns(latencies, 0.999) << " ns\n"
			  << "Resting orders     : " << result.resting_orders << '\n'
			  << "Bid/ask levels     : " << result.bid_levels << '/'
			  << result.ask_levels << '\n';
}

} // namespace

int main() {
	const std::vector<OrderRequest> requests = generate_orders();
	std::vector<std::uint64_t> latencies;
	latencies.reserve(kRequestCount);

	using Book = OrderBook<kRequestCount + 1, kMaxPriceLevels>;
	auto book = std::make_unique<Book>();
	BenchmarkResult result;

	const auto benchmark_start = Clock::now();
	for (const OrderRequest& request : requests) {
		const auto start = Clock::now();
		const bool accepted = book->addOrder(request);
		const auto finish = Clock::now();
		latencies.push_back(static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count()));
		if (accepted) {
			++result.processed;
		} else {
			++result.rejected;
		}
	}
	const auto benchmark_finish = Clock::now();
	result.total_nanoseconds = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			benchmark_finish - benchmark_start)
			.count());
	result.resting_orders = book->order_count();
	result.bid_levels = book->bid_level_count();
	result.ask_levels = book->ask_level_count();

	if (result.processed != kRequestCount || result.rejected != 0 ||
		result.resting_orders > result.processed ||
		result.bid_levels > kMaxPriceLevels || result.ask_levels > kMaxPriceLevels) {
		std::cerr << "Benchmark correctness validation failed\n";
		return 1;
	}

	std::sort(latencies.begin(), latencies.end());
	print_summary(result, latencies);
	return 0;
}
