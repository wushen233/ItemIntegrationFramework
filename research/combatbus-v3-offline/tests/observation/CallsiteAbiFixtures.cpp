#include "MpscObservationRing.h"

#include <Windows.h>
#include <intrin.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_M_X64)
#error This fixture intentionally validates only the Win64 ABI.
#endif

std::uint64_t __fastcall ObservationWrapper(std::uint32_t, void*, void*, void*, float*);
extern "C" std::uint64_t g_asm_wrapper_return;
extern "C" std::uint32_t g_asm_register_preservation_ok;
extern "C" std::uint32_t g_asm_xmm_restore_ok;
extern "C" std::uint32_t g_asm_corruption_mode;
extern "C" std::uint32_t g_asm_last_xmm_mask;
extern "C" std::uint32_t g_asm_outer_gpr_restore_ok;
extern "C" std::uint32_t __fastcall AbiCallWithNonvolatileSentinels(std::uint32_t, void*, void*, void*, float*);
extern "C" std::uint32_t __fastcall AbiCallProbeAndVerifyGprs(std::uint32_t, void*, void*, void*, float*);
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

	bool VerifyRegisterFailureBranches(const char* sourcePath)
	{
		std::ifstream source(sourcePath);
		if (!source) return false;
		auto compact = [](std::string line) {
			if (const auto comment = line.find(';'); comment != std::string::npos) line.resize(comment);
			std::string result;
			for (const unsigned char ch : line) {
				if (!std::isspace(ch)) result.push_back(static_cast<char>(std::tolower(ch)));
			}
			return result;
		};
		std::array<bool, 10> routed{};
		std::vector<std::string> lines;
		for (std::string line; std::getline(source, line);) lines.push_back(compact(std::move(line)));

		std::size_t checks = 0;
		std::size_t gprBranches = 0;
		for (std::size_t i = 0; i < lines.size(); ++i) {
			if (lines[i] != "abi_probe_check:") continue;
			for (std::size_t j = i + 1; j < lines.size(); ++j) {
				if (lines[j].starts_with("pcmpeqbxmm6,")) break;
				if (!lines[j].starts_with("jne")) continue;
				++gprBranches;
				if (lines[j] != "jneabi_probe_failed") {
					std::fprintf(stderr, "Static source check found a GPR mismatch branch [%s].\n", lines[j].c_str());
					return false;
				}
			}
			break;
		}
		if (gprBranches != 8) {
			std::fprintf(stderr, "Static source check found %zu GPR mismatch branches; expected 8.\n", gprBranches);
			return false;
		}

		for (std::size_t i = 0; i < lines.size(); ++i) {
			int reg = -1;
			for (int candidate = 6; candidate <= 15; ++candidate) {
				const auto token = "pcmpeqb" + std::string("xmm") + std::to_string(candidate) + ",";
				if (lines[i].starts_with(token)) {
					reg = candidate;
					break;
				}
			}
			if (reg == -1) continue;
			if (routed[static_cast<std::size_t>(reg - 6)]) {
				std::fprintf(stderr, "Static source check found duplicate XMM%d comparison.\n", reg);
				return false;
			}
			++checks;

			bool foundMaskCheck = false;
			for (std::size_t j = i + 1; j < lines.size(); ++j) {
				if (lines[j].empty()) continue;
				if (!foundMaskCheck) {
					if (lines[j] == "cmpeax,0ffffh") foundMaskCheck = true;
					continue;
				}
				routed[static_cast<std::size_t>(reg - 6)] = lines[j] == "jneabi_probe_failed";
				if (!routed[static_cast<std::size_t>(reg - 6)])
					std::fprintf(stderr, "XMM%d branch text was [%s].\n", reg, lines[j].c_str());
				break;
			}
			if (!routed[static_cast<std::size_t>(reg - 6)]) {
				std::fprintf(stderr, "Static source check: XMM%d mismatch is not routed to abi_probe_failed.\n", reg);
				return false;
			}
		}
		const bool complete = checks == routed.size() &&
			std::all_of(routed.begin(), routed.end(), [](bool value) { return value; });
		if (!complete) std::fprintf(stderr, "Static source check found %zu XMM comparisons; expected %zu.\n", checks, routed.size());
		return complete;
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
extern "C" std::uint32_t g_asm_xmm_restore_ok = 0;
extern "C" std::uint32_t g_asm_corruption_mode = 0;
extern "C" std::uint32_t g_asm_last_xmm_mask = 0;
extern "C" std::uint32_t g_asm_outer_gpr_restore_ok = 0;

int main(int argc, char** argv)
{
	if (argc == 2) {
		Expect(VerifyRegisterFailureBranches(argv[1]),
			"static check: all GPR and XMM6-XMM15 mismatches branch to the explicit failure result");
	} else if (argc != 1) {
		Expect(false, "ABI fixture accepts at most one MASM source path");
	}

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
	g_asm_xmm_restore_ok = 0;
	g_seen.calls = 0;
	g_asm_outer_gpr_restore_ok = 0;
	Expect(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&AbiCallWithNonvolatileSentinels), &imageBase, nullptr) != nullptr,
		"MASM caller exposes unwind metadata for its saved GPR and XMM state");
	Expect(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&AbiCallProbeAndVerifyGprs), &imageBase, nullptr) != nullptr,
		"outer MASM caller exposes unwind metadata while verifying caller GPR preservation");
	const auto preserved = AbiCallProbeAndVerifyGprs(0x24, second, third, fourth, &value);
	Expect(preserved == 1 && g_asm_register_preservation_ok == 1 && g_asm_xmm_restore_ok == 1,
		"MASM caller confirms RBX/RBP/RSI/RDI/R12-R15 and XMM6-XMM15 low 128 bits survive wrapper call");
	Expect(g_asm_outer_gpr_restore_ok == 1,
		"outer MASM caller confirms all nonvolatile GPR sentinels survive the inner ABI probe");
	Expect(g_asm_wrapper_return == 0xD00DFEEDCAFEBEEFULL,
		"MASM caller confirms wrapper return value survives the ABI boundary");
	Expect(g_seen.calls == 1,
		"MASM positive probe calls the wrapper exactly once");

	for (const auto [mode, label] : std::array<std::pair<std::uint32_t, const char*>, 3>{
		     std::pair{ 1u, "GPR compare failure returns explicit rejection" },
		     std::pair{ 2u, "XMM6 compare failure returns explicit rejection" },
		     std::pair{ 3u, "XMM14 compare failure returns explicit rejection" } }) {
		g_seen.calls = 0;
		g_asm_register_preservation_ok = 0xFFFFFFFFu;
		g_asm_xmm_restore_ok = 0;
		g_asm_wrapper_return = 0;
		g_asm_last_xmm_mask = 0;
		g_asm_outer_gpr_restore_ok = 0;
		g_asm_corruption_mode = mode;
		const auto rejected = AbiCallProbeAndVerifyGprs(0x24, second, third, fourth, &value);
		Expect(rejected == 0 && g_asm_register_preservation_ok == 0,
			label);
		Expect(g_asm_xmm_restore_ok == 1,
			"negative MASM probe restores caller XMM6-XMM15 state");
		Expect(g_asm_outer_gpr_restore_ok == 1,
			"negative MASM probe does not leak corrupted nonvolatile GPR state to its caller");
		Expect(g_seen.calls == 1 && g_asm_wrapper_return == 0xD00DFEEDCAFEBEEFULL,
			"negative MASM probe still forwards exactly one call and preserves its return value");
		if (mode == 2) {
			Expect(g_asm_last_xmm_mask == 1,
				"XMM6 partial equality mask of exactly 1 is rejected, not treated as PASS");
		}
		if (mode == 3) {
			Expect(g_asm_last_xmm_mask == 1,
				"XMM14 partial equality mask of exactly 1 is rejected, not treated as PASS");
		}
	}
	g_asm_corruption_mode = 0;

	return g_failed == 0 ? 0 : 1;
}
