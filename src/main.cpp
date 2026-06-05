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
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetBoolValue("General", "privacy_accepted", true);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
	g_privacyAccepted = true;
}

void SaveChatEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_chatEnabled = enabled;
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetBoolValue("General", "chat_enabled", enabled);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
}

void SaveTutorialSeen()
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetBoolValue("General", "tutorial_seen", true);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
	g_tutorialSeen = true;
}

void SaveIntroDismissed()
{
	std::lock_guard<std::mutex> lock(g_iniMutex);
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetBoolValue("General", "intro_dismissed", true);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
	g_introDismissed = true;
}

void SaveFontSize(int size)
{
	if (size < 10 || size > 20) {
		logger::warn("SaveFontSize: size {} out of range [10, 20], ignoring", size);
		return;
	}
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_fontSize = size;
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetLongValue("General", "font_size", size);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
}

void SaveOpacity(int opacity)
{
	if (opacity < 10 || opacity > 100) {
		logger::warn("SaveOpacity: opacity {} out of range [10, 100], ignoring", opacity);
		return;
	}
	std::lock_guard<std::mutex> lock(g_iniMutex);
	g_bgOpacity = opacity;
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetLongValue("General", "bg_opacity", opacity);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
}

void LoadConfigs()
{
	logger::info("LoadConfigs: reading FalloutChat.ini");
	std::lock_guard<std::mutex> lock(g_iniMutex);
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");

	// Server URL is hardcoded — not user-configurable to prevent redirection attacks
	serverUrl = "wss://chat.fallenworld.nexus/ws";
	// Username is now managed by UserID class (persisted in %APPDATA%\Local\FalloutChat\user_id.json)
	username = "Player";

	bool privacyAlreadyAccepted = ini.GetBoolValue("General", "privacy_accepted", false);
	g_privacyAccepted  = privacyAlreadyAccepted;
	g_chatEnabled      = ini.GetBoolValue("General", "chat_enabled", true);
	g_tutorialSeen     = ini.GetBoolValue("General", "tutorial_seen", false);
	g_introDismissed   = ini.GetBoolValue("General", "intro_dismissed", privacyAlreadyAccepted);
	g_fontSize         = (int)ini.GetLongValue("General", "font_size",  14);
	g_bgOpacity        = (int)ini.GetLongValue("General", "bg_opacity", 60);
	ini.Reset();

	logger::info("LoadConfigs: url='{}' username='{}' privacy={} enabled={} tutorialSeen={} introDismissed={} fontSize={} opacity={}",
		serverUrl, username, g_privacyAccepted, g_chatEnabled, g_tutorialSeen, g_introDismissed, g_fontSize, g_bgOpacity);
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
			std::string userIDValue = userID.GetID();
			username = userID.GetUsername();
			logger::info("F4SE: User ID: {} (username: {})", userIDValue, username);

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
