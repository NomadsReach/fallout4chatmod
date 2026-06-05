#include "PCH.h"
#include "ChatUI.h"
#include "ChatClient.h"
#include "UserID.h"
#include <SimpleIni.h>

CSimpleIniA ini(true, false, false);
static std::mutex g_iniMutex;
std::string serverUrl = "wss://chat.fallenworld.nexus/ws";
std::string username = "Player";
bool g_privacyAccepted = false;
bool g_chatEnabled = true;
bool g_tutorialSeen = false;
bool g_introDismissed = false;
int g_fontSize = 14;
int g_bgOpacity = 60;

void SaveUsername(const std::string& newName)
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	username = newName;
	FalloutChat::UserID::GetSingleton().SetUsername(newName);
}

void SavePrivacyPolicy()
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_privacyAccepted = true;
	FalloutChat::UserID::GetSingleton().SetPrivacyAccepted(true);
}

void SaveChatEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_chatEnabled = enabled;
	FalloutChat::UserID::GetSingleton().SetChatEnabled(enabled);
}

void SaveTutorialSeen()
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_tutorialSeen = true;
	FalloutChat::UserID::GetSingleton().SetTutorialSeen(true);
}

void SaveIntroDismissed()
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_introDismissed = true;
	FalloutChat::UserID::GetSingleton().SetIntroDismissed(true);
}

void SaveFontSize(int size)
{
	if (size < 10 || size > 20) {
		logger::warn("SaveFontSize: size {} out of range [10, 20], ignoring", size);
		return;
	}
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_fontSize = size;
	FalloutChat::UserID::GetSingleton().SetFontSize(size);
}

void SaveOpacity(int opacity)
{
	if (opacity < 10 || opacity > 100) {
		logger::warn("SaveOpacity: opacity {} out of range [10, 100], ignoring", opacity);
		return;
	}
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_bgOpacity = opacity;
	FalloutChat::UserID::GetSingleton().SetBgOpacity(opacity);
}

void LoadConfigs()
{
	logger::info("LoadConfigs: reading preferences from UserID");
	std::lock_guard<std::mutex> lock(g_iniMutex);

	// Server URL is hardcoded — not user-configurable to prevent redirection attacks
	serverUrl = "wss://chat.fallenworld.nexus/ws";
	// All user preferences are now persisted in %APPDATA%\Local\FalloutChat\user_id.json
	// Default values will be used until UserID::Initialize() is called at kGameDataReady
	username = "Player";
	g_privacyAccepted = false;
	g_chatEnabled = true;
	g_tutorialSeen = false;
	g_introDismissed = false;
	g_fontSize = 14;
	g_bgOpacity = 60;

	logger::info("LoadConfigs: defaults set, will sync from UserID at kGameDataReady");
}



extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .logName = "FalloutChat" });
	logger::info("{} v{}", Version::PROJECT, Version::NAME);
	LoadConfigs();

	const F4SE::MessagingInterface* messageInterface = F4SE::GetMessagingInterface();
	messageInterface->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
			logger::info("F4SE: kGameDataReady — initializing chat");
			FalloutChat::ChatUI::Initialize();

			FalloutChat::UserID::GetSingleton().Initialize();
			auto& userID = FalloutChat::UserID::GetSingleton();

			// Sync all persisted preferences from UserID into globals
			std::string userIDValue = userID.GetID();
			username = userID.GetUsername();
			g_privacyAccepted = userID.GetPrivacyAccepted();
			g_chatEnabled = userID.GetChatEnabled();
			g_tutorialSeen = userID.GetTutorialSeen();
			g_introDismissed = userID.GetIntroDismissed();
			g_fontSize = userID.GetFontSize();
			g_bgOpacity = userID.GetBgOpacity();

			logger::info("F4SE: User ID: {} (username: {})", userIDValue, username);
			logger::info("F4SE: Preferences: privacy={} chat_enabled={} tutorial_seen={} intro_dismissed={} font_size={} opacity={}",
				g_privacyAccepted, g_chatEnabled, g_tutorialSeen, g_introDismissed, g_fontSize, g_bgOpacity);

			FalloutChat::ChatClient::GetSingleton().Initialize(serverUrl, username, userIDValue);
		} else if (msg->type == F4SE::MessagingInterface::kPostLoadGame) {
			logger::info("F4SE: kPostLoadGame — creating view");
			FalloutChat::ChatUI::CreateView();
		} else if (msg->type == F4SE::MessagingInterface::kNewGame) {
			logger::info("F4SE: kNewGame — creating view");
			FalloutChat::ChatUI::CreateView();
		}
	});

	return true;
}
