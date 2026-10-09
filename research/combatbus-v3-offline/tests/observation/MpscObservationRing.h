#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <utility>

namespace observation::offline
{
	template <class T, std::size_t Capacity, std::size_t RetryLimit = 64>
	class MpscObservationRing
	{
		static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
		static_assert(std::is_trivially_copyable_v<T>, "records must be trivially copyable");
		static_assert(std::is_trivially_destructible_v<T>, "records must be trivially destructible");

		static constexpr std::uint64_t kClosed = std::uint64_t{ 1 } << 63;
		static constexpr std::uint64_t kCountMask = ~kClosed;

		struct Slot
		{
			std::atomic<std::uint64_t> sequence{};
			T value{};
			bool cancelled{};
		};

	public:
		enum class PopResult { Item, Empty, ClosedAndDrained };

		class Reservation
		{
		public:
			Reservation() = default;
			Reservation(const Reservation&) = delete;
			Reservation& operator=(const Reservation&) = delete;
			Reservation(Reservation&& other) noexcept { MoveFrom(other); }
			Reservation& operator=(Reservation&& other) noexcept
			{
				if (this != &other) {
					Cancel();
					MoveFrom(other);
				}
				return *this;
			}
			~Reservation() { Cancel(); }

			void Commit(const T& value) noexcept
			{
				if (!_owner) return;
				_slot->value = value;
				_slot->cancelled = false;
				_slot->sequence.store(_position + 1, std::memory_order_release);
				Release();
			}

			void Cancel() noexcept
			{
				if (!_owner) return;
				_slot->cancelled = true;
				_slot->sequence.store(_position + 1, std::memory_order_release);
				Release();
			}

		private:
			friend class MpscObservationRing;
			Reservation(MpscObservationRing* owner, Slot* slot, std::uint64_t position) noexcept :
				_owner(owner), _slot(slot), _position(position) {}
			void Release() noexcept
			{
				auto* owner = std::exchange(_owner, nullptr);
				_slot = nullptr;
				owner->ReleaseProducer();
			}
			void MoveFrom(Reservation& other) noexcept
			{
				_owner = std::exchange(other._owner, nullptr);
				_slot = std::exchange(other._slot, nullptr);
				_position = other._position;
			}
			MpscObservationRing* _owner{};
			Slot* _slot{};
			std::uint64_t _position{};
		};

		MpscObservationRing() noexcept
		{
			for (std::size_t i = 0; i < Capacity; ++i) _slots[i].sequence.store(i, std::memory_order_relaxed);
		}
		MpscObservationRing(const MpscObservationRing&) = delete;
		MpscObservationRing& operator=(const MpscObservationRing&) = delete;

		[[nodiscard]] bool TryReserve(Reservation& output) noexcept
		{
			if (output._owner || !AcquireProducer()) return CountDrop(false);
			std::uint64_t position = _enqueue.load(std::memory_order_relaxed);
			for (std::size_t attempt = 0; attempt < RetryLimit; ++attempt) {
				auto& slot = _slots[position & (Capacity - 1)];
				const auto sequence = slot.sequence.load(std::memory_order_acquire);
				const auto difference = static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(position);
				if (difference == 0) {
					if (_enqueue.compare_exchange_weak(position, position + 1,
							std::memory_order_relaxed, std::memory_order_relaxed)) {
						output = Reservation(this, &slot, position);
						return true;
					}
				}
				else if (difference < 0) {
					ReleaseProducer();
					return CountDrop(true);
				}
				else {
					position = _enqueue.load(std::memory_order_relaxed);
				}
			}
			ReleaseProducer();
			return CountDrop(true);
		}

		[[nodiscard]] bool TryPush(const T& value) noexcept
		{
			Reservation reservation;
			if (!TryReserve(reservation)) return false;
			reservation.Commit(value);
			return true;
		}

		[[nodiscard]] PopResult TryPop(T& output) noexcept
		{
			for (std::size_t skipped = 0; skipped < Capacity; ++skipped) {
				const auto position = _dequeue.load(std::memory_order_relaxed);
				auto& slot = _slots[position & (Capacity - 1)];
				const auto sequence = slot.sequence.load(std::memory_order_acquire);
				const auto difference = static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(position + 1);
				if (difference != 0) return IsClosedAndDrained() ? PopResult::ClosedAndDrained : PopResult::Empty;
				_dequeue.store(position + 1, std::memory_order_relaxed);
				const bool cancelled = slot.cancelled;
				if (!cancelled) output = slot.value;
				slot.sequence.store(position + Capacity, std::memory_order_release);
				if (!cancelled) return PopResult::Item;
			}
			return PopResult::Empty;
		}

		void Close() noexcept { _producerState.fetch_or(kClosed, std::memory_order_acq_rel); }
		[[nodiscard]] bool WaitForProducers(std::chrono::milliseconds timeout)
		{
			std::unique_lock lock(_closeMutex);
			return _closeCv.wait_for(lock, timeout, [this] {
				return (_producerState.load(std::memory_order_acquire) & kCountMask) == 0;
			});
		}
		[[nodiscard]] std::uint64_t Dropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }
		[[nodiscard]] std::uint64_t Overflowed() const noexcept { return _overflowed.load(std::memory_order_relaxed); }
		[[nodiscard]] bool IsClosedAndDrained() const noexcept
		{
			return (_producerState.load(std::memory_order_acquire) & kClosed) != 0 &&
				(_producerState.load(std::memory_order_acquire) & kCountMask) == 0 &&
				_dequeue.load(std::memory_order_relaxed) == _enqueue.load(std::memory_order_relaxed);
		}

	private:
		[[nodiscard]] bool AcquireProducer() noexcept
		{
			auto state = _producerState.load(std::memory_order_acquire);
			for (std::size_t attempt = 0; attempt < RetryLimit; ++attempt) {
				if ((state & kClosed) != 0 || (state & kCountMask) == kCountMask) return false;
				if (_producerState.compare_exchange_weak(state, state + 1,
						std::memory_order_acq_rel, std::memory_order_acquire)) return true;
			}
			return false;
		}
		void ReleaseProducer() noexcept
		{
			const auto prior = _producerState.fetch_sub(1, std::memory_order_release);
			if ((prior & kClosed) != 0 && (prior & kCountMask) == 1) _closeCv.notify_all();
		}
		bool CountDrop(bool overflow) noexcept
		{
			_dropped.fetch_add(1, std::memory_order_relaxed);
			if (overflow) _overflowed.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		std::array<Slot, Capacity> _slots{};
		std::atomic<std::uint64_t> _enqueue{};
		std::atomic<std::uint64_t> _dequeue{};
		std::atomic<std::uint64_t> _producerState{};
		std::atomic<std::uint64_t> _dropped{};
		std::atomic<std::uint64_t> _overflowed{};
		std::mutex _closeMutex;
		std::condition_variable _closeCv;
	};
}
