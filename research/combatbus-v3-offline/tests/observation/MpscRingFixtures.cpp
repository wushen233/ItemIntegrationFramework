#include "MpscObservationRing.h"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using namespace observation::offline;

namespace
{
	struct Record { std::uint64_t producer; std::uint64_t sequence; };
	using Ring = MpscObservationRing<Record, 16>;
	std::uint32_t g_failed{};
	void Check(bool condition, const char* label)
	{
		std::printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
		if (!condition) ++g_failed;
	}
}

int main()
{
	// Deterministically hold the first reservation while a later reservation publishes.
	Ring ordered;
	Ring::Reservation first;
	Check(ordered.TryReserve(first), "first producer reserves a slot");
	Check(ordered.TryPush({ 2, 1 }), "later producer reserves and publishes its slot");
	Record out{};
	Check(ordered.TryPop(out) == Ring::PopResult::Empty,
		"consumer does not read past an earlier unpublished reservation");
	first.Commit({ 1, 1 });
	Check(ordered.TryPop(out) == Ring::PopResult::Item && out.producer == 1,
		"consumer receives the earlier reservation first after publication");
	Check(ordered.TryPop(out) == Ring::PopResult::Item && out.producer == 2,
		"consumer then receives the later completed record");

	// Reentrant producers can reserve/commit without a lock; tombstones are bounded skips.
	Ring reentrant;
	Ring::Reservation outer;
	Check(reentrant.TryReserve(outer), "outer reentrant reservation succeeds");
	Check(reentrant.TryPush({ 4, 2 }), "nested producer publishes while outer record is pending");
	outer.Cancel();
	Check(reentrant.TryPop(out) == Ring::PopResult::Item && out.producer == 4,
		"consumer skips a cancelled reservation and returns nested record");

	Ring full;
	for (std::uint64_t i = 0; i < 16; ++i) Check(full.TryPush({ 7, i }), "bounded ring accepts in-capacity record");
	Check(!full.TryPush({ 7, 17 }) && full.Dropped() == 1 && full.Overflowed() == 1,
		"full ring drops safely and increments loss and overflow counters");

	Ring close;
	Ring::Reservation active;
	Check(close.TryReserve(active), "in-flight producer lease acquired");
	close.Close();
	Check(!close.TryPush({ 8, 1 }), "closed ring rejects new producers");
	Check(!close.WaitForProducers(std::chrono::milliseconds(0)),
		"close cannot claim quiescence while a producer owns a reservation");
	active.Commit({ 8, 0 });
	Check(close.WaitForProducers(std::chrono::milliseconds(100)),
		"external close waiter observes producer completion");
	Check(close.TryPop(out) == Ring::PopResult::Item && out.sequence == 0,
		"closed ring still lets the consumer drain published records");
	Check(close.TryPop(out) == Ring::PopResult::ClosedAndDrained,
		"consumer reports closed only after the queue is drained");

	// Four bounded producers and one consumer: every published item is unique, or counted lost.
	Ring concurrent;
	constexpr std::uint64_t perProducer = 128;
	std::barrier start(5);
	std::array<std::thread, 4> producers;
	std::atomic<std::uint64_t> finished{};
	for (std::uint64_t producer = 0; producer < producers.size(); ++producer) {
		producers[producer] = std::thread([&, producer] {
			start.arrive_and_wait();
			for (std::uint64_t n = 0; n < perProducer; ++n) (void)concurrent.TryPush({ producer, n });
			finished.fetch_add(1, std::memory_order_release);
		});
	}
	start.arrive_and_wait();
	for (auto& thread : producers) thread.join();
	std::array<std::array<bool, perProducer>, 4> seen{};
	std::uint64_t received{};
	while (concurrent.TryPop(out) == Ring::PopResult::Item) {
		if (out.producer < seen.size() && out.sequence < perProducer && !seen[out.producer][out.sequence]) {
			seen[out.producer][out.sequence] = true;
			++received;
		}
		else {
			Check(false, "MPSC consumer receives no duplicate or malformed record");
		}
	}
	const auto lost = concurrent.Dropped();
	Check(finished.load(std::memory_order_acquire) == producers.size(), "all concurrent producers finish");
	Check(received + lost == producers.size() * perProducer,
		"each attempted record is either delivered once or included in the drop count");
	Check(concurrent.Overflowed() <= lost, "overflow count is bounded by total losses");

	return g_failed == 0 ? 0 : 1;
}
