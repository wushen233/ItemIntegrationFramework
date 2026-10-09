#include "CallsiteModel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>

using namespace observation::offline;

namespace
{
	std::uint32_t g_failed{};
	void Check(bool condition, const char* label)
	{
		std::printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
		if (!condition) ++g_failed;
	}

	PatchSite MakeSite(std::vector<std::uint8_t>& image, std::size_t offset, std::uint64_t va,
		std::uint64_t target, std::uint64_t replacement)
	{
		const auto encoded = EncodeCallRel32(va, target);
		if (!encoded) std::abort();
		std::copy(encoded->begin(), encoded->end(), image.begin() + static_cast<std::ptrdiff_t>(offset));
		return PatchSite{ offset, va, target, replacement, *encoded };
	}
}

int main()
{
	constexpr std::uint64_t base = 0x0000000140001000ull;
	std::vector<std::uint8_t> image(96, 0xCC);
	const auto first = MakeSite(image, 8, base + 8, base + 0x500, base + 0x700);
	const auto second = MakeSite(image, 32, base + 32, base - 0x200, base + 0x780);
	const auto decoded = DecodeCallRel32(std::span<const std::uint8_t>(image).subspan(8, 5), base + 8);
	Check(decoded && decoded->target == base + 0x500, "E8 rel32 decoding resolves a positive target");
	const auto negative = DecodeCallRel32(std::span<const std::uint8_t>(image).subspan(32, 5), base + 32);
	Check(negative && negative->target == base - 0x200, "signed rel32 decoding resolves a negative target");
	Check(EncodeCallRel32(base + 8, base + 0x700).has_value(), "in-range replacement displacement encodes");
	Check(!EncodeCallRel32(base, base + 5 + static_cast<std::uint64_t>(INT32_MAX) + 1),
		"replacement target beyond positive rel32 range is rejected");
	Check(!EncodeCallRel32(base, base - static_cast<std::uint64_t>(INT32_MAX) - 2),
		"replacement target beyond negative rel32 range is rejected");
	const std::array<std::uint8_t, 4> shortInstruction{ 0xE8, 0, 0, 0 };
	Check(!DecodeCallRel32(shortInstruction, base), "truncated call bytes are rejected");
	auto wrongOpcode = std::array<std::uint8_t, 5>{ 0xE9, 0, 0, 0, 0 };
	Check(!DecodeCallRel32(wrongOpcode, base), "non-CALL opcode is rejected");
	Check(image[7] == 0xCC && image[13] == 0xCC && image[14] == 0xCC,
		"CALL decoder leaves adjacent instruction bytes outside its five-byte extent");

	const auto before = image;
	const std::array<PatchSite, 2> sites{ first, second };
	Check(ApplyCallPlan(image, sites) == PatchResult::Applied, "all callsites pass preflight and patch in fixture memory");
	Check(image[7] == before[7] && image[13] == before[13] && image[14] == before[14],
		"patch writes exactly five bytes and preserves neighboring bytes");
	const auto patched1 = DecodeCallRel32(std::span<const std::uint8_t>(image).subspan(8, 5), base + 8);
	const auto patched2 = DecodeCallRel32(std::span<const std::uint8_t>(image).subspan(32, 5), base + 32);
	Check(patched1 && patched1->target == first.replacementTarget && patched2 && patched2->target == second.replacementTarget,
		"patched rel32 targets resolve to their expected replacements");

	image = before;
	auto badSecond = second;
	badSecond.expectedBytes[0] = 0xE9;
	const std::array<PatchSite, 2> badPlan{ first, badSecond };
	Check(ApplyCallPlan(image, badPlan) == PatchResult::PreflightRejected && image == before,
		"one signature mismatch rejects the entire multi-site plan before any write");

	image = before;
	image[32] ^= 0x01;
	const auto modified = image;
	Check(ApplyCallPlan(image, sites) == PatchResult::PreflightRejected && image == modified,
		"third-party-modified call bytes fail closed without changing any fixture byte");

	image = before;
	const auto overlap = std::array<PatchSite, 2>{ first, PatchSite{ 12, base + 12, 0, base + 0x700, {} } };
	Check(ApplyCallPlan(image, overlap) == PatchResult::PreflightRejected && image == before,
		"overlapping patch ranges are rejected before writing");

	image = before;
	Check(ApplyCallPlan(image, sites, PatchOptions{ 1, false }) == PatchResult::WriteFailedRolledBack && image == before,
		"simulated second-write failure restores the first site's original bytes");
	image = before;
	Check(ApplyCallPlan(image, sites, PatchOptions{ 1, true }) == PatchResult::RecoveryRequired &&
		image != before && image[32] == before[32],
		"failed rollback reports recovery required and never claims clean restoration");
	image = before;
	Check(ApplyCallPlan(image, sites, PatchOptions{ static_cast<std::size_t>(-1), false, 3 }) ==
		PatchResult::WriteFailedRolledBack && image == before,
		"partial five-byte write failure restores every changed fixture byte");
	image = before;
	Check(ApplyCallPlan(image, sites, PatchOptions{ static_cast<std::size_t>(-1), true, 3 }) ==
		PatchResult::RecoveryRequired && image != before,
		"partial write with failed recovery explicitly returns RecoveryRequired");

	return g_failed == 0 ? 0 : 1;
}
