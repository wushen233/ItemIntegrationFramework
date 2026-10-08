#pragma once

#include <windows.h>

#include <cstring>

namespace combatbus_test {
	inline bool WriteStartupMarker(const char* marker, bool reset = false)
	{
		wchar_t path[32768]{};
		const DWORD length = GetEnvironmentVariableW(L"IIF_COMBATBUS_PHASE1V_MARKER", path,
			static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
		if (length == 0) return true;
		if (length >= sizeof(path) / sizeof(path[0])) return false;

		const DWORD access = reset ? GENERIC_WRITE : FILE_APPEND_DATA;
		const DWORD disposition = reset ? CREATE_ALWAYS : OPEN_ALWAYS;
		HANDLE file = CreateFileW(path, access, FILE_SHARE_READ, nullptr, disposition,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;

		DWORD written{};
		const DWORD markerLength = static_cast<DWORD>(std::strlen(marker));
		const bool wroteMarker = WriteFile(file, marker, markerLength, &written, nullptr) != FALSE &&
			written == markerLength;
		const char newline[] = "\r\n";
		const bool wroteNewline = WriteFile(file, newline, 2, &written, nullptr) != FALSE && written == 2;
		const bool flushed = FlushFileBuffers(file) != FALSE;
		CloseHandle(file);
		return wroteMarker && wroteNewline && flushed;
	}
}
