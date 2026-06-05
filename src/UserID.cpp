#include "UserID.h"
#include <random>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace FalloutChat
{
    UserID& UserID::GetSingleton()
    {
        static UserID singleton;
        return singleton;
    }

    std::string UserID::GenerateRandomID()
    {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        uint64_t randomValue = dis(gen);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << randomValue;
        return oss.str();
    }

    std::string UserID::GetAppDataPath()
    {
        char appDataPath[MAX_PATH];
        HRESULT hr = SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appDataPath);

        if (FAILED(hr)) {
            spdlog::error("UserID: Failed to get AppData path (hr={:x})", static_cast<uint32_t>(hr));
            return "";
        }

        std::string path(appDataPath);
        path += "\\FalloutChat";

        // Ensure directory exists
        CreateDirectoryA(path.c_str(), nullptr);  // Silently succeeds if already exists

        return path;
    }

    bool UserID::LoadIDFile()
    {
        std::string appDataPath = GetAppDataPath();
        if (appDataPath.empty()) {
            spdlog::error("UserID: Could not determine AppData path");
            return false;
        }

        std::string filePath = appDataPath + "\\user_id.json";
        std::ifstream file(filePath);

        if (!file.is_open()) {
            spdlog::info("UserID: No existing ID file found at {}", filePath);
            return false;
        }

        try {
            nlohmann::json data = nlohmann::json::parse(file);
            file.close();

            _id = data.value("id", "");
            _username = data.value("username", "Player");
            _privacyAccepted = data.value("privacyAccepted", false);
            _tutorialSeen = data.value("tutorialSeen", false);
            _introDismissed = data.value("introDismissed", false);
            _chatEnabled = data.value("chatEnabled", true);
            _fontSize = data.value("fontSize", 14);
            _bgOpacity = data.value("bgOpacity", 60);

            if (_id.empty()) {
                spdlog::warn("UserID: ID file exists but ID is empty, regenerating");
                return false;
            }

            spdlog::info("UserID: Loaded existing ID: {} (username: {})", _id, _username);
            return true;
        }
        catch (const std::exception& e) {
            spdlog::error("UserID: Failed to parse ID file: {}", e.what());
            file.close();
            return false;
        }
    }

    void UserID::CreateIDFile()
    {
        _id = GenerateRandomID();
        _username = "Player";
        _privacyAccepted = false;
        _tutorialSeen = false;
        _introDismissed = false;
        _chatEnabled = true;
        _fontSize = 14;
        _bgOpacity = 60;

        SaveIDFile();

        spdlog::info("UserID: Generated new ID: {} at first launch", _id);
    }

    void UserID::SaveIDFile()
    {
        std::string appDataPath = GetAppDataPath();
        if (appDataPath.empty()) {
            spdlog::error("UserID: Could not determine AppData path for save");
            return;
        }

        std::string filePath = appDataPath + "\\user_id.json";

        try {
            nlohmann::json data;
            data["id"] = _id;
            data["username"] = _username;
            data["privacyAccepted"] = _privacyAccepted;
            data["tutorialSeen"] = _tutorialSeen;
            data["introDismissed"] = _introDismissed;
            data["chatEnabled"] = _chatEnabled;
            data["fontSize"] = _fontSize;
            data["bgOpacity"] = _bgOpacity;
            data["created"] = std::time(nullptr);  // Unix timestamp

            std::ofstream file(filePath);
            if (!file.is_open()) {
                spdlog::error("UserID: Failed to open file for writing: {}", filePath);
                return;
            }

            file << data.dump(2);  // Pretty-print with 2-space indent
            if (!file.good()) {
                spdlog::error("UserID: Write failed for file: {}", filePath);
                file.close();
                return;
            }

            file.close();
            spdlog::info("UserID: Saved ID file to {}", filePath);
        }
        catch (const std::exception& e) {
            spdlog::error("UserID: Failed to save ID file: {}", e.what());
        }
    }

    void UserID::Initialize()
    {
        if (!LoadIDFile()) {
            CreateIDFile();
        }
    }

    void UserID::SetUsername(const std::string& newUsername)
    {
        if (newUsername.empty()) {
            spdlog::warn("UserID: Attempted to set empty username, ignoring");
            return;
        }

        if (newUsername.length() > 32) {
            spdlog::warn("UserID: Username too long ({}), truncating to 32 chars", newUsername.length());
            _username = newUsername.substr(0, 32);
        } else {
            _username = newUsername;
        }

        SaveIDFile();
        spdlog::info("UserID: Username updated to: {}", _username);
    }

    void UserID::RegenerateID()
    {
        std::string oldID = _id;
        _id = GenerateRandomID();
        SaveIDFile();
        spdlog::warn("UserID: ID regenerated (was: {}, now: {})", oldID, _id);
    }

    void UserID::SetPrivacyAccepted(bool accepted)
    {
        _privacyAccepted = accepted;
        SaveIDFile();
    }

    void UserID::SetTutorialSeen(bool seen)
    {
        _tutorialSeen = seen;
        SaveIDFile();
    }

    void UserID::SetIntroDismissed(bool dismissed)
    {
        _introDismissed = dismissed;
        SaveIDFile();
    }

    void UserID::SetChatEnabled(bool enabled)
    {
        _chatEnabled = enabled;
        SaveIDFile();
    }

    void UserID::SetFontSize(int size)
    {
        if (size < 10 || size > 20) {
            spdlog::warn("UserID: SetFontSize size {} out of range [10, 20], ignoring", size);
            return;
        }
        _fontSize = size;
        SaveIDFile();
    }

    void UserID::SetBgOpacity(int opacity)
    {
        if (opacity < 10 || opacity > 100) {
            spdlog::warn("UserID: SetBgOpacity opacity {} out of range [10, 100], ignoring", opacity);
            return;
        }
        _bgOpacity = opacity;
        SaveIDFile();
    }
}
