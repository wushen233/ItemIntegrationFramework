#pragma once

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace observation::offline
{
	inline constexpr std::size_t kCallLength = 5;
	using CallBytes = std::array<std::uint8_t, kCallLength>;

	struct DecodedCall
	{
		std::uint64_t target{};
		CallBytes bytes{};
	};

	[[nodiscard]] inline std::optional<DecodedCall> DecodeCallRel32(
		std::span<const std::uint8_t> bytes, std::uint64_t instructionAddress) noexcept
	{
		if (bytes.size() < kCallLength || bytes[0] != 0xE8) {
			return std::nullopt;
		}

		std::uint32_t raw{};
		for (std::size_t i = 0; i < sizeof(raw); ++i) {
			raw |= static_cast<std::uint32_t>(bytes[i + 1]) << (i * 8);
		}
		const auto displacement = std::bit_cast<std::int32_t>(raw);
		const auto next = instructionAddress + kCallLength;
		const auto target = static_cast<std::uint64_t>(static_cast<std::int64_t>(next) + displacement);
		return DecodedCall{ target, { bytes[0], bytes[1], bytes[2], bytes[3], bytes[4] } };
	}

	[[nodiscard]] inline std::optional<CallBytes> EncodeCallRel32(
		std::uint64_t instructionAddress, std::uint64_t target) noexcept
	{
		const auto next = instructionAddress + kCallLength;
		const auto difference = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next);
		if (difference < INT32_MIN || difference > INT32_MAX) {
			return std::nullopt;
		}
		const auto displacement = static_cast<std::uint32_t>(static_cast<std::int32_t>(difference));
		return CallBytes{ 0xE8,
			static_cast<std::uint8_t>(displacement),
			static_cast<std::uint8_t>(displacement >> 8),
			static_cast<std::uint8_t>(displacement >> 16),
			static_cast<std::uint8_t>(displacement >> 24) };
	}

	struct PatchSite
	{
		std::size_t offset{};
		std::uint64_t virtualAddress{};
		std::uint64_t expectedTarget{};
		std::uint64_t replacementTarget{};
		CallBytes expectedBytes{};
	};

	enum class PatchResult
	{
		Applied,
		PreflightRejected,
		WriteFailedRolledBack,
		RecoveryRequired
	};

	struct PatchOptions
	{
		std::size_t failWriteAt{ static_cast<std::size_t>(-1) };
		bool failRollback{};
		std::size_t failByteAt{ static_cast<std::size_t>(-1) };
	};

	[[nodiscard]] inline PatchResult ApplyCallPlan(std::span<std::uint8_t> image,
		std::span<const PatchSite> sites, PatchOptions options = {})
	{
		if (sites.empty()) {
			return PatchResult::PreflightRejected;
		}
		for (std::size_t i = 0; i < sites.size(); ++i) {
			for (std::size_t j = 0; j < i; ++j) {
				const auto& site = sites[i];
				const auto& prior = sites[j];
				if (site.offset < prior.offset + kCallLength && prior.offset < site.offset + kCallLength) {
					return PatchResult::PreflightRejected;
				}
			}
		}

		// Validate every site before writing any byte.
		for (std::size_t i = 0; i < sites.size(); ++i) {
			const auto& site = sites[i];
			if (site.offset > image.size() || image.size() - site.offset < kCallLength) {
				return PatchResult::PreflightRejected;
			}
			const auto current = std::span<const std::uint8_t>(image.data() + site.offset, kCallLength);
			const auto decoded = DecodeCallRel32(current, site.virtualAddress);
			if (!decoded || decoded->bytes != site.expectedBytes || decoded->target != site.expectedTarget ||
				!EncodeCallRel32(site.virtualAddress, site.replacementTarget)) {
				return PatchResult::PreflightRejected;
			}
		}

		std::size_t writtenSites{};
		std::size_t writtenBytes{};
		bool writeFailed = false;
		for (; writtenSites < sites.size() && !writeFailed; ++writtenSites) {
			if (writtenSites == options.failWriteAt) {
				writeFailed = true;
				break;
			}
			const auto replacement = EncodeCallRel32(sites[writtenSites].virtualAddress, sites[writtenSites].replacementTarget);
			if (!replacement) {
				writeFailed = true;
				break;
			}
			for (std::size_t byte = 0; byte < kCallLength; ++byte) {
				if (writtenBytes == options.failByteAt) {
					writeFailed = true;
					break;
				}
				image[sites[writtenSites].offset + byte] = (*replacement)[byte];
				++writtenBytes;
			}
		}
		if (!writeFailed && writtenSites == sites.size()) {
			return PatchResult::Applied;
		}

		if (options.failRollback) {
			return PatchResult::RecoveryRequired;
		}
		for (const auto& site : sites) {
			const auto bytesAtSite = writtenBytes < kCallLength ? writtenBytes : kCallLength;
			for (std::size_t byte = 0; byte < bytesAtSite; ++byte) {
				image[site.offset + byte] = site.expectedBytes[byte];
			}
			writtenBytes -= bytesAtSite;
			if (writtenBytes == 0) break;
		}
		return PatchResult::WriteFailedRolledBack;
	}
}
