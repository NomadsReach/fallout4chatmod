#include "PCH.h"
#include "Renderer.h"
#include "ChatClient.h"
#include <SimpleIni.h>

CSimpleIniA ini(true, false, false);
std::string serverUrl = "wss://fallenworld.nexus/ws";
std::string username = "Player";
bool g_privacyAccepted = false;
uint64_t g_steamID = 0;
bool g_chatEnabled = true;

uint64_t FetchSteamID()
{
	HMODULE hSteam = GetModuleHandleA("steam_api64.dll");
	if (!hSteam) {
		logger::warn("FetchSteamID: steam_api64.dll not loaded");
		return 0;
	}

	auto pfnSteamUser = reinterpret_cast<void*(*)()>(GetProcAddress(hSteam, "SteamUser"));
	if (!pfnSteamUser) {
		logger::warn("FetchSteamID: SteamUser export not found");
		return 0;
	}

	void* user = pfnSteamUser();
	if (!user) {
		logger::warn("FetchSteamID: SteamUser() returned null");
		return 0;
	}

	// CSteamID uses hidden-pointer return (MSVC x64): call vtable slot directly
	// so RDX points to our storage, not a garbage code-section address.
	void** vtable = *reinterpret_cast<void***>(user);
	using GetSteamID_fn = void*(__fastcall*)(void* self, uint64_t* result);
	auto pfnGetSteamID = reinterpret_cast<GetSteamID_fn>(vtable[2]);

	uint64_t id = 0;
	pfnGetSteamID(user, &id);
	return id;
}

void SaveUsername(const std::string& newName)
{
	username = newName;
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetValue("General", "username", newName.c_str());
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
}

void SavePrivacyPolicy()
{
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetBoolValue("General", "privacy_accepted", true);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	g_privacyAccepted = true;
}

void SaveChatEnabled(bool enabled)
{
	g_chatEnabled = enabled;
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetBoolValue("General", "chat_enabled", enabled);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.Reset();
}

void LoadConfigs()
{
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");

	serverUrl = ini.GetValue("General", "server_url", "wss://fallenworld.nexus/ws");
	username = ini.GetValue("General", "username", "");

	if (username.empty())
		username = "Player";

	g_privacyAccepted = ini.GetBoolValue("General", "privacy_accepted", false);
	g_chatEnabled = ini.GetBoolValue("General", "chat_enabled", true);
	ini.Reset();
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface * a_f4se, F4SE::PluginInfo * a_info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("--> %v"s);

	logger::info("{} v{}", Version::PROJECT, Version::NAME);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = "FalloutChat";
	a_info->version = Version::MAJOR;

	if (a_f4se->IsEditor()) {
		logger::critical("loaded in editor");
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if (ver < REL::Version{ 1, 10, 984, 0 }) {
		logger::critical("unsupported runtime v{}", ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface * a_f4se)
{
	F4SE::Init(a_f4se);
	LoadConfigs();

	const F4SE::MessagingInterface* messageInterface = F4SE::GetMessagingInterface();
	messageInterface->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
			FalloutChat::Renderer::InstallHooks();
			if (username.empty())
				username = "Player";
			FalloutChat::ChatClient::GetSingleton().Initialize(serverUrl, username, 0);
		}
	});

	return true;
}
