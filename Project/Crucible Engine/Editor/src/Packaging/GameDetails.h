/*!
* @file			GameDetails.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			26 November 2026
*
* @brief		The game's name, version, publisher, install folder and AppId:
*				Assets/Config/GameInfo.json, read and written for the Package Game window.
*
* The top-level CMakeLists.txt reads the same file and turns it into the .exe's name, the zip's
* name, GameInfo.h (C++) and GameInfo.iss (the installer). The file is a CMake configure
* dependency, so a build after a Save (Package Game starts one) picks the new values up.
*
* Check() uses the same rules as CMakeLists.txt: keep the two in step.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <filesystem>
#include <string>

namespace Crucible {

	struct GameDetails {
		std::string name;            ///< "Lithomachia": the .exe, window title, install and AppData folders
		std::string version;         ///< "0.1.0": major.minor.patch
		std::string publisher;       ///< "DigiPen Institute of Technology": the installer and Windows' app list
		std::string installFolder;   ///< "DigiPen": Program Files\<installFolder>\<name> (may be empty)
		std::string appId;           ///< the installer's AppId, without braces - never changes

		bool operator==(const GameDetails&) const = default;

		/// Assets/Config/GameInfo.json under this Assets folder.
		static std::filesystem::path File(const std::filesystem::path& assetRoot);

		/// Reads the file. False (and why) if it is missing or not JSON.
		static bool Load(const std::filesystem::path& file, GameDetails& out, std::string& error);

		/// Writes these values into the file, keeping any other keys in it. False (and why) on failure.
		bool Save(const std::filesystem::path& file, std::string& error) const;

		/// What is wrong with each field ("" = fine). Same rules as the top-level CMakeLists.txt.
		static std::string CheckName(const std::string& name);
		static std::string CheckVersion(const std::string& version);
		static std::string CheckPublisher(const std::string& publisher);
		static std::string CheckInstallFolder(const std::string& folder);
		static std::string CheckAppId(const std::string& appId);

		/// True if every field is fine.
		bool IsValid() const;
	};

}
