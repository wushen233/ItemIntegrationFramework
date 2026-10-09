#include "MpscObservationRing.h"

#include <Windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>

#if !defined(_M_X64)
#error This fixture intentionally validates only the Win64 ABI.
#endif

std::uint64_t __fastcall ObservationWrapper(std::uint32_t, void*, void*, void*, float*);
extern "C" std::uint64_t g_asm_wrapper_return;
extern "C" std::uint32_t g_asm_register_preservation_ok;
extern "C" std::uint32_t __fastcall AbiCallWithNonvolatileSentinels(std::uint32_t, void*, void*, void*, float*);
extern "C" std::uint64_t (__fastcall *g_asm_wrapper_target)(std::uint32_t, void*, void*, void*, float*) = nullptr;

namespace
{
	using CallFn = std::uint64_t(__fastcall*)(std::uint32_t, void*, void*, void*, float*);
	struct CallRecord { std::uint64_t id; float before; float after; std::uint32_t thread; };
	using TraceRing = observation::offline::MpscObservationRing<CallRecord, 8>;

	struct SeenArgs
	{
		std::uint32_t entry{};
		void* second{};
		void* third{};
		void* fourth{};
		float* fifth{};
		std::uint64_t calls{};
		bool aligned{};
	} g_seen;

	CallFn g_original{};
	TraceRing* g_trace{};
	std::atomic<bool> g_mutate{};
	std::atomic<bool> g_throw{};
	std::atomic<bool> g_reenter{};
	std::atomic<std::uint64_t> g_recordId{ 1 };
	std::uint64_t g_nestedResult{};
	std::uint32_t g_failed{};

	bool Expect(bool condition, const char* message)
	{
		if (!condition) {
			std::fprintf(stderr, "FAIL: %s\n", message);
			++g_failed;
			return false;
		}
		std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
		std::fflush(stdout);
		return condition;
	}

	bool EntryStackAligned() noexcept
	{
		return (reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()) & 0xF) == 8;
	}

	__declspec(noinline) std::uint64_t __fastcall OriginalEntry(std::uint32_t entry, void* second, void* third, void* fourth, float* fifth)
	{
		++g_seen.calls;
		g_seen.entry = entry;
		g_seen.second = second;
		g_seen.third = third;
		g_seen.fourth = fourth;
		g_seen.fifth = fifth;
		g_seen.aligned = EntryStackAligned();
		if (g_throw.load(std::memory_order_relaxed)) throw std::runtime_error("fixture exception");
		if (g_mutate.load(std::memory_order_relaxed)) *fifth += 3.25F;
		if (g_reenter.exchange(false, std::memory_order_relaxed)) {
			float nested = 2.0F;
			g_nestedResult = ObservationWrapper(entry, second, third, fourth, &nested);
		}
		return 0xD00DFEEDCAFEBEEFULL;
	}

}

// The native callsite needs the Win64 machine ABI, not an exported C symbol. Keep C++ language
// linkage so C++ exception unwinding through the wrapper remains testable by both compilers.
__declspec(noinline) std::uint64_t __fastcall ObservationWrapper(std::uint32_t entry, void* second, void* third, void* fourth, float* fifth)
{
	const auto before = *fifth;
	const auto result = g_original(entry, second, third, fourth, fifth);
	const auto after = *fifth;
	if (g_trace) {
		const CallRecord record{ g_recordId.fetch_add(1, std::memory_order_relaxed), before, after, GetCurrentThreadId() };
		(void)g_trace->TryPush(record);  // A full/closed logger never retries the native call.
	}
	return result;
}

extern "C" std::uint64_t g_asm_wrapper_return = 0;
extern "C" std::uint32_t g_asm_register_preservation_ok = 0;

int main()
{
	g_original = &OriginalEntry;
	g_asm_wrapper_target = &ObservationWrapper;
	void* second = reinterpret_cast<void*>(0x1111222233334444ull);
	void* third = reinterpret_cast<void*>(0x5555666677778888ull);
	void* fourth = reinterpret_cast<void*>(0x9999AAAABBBBCCCCull);
	float value = 10.0F;
	TraceRing trace;
	g_trace = &trace;
	const auto result = ObservationWrapper(0x24, second, third, fourth, &value);
	Expect(result == 0xD00DFEEDCAFEBEEFULL, "wrapper forwards the full 64-bit return value");
	Expect(g_seen.calls == 1, "original function is called exactly once");
	Expect(g_seen.entry == 0x24 && g_seen.second == second && g_seen.third == third &&
		g_seen.fourth == fourth && g_seen.fifth == &value, "RCX/RDX/R8/R9/fifth stack argument arrive intact");
	Expect(g_seen.aligned && EntryStackAligned(), "Win64 callee entry return-address slots have ABI alignment");
	DWORD64 imageBase{};
	Expect(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&ObservationWrapper), &imageBase, nullptr) != nullptr,
		"compiled C++ wrapper has registered Win64 unwind metadata");
	Expect(value == 10.0F, "observer wrapper does not write the caller float");
	CallRecord record{};
	Expect(trace.TryPop(record) == TraceRing::PopResult::Item && record.before == 10.0F && record.after == 10.0F,
		"wrapper captures pre/post float values without changing them");

	g_seen.calls = 0;
	value = 1.0F;
	g_mutate.store(true, std::memory_order_relaxed);
	const auto modifiedResult = ObservationWrapper(0x23, second, third, fourth, &value);
	g_mutate.store(false, std::memory_order_relaxed);
	Expect(modifiedResult == 0xD00DFEEDCAFEBEEFULL && value == 4.25F && g_seen.calls == 1,
		"original mutation is observed and wrapper adds no second write");
	Expect(trace.TryPop(record) == TraceRing::PopResult::Item && record.before == 1.0F && record.after == 4.25F,
		"record contains the original's before/after values");

	g_seen.calls = 0;
	value = 3.0F;
	g_reenter.store(true, std::memory_order_relaxed);
	(void)ObservationWrapper(0x23, second, third, fourth, &value);
	Expect(g_seen.calls == 2 && g_nestedResult == 0xD00DFEEDCAFEBEEFULL,
		"recursive/reentrant wrapper forwards each invocation once");
	(void)trace.TryPop(record);
	(void)trace.TryPop(record);

	TraceRing fullTrace;
	CallRecord filler{ 0, 0.0F, 0.0F, 0 };
	for (int i = 0; i < 8; ++i) Expect(fullTrace.TryPush(filler), "fill bounded observer ring");
	g_trace = &fullTrace;
	g_seen.calls = 0;
	value = 7.0F;
	(void)ObservationWrapper(0x24, second, third, fourth, &value);
	Expect(g_seen.calls == 1 && fullTrace.Dropped() == 1 && fullTrace.Overflowed() == 1,
		"log overflow drops the record and still calls the original once");
	g_trace = &trace;

	g_seen.calls = 0;
	g_throw.store(true, std::memory_order_relaxed);
	bool caught = false;
	try { (void)ObservationWrapper(0x24, second, third, fourth, &value); }
	catch (const std::runtime_error&) { caught = true; }
	g_throw.store(false, std::memory_order_relaxed);
	Expect(caught && g_seen.calls == 1, "original exception unwinds once without retry or second call");

	value = 9.0F;
	g_asm_wrapper_return = 0;
	g_asm_register_preservation_ok = 0;
	const auto preserved = AbiCallWithNonvolatileSentinels(0x24, second, third, fourth, &value);
	Expect(preserved == 1 && g_asm_register_preservation_ok == 1,
		"MASM caller confirms RBX/RBP/RSI/RDI/R12-R15 survive wrapper call");
	Expect(g_asm_wrapper_return == 0xD00DFEEDCAFEBEEFULL,
		"MASM caller confirms wrapper return value survives the ABI boundary");

	return g_failed == 0 ? 0 : 1;
}
