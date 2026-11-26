/*!
* @file			GameDetails.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			26 November 2026
*
* @brief		Implementation - see GameDetails.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Packaging/GameDetails.h"

#include "Json/JsonWrapper.h"

#include <system_error>

namespace fs = std::filesystem;

namespace Crucible {

	namespace {

		bool IsAsciiAlnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

		// The rule for the name and the install folder: they become file and folder names.
		// Letters, digits, spaces, '.', '-', '_'; starts with a letter or digit; does not end with a
		// space or '.' (Windows drops those from folder names).
		bool IsSafeFileName(std::string const& text)
		{
			if (text.empty() || text.size() > 64 || !IsAsciiAlnum(text.front()))
				return false;
			for (char const c : text) {
				if (!IsAsciiAlnum(c) && c != ' ' && c != '.' && c != '-' && c != '_')
					return false;
			}
			return text.back() != ' ' && text.back() != '.';
		}

	}	// anonymous namespace

	fs::path GameDetails::File(fs::path const& assetRoot)
	{
		return assetRoot / "Config" / "GameInfo.json";
	}

	bool GameDetails::Load(fs::path const& file, GameDetails& out, std::string& error)
	{
		std::error_code ec;
		if (!fs::is_regular_file(file, ec)) {
			error = "Assets/Config/GameInfo.json is missing";
			return false;
		}
		JsonWrapper doc;
		if (!doc.ReadFromFile(file)) {
			error = "Assets/Config/GameInfo.json could not be read (is it valid JSON?)";
			return false;
		}
		out.name          = doc.Get<std::string>("name");
		out.version       = doc.Get<std::string>("version");
		out.publisher     = doc.Get<std::string>("publisher");
		out.installFolder = doc.Get<std::string>("installFolder");
		out.appId         = doc.Get<std::string>("appId");
		return true;
	}

	bool GameDetails::Save(fs::path const& file, std::string& error) const
	{
		if (!IsValid()) {
			error = "some details are not valid";
			return false;
		}
		// Keep any other keys someone added to the file.
		JsonWrapper doc;
		std::error_code ec;
		if (!fs::exists(file, ec) || !doc.ReadFromFile(file))
			doc.CreateEmpty();
		doc.Set("name", name);
		doc.Set("version", version);
		doc.Set("publisher", publisher);
		doc.Set("installFolder", installFolder);
		doc.Set("appId", appId);
		if (!doc.SaveToFile(file)) {
			error = "Assets/Config/GameInfo.json could not be written";
			return false;
		}
		return true;
	}

	std::string GameDetails::CheckName(std::string const& value)
	{
		if (value.empty())
			return "needed";
		return IsSafeFileName(value) ? std::string{}
			: "letters, digits, spaces, '.', '-' and '_'; starts with a letter or digit";
	}

	std::string GameDetails::CheckVersion(std::string const& value)
	{
		// Three numbers: GameInfo.h splits it for Vulkan's VK_MAKE_VERSION.
		int parts = 1, digits = 0;
		for (char const c : value) {
			if (c == '.') {
				if (digits == 0) return "three numbers, like 0.2.0";
				++parts;
				digits = 0;
			}
			else if (c >= '0' && c <= '9') {
				if (++digits > 9) return "each number at most 9 digits";
			}
			else {
				return "three numbers, like 0.2.0";
			}
		}
		return (parts == 3 && digits > 0) ? std::string{} : "three numbers, like 0.2.0";
	}

	std::string GameDetails::CheckPublisher(std::string const& value)
	{
		if (value.empty())
			return "needed";
		if (value.size() > 128)
			return "at most 128 characters";
		for (char const c : value) {
			// Goes into C++ strings and the installer script, where these would break them.
			if (c == '"' || c == '\\' || c == '{' || c == '}' || static_cast<unsigned char>(c) < 0x20)
				return "no \" \\ { }";
		}
		return {};
	}

	std::string GameDetails::CheckInstallFolder(std::string const& value)
	{
		if (value.empty())
			return {};   // allowed: the game goes straight into Program Files\<name>
		return IsSafeFileName(value) ? std::string{}
			: "one folder name: letters, digits, spaces, '.', '-' and '_'";
	}

	std::string GameDetails::CheckAppId(std::string const& value)
	{
		if (value.size() != 36)
			return "not a GUID";
		for (std::size_t i = 0; i < value.size(); ++i) {
			char const c = value[i];
			bool const dash = (i == 8 || i == 13 || i == 18 || i == 23);
			bool const ok = dash ? c == '-' : ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
			if (!ok)
				return "not a GUID in capitals";
		}
		return {};
	}

	bool GameDetails::IsValid() const
	{
		return CheckName(name).empty() && CheckVersion(version).empty() && CheckPublisher(publisher).empty()
		    && CheckInstallFolder(installFolder).empty() && CheckAppId(appId).empty();
	}

}
