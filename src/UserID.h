#pragma once

#include <string>
#include <cstdint>

namespace FalloutChat
{
    class UserID
    {
    public:
        static UserID& GetSingleton();

        // Initialize: load or create ID file
        void Initialize();

        // Get the persistent hex64 ID
        std::string GetID() const { return _id; }

        // Get current username
        std::string GetUsername() const { return _username; }

        // Update username in memory and file
        void SetUsername(const std::string& newUsername);

        // Force regenerate ID (rarely needed, logs warning)
        void RegenerateID();

    private:
        UserID() = default;
        ~UserID() = default;

        UserID(const UserID&) = delete;
        UserID& operator=(const UserID&) = delete;

        // Generate random hex64 string
        static std::string GenerateRandomID();

        // Get AppData path: %APPDATA%\Local\FalloutChat\
        static std::string GetAppDataPath();

        // Load ID from file, return true if exists
        bool LoadIDFile();

        // Create new ID and write to file
        void CreateIDFile();

        // Save current ID and username to file
        void SaveIDFile();

        std::string _id;        // e.g., "a1b2c3d4e5f6g7h8"
        std::string _username;  // Default or user-set
    };
}
