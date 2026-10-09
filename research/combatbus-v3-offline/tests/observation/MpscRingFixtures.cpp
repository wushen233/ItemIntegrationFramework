#include "MpscObservationRing.h"

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using namespace observation::offline;

namespace
{
	struct Record { std::uint64_t producer; std::uint64_t sequence; std::uint64_t check; };
	using Ring = MpscObservationRing<Record, 16>;
	using ConcurrentRing = MpscObservationRing<Record, 64>;
	std::uint32_t g_failed{};
	void Check(bool condition, const char* label)
	{
		std::printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
		if (!condition) ++g_failed;
	}

	constexpr std::uint64_t CheckWord(std::uint64_t producer, std::uint64_t sequence)
	{
		return 0x9E3779B97F4A7C15ull ^ (producer << 48) ^ sequence;
	}

	bool CloseAndDrain(auto& ring, Record& output)
	{
		using Result = decltype(ring.TryPop(output));
		ring.Close();
		return ring.WaitForProducers(std::chrono::milliseconds(100)) &&
			ring.TryPop(output) == Result::ClosedAndDrained;
	}
}

int main()
{
	// Deterministically hold the first reservation while a later reservation publishes.
	Ring ordered;
	Ring::Reservation first;
	Check(ordered.TryReserve(first), "first producer reserves a slot");
	Check(ordered.TryPush({ 2, 1, CheckWord(2, 1) }), "later producer reserves and publishes its slot");
	Record out{};
	Check(ordered.TryPop(out) == Ring::PopResult::Empty,
		"consumer does not read past an earlier unpublished reservation");
	first.Commit({ 1, 1, CheckWord(1, 1) });
	Check(ordered.TryPop(out) == Ring::PopResult::Item && out.producer == 1,
		"consumer receives the earlier reservation first after publication");
	Check(ordered.TryPop(out) == Ring::PopResult::Item && out.producer == 2,
		"consumer then receives the later completed record");
	Check(CloseAndDrain(ordered, out), "ordered fixture closes and drains before releasing storage");

	Ring delayed;
	Ring::Reservation delayedReservation;
	Check(delayed.TryReserve(delayedReservation), "delayed reservation reserves the queue head");
	std::barrier delayedStart(3); // delayed producer, concurrent consumer, coordinator
	std::atomic<bool> laterPublished{};
	std::atomic<bool> consumerObservedBlocked{};
	std::atomic<bool> releaseDelayedConsumer{};
	std::atomic<bool> delayedOverlapFailed{};
	Record delayedOutput{};
	std::thread delayedProducer([&] {
		delayedStart.arrive_and_wait();
		laterPublished.store(delayed.TryPush({ 9, 1, CheckWord(9, 1) }), std::memory_order_release);
	});
	std::thread delayedConsumer([&] {
		delayedStart.arrive_and_wait();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!laterPublished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		if (!laterPublished.load(std::memory_order_acquire) || delayed.TryPop(delayedOutput) != Ring::PopResult::Empty)
			delayedOverlapFailed.store(true, std::memory_order_release);
		consumerObservedBlocked.store(true, std::memory_order_release);
		while (!releaseDelayedConsumer.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		if (!releaseDelayedConsumer.load(std::memory_order_acquire) || delayed.TryPop(delayedOutput) != Ring::PopResult::Item ||
			delayedOutput.producer != 9 || delayedOutput.sequence != 1)
			delayedOverlapFailed.store(true, std::memory_order_release);
	});
	delayedStart.arrive_and_wait();
	const auto delayedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while ((!laterPublished.load(std::memory_order_acquire) || !consumerObservedBlocked.load(std::memory_order_acquire)) &&
		std::chrono::steady_clock::now() < delayedDeadline) std::this_thread::yield();
	const bool delayedHandshake = laterPublished.load(std::memory_order_acquire) &&
		consumerObservedBlocked.load(std::memory_order_acquire);
	delayedReservation.Cancel();
	releaseDelayedConsumer.store(true, std::memory_order_release);
	delayedProducer.join();
	delayedConsumer.join();
	Check(delayedHandshake && !delayedOverlapFailed.load(std::memory_order_acquire),
		"concurrent consumer cannot pass a delayed head and receives later record after Cancel");
	Check(CloseAndDrain(delayed, out), "delayed reservation fixture closes and drains before release");

	// Reentrant producers can reserve/commit without a lock; tombstones are bounded skips.
	Ring reentrant;
	Ring::Reservation outer;
	Check(reentrant.TryReserve(outer), "outer reentrant reservation succeeds");
	Check(reentrant.TryPush({ 4, 2, CheckWord(4, 2) }), "nested producer publishes while outer record is pending");
	outer.Cancel();
	Check(reentrant.TryPop(out) == Ring::PopResult::Item && out.producer == 4,
		"consumer skips a cancelled reservation and returns nested record");
	Check(CloseAndDrain(reentrant, out), "reentrant fixture closes and drains before release");

	Ring full;
	for (std::uint64_t i = 0; i < 16; ++i) Check(full.TryPush({ 7, i, CheckWord(7, i) }), "bounded ring accepts in-capacity record");
	Check(!full.TryPush({ 7, 17, CheckWord(7, 17) }) && full.Dropped() == 1 && full.Overflowed() == 1,
		"full ring drops safely and increments loss and overflow counters");
	for (std::uint64_t i = 0; i < 16; ++i) Check(full.TryPop(out) == Ring::PopResult::Item,
		"consumer drains a full-ring record");
	Check(CloseAndDrain(full, out), "full fixture closes and drains before release");

	Ring open;
	Check(!open.WaitForProducers(std::chrono::milliseconds(0)),
		"open and empty ring cannot claim quiescence");
	Check(CloseAndDrain(open, out), "open-empty fixture closes before storage release");
	Ring close;
	Ring::Reservation active;
	Check(close.TryReserve(active), "in-flight producer lease acquired");
	close.Close();
	Check(!close.TryPush({ 8, 1, CheckWord(8, 1) }), "closed ring rejects new producers");
	Check(!close.WaitForProducers(std::chrono::milliseconds(0)),
		"close cannot claim quiescence while a producer owns a reservation");
	active.Commit({ 8, 0, CheckWord(8, 0) });
	Check(close.WaitForProducers(std::chrono::milliseconds(100)),
		"closed ring waiter observes completion of a committed reservation");
	Check(close.TryPop(out) == Ring::PopResult::Item && out.sequence == 0,
		"closed ring still lets the consumer drain published records");
	Check(close.TryPop(out) == Ring::PopResult::ClosedAndDrained && close.IsClosedAndDrained(),
		"storage is releasable only after the closed queue is drained");
	Ring cancelledAfterClose;
	Ring::Reservation cancelled;
	Check(cancelledAfterClose.TryReserve(cancelled), "cancellable producer lease acquired");
	cancelledAfterClose.Close();
	cancelled.Cancel();
	Check(cancelledAfterClose.WaitForProducers(std::chrono::milliseconds(100)) &&
		cancelledAfterClose.TryPop(out) == Ring::PopResult::ClosedAndDrained,
		"closed ring reaches quiescence after cancellation and drains the tombstone");
	Ring waiterRace;
	Ring::Reservation waiterReservation;
	Check(waiterRace.TryReserve(waiterReservation), "waiter race holds an active reservation");
	waiterRace.Close();
	std::barrier waiterStart(2);
	std::atomic<bool> waiterSucceeded{};
	std::thread quiescenceWaiter([&] {
		waiterStart.arrive_and_wait();
		waiterSucceeded.store(waiterRace.WaitForProducers(std::chrono::seconds(2)), std::memory_order_release);
	});
	waiterStart.arrive_and_wait();
	waiterReservation.Commit({ 10, 0, CheckWord(10, 0) });
	quiescenceWaiter.join();
	Check(waiterSucceeded.load(std::memory_order_acquire),
		"wait already racing with final producer release observes quiescence within its deadline");
	Check(waiterRace.TryPop(out) == Ring::PopResult::Item && CloseAndDrain(waiterRace, out),
		"racing waiter fixture drains before releasing storage");

	// Four producers and a consumer run concurrently. The first four records form a
	// deterministic overlap witness: producers remain active until the consumer sees them.
	ConcurrentRing concurrent;
	constexpr std::uint64_t producerCount = 4;
	constexpr std::uint64_t perProducer = 512;
	constexpr auto timeout = std::chrono::seconds(8);
	std::barrier start(static_cast<std::ptrdiff_t>(producerCount + 2)); // 4 producers, consumer, coordinator
	std::array<std::thread, producerCount> producers;
	std::atomic<std::uint64_t> activeProducers{};
	std::atomic<std::uint64_t> producersDone{};
	std::atomic<std::uint64_t> successfulPushes{};
	std::atomic<std::uint64_t> attempts{};
	std::atomic<bool> initialRecordsSeen{};
	std::atomic<bool> abort{};
	std::atomic<bool> overlapWitness{};
	std::array<std::array<bool, perProducer>, producerCount> seen{};
	std::atomic<std::uint64_t> received{};
	std::atomic<bool> malformedOrDuplicate{};
	std::thread consumer([&] {
		start.arrive_and_wait();
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		std::uint64_t initialSeen{};
		for (;;) {
			if (std::chrono::steady_clock::now() >= deadline) {
				abort.store(true, std::memory_order_release);
				break;
			}
			auto result = concurrent.TryPop(out);
			if (result == ConcurrentRing::PopResult::Item) {
				if (activeProducers.load(std::memory_order_acquire) > 0) overlapWitness.store(true, std::memory_order_release);
				if (out.producer >= producerCount || out.sequence >= perProducer ||
					out.check != CheckWord(out.producer, out.sequence) || seen[out.producer][out.sequence]) {
					malformedOrDuplicate.store(true, std::memory_order_release);
				}
				else {
					seen[out.producer][out.sequence] = true;
					++received;
					if (out.sequence == 0) ++initialSeen;
				}
				if (initialSeen == producerCount) initialRecordsSeen.store(true, std::memory_order_release);
			}
			else if (result == ConcurrentRing::PopResult::ClosedAndDrained) {
				break;
			}
			else if (abort.load(std::memory_order_acquire)) {
				break;
			}
			else {
				std::this_thread::yield();
			}
		}
	});
	for (std::uint64_t producer = 0; producer < producerCount; ++producer) {
		producers[producer] = std::thread([&, producer] {
			start.arrive_and_wait();
			activeProducers.fetch_add(1, std::memory_order_acq_rel);
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			for (std::uint64_t sequence = 0; sequence < perProducer && !abort.load(std::memory_order_acquire); ++sequence) {
				bool published = false;
				while (!published && !abort.load(std::memory_order_acquire)) {
					attempts.fetch_add(1, std::memory_order_relaxed);
					published = concurrent.TryPush({ producer, sequence, CheckWord(producer, sequence) });
					if (published) successfulPushes.fetch_add(1, std::memory_order_relaxed);
					else if (std::chrono::steady_clock::now() >= deadline) abort.store(true, std::memory_order_release);
					else std::this_thread::yield();
				}
				if (sequence == 0) {
					while (!initialRecordsSeen.load(std::memory_order_acquire) &&
						!abort.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
						std::this_thread::yield();
					}
					if (!initialRecordsSeen.load(std::memory_order_acquire)) abort.store(true, std::memory_order_release);
				}
			}
			activeProducers.fetch_sub(1, std::memory_order_release);
			producersDone.fetch_add(1, std::memory_order_release);
		});
	}
	start.arrive_and_wait();
	for (auto& thread : producers) thread.join();
	concurrent.Close();
	const bool producerQuiescent = concurrent.WaitForProducers(std::chrono::milliseconds(100));
	consumer.join();
	Check(producerQuiescent, "closed concurrent ring confirms all producer leases ended");
	Check(producersDone.load(std::memory_order_acquire) == producerCount,
		"all four bounded producers exit and join");
	Check(initialRecordsSeen.load(std::memory_order_acquire) && overlapWitness.load(std::memory_order_acquire),
		"consumer Pop is proven to overlap active producers by the initial-record handshake");
	Check(!abort.load(std::memory_order_acquire), "producer/consumer overlap fixture stays within its deadline");
	Check(!malformedOrDuplicate.load(std::memory_order_acquire), "consumer sees no malformed, duplicate, or uninitialized record");
	Check(received.load(std::memory_order_acquire) == successfulPushes.load(std::memory_order_acquire),
		"every successfully published record is consumed exactly once");
	Check(successfulPushes.load(std::memory_order_acquire) == producerCount * perProducer,
		"four producers force multiple complete ring wraparounds and slot reuse");
	Check(attempts.load(std::memory_order_acquire) == successfulPushes.load(std::memory_order_acquire) + concurrent.Dropped(),
		"each failed attempt is represented exactly once in the drop count");
	Check(concurrent.TryPop(out) == ConcurrentRing::PopResult::ClosedAndDrained && concurrent.IsClosedAndDrained(),
		"close, producer quiescence, and consumer drain complete before ring destruction");

	return g_failed == 0 ? 0 : 1;
}
