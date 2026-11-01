/*************************************************************************
* \file:         ScriptInterop.cpp
* \author:       <author>
* \par:          Email: <email>
* \date          30 October 2026
* \version       1.0
* \brief         Implementation - see ScriptInterop.h.
*
**************************************************************************/
#include "Scripting/ScriptInterop.h"
#if CRUCIBLE_WITH_MONO
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <limits>

namespace Crucible::ScriptingDetail
{
	thread_local unsigned managedCallDepth = 0;

	bool ToUtf16(const std::string& value, std::wstring& wide)
	{
		wide.clear();
		if (value.empty())
			return true;
		if (value.size() > size_t(std::numeric_limits<int>::max()))
			return false;
		const int count =
		    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
		if (!count)
			return false;
		wide.resize(count);
		return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()),
		                           wide.data(), count) == count;
	}

	bool FromManagedString(MonoString* value, std::string& text)
	{
		text.clear();
		const int length = mono_string_length(value);
		if (!length)
			return true;
		const auto* chars = reinterpret_cast<const wchar_t*>(mono_string_chars(value));
		const int count =
		    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, nullptr, 0, nullptr, nullptr);
		if (!count)
			return false;
		text.resize(count);
		return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, text.data(), count, nullptr,
		                           nullptr) == count;
	}
}
#endif
