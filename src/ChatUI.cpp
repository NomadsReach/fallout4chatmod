#include "PCH.h"
#include "ChatUI.h"
#include "ChatClient.h"
#include "KeyHandler.h"
#include <shellapi.h>

extern bool g_privacyAccepted;
extern bool g_introDismissed;
extern bool g_tutorialSeen;
extern bool g_chatEnabled;
extern int g_fontSize;
extern int g_bgOpacity;
extern void SavePrivacyPolicy();
extern void SaveTutorialSeen();
extern void SaveIntroDismissed();
extern void SaveUsername(const std::string& newName);
extern void SaveChatEnabled(bool enabled);
extern void SaveFontSize(int size);
extern void SaveOpacity(int opacity);
extern uint64_t g_steamID;
extern uint64_t FetchSteamID();

namespace FalloutChat
{
	namespace ChatUI
	{
		static PRISMA_UI_API::IVPrismaUI2* g_api = nullptr;
		static PrismaView g_view = 0;
		static bool g_chatOpen = false;

		// Escape backslashes and double-quotes for embedding in a JS double-quoted string literal.
		static std::string EscapeJS(std::string s)
		{
			size_t pos = 0;
			while ((pos = s.find('\\', pos)) != std::string::npos) { s.replace(pos, 1, "\\\\"); pos += 2; }
			pos = 0;
			while ((pos = s.find('"',  pos)) != std::string::npos) { s.replace(pos, 1, "\\\""); pos += 2; }
			pos = 0;
			while ((pos = s.find('\n', pos)) != std::string::npos) { s.replace(pos, 1, "\\n");  pos += 2; }
			pos = 0;
			while ((pos = s.find('\r', pos)) != std::string::npos) { s.replace(pos, 1, "\\r");  pos += 2; }
			return s;
		}

		// Strip outer double-quotes and unescape \" and \\ from a JSON string argument.
		static std::string UnquoteJS(const char* jsonArg)
		{
			std::string s = jsonArg ? jsonArg : "";
			if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
				s = s.substr(1, s.size() - 2);
				std::string out;
				out.reserve(s.size());
				for (size_t i = 0; i < s.size(); ++i) {
					if (s[i] == '\\' && i + 1 < s.size()) {
						if      (s[i+1] == '"')  { out += '"';  ++i; }
						else if (s[i+1] == '\\') { out += '\\'; ++i; }
						else                     { out += s[i]; }
					} else {
						out += s[i];
					}
				}
				return out;
			}
			return s;
		}

		static void OnDomReady(PrismaView view)
		{
			logger::info("ChatUI: DOM ready, view={}", view);

			g_api->RegisterJSListener(view, "SendMessage", [](const char* jsonArgs) {
				std::string text = UnquoteJS(jsonArgs);
				logger::info("ChatUI: SendMessage received, len={}", text.size());
				if (text.empty()) {
					logger::warn("ChatUI: SendMessage ignored — empty text");
					return;
				}

				if (g_steamID == 0) {
					logger::info("ChatUI: SteamID not cached, attempting fetch");
					g_steamID = FetchSteamID();
					if (g_steamID != 0) {
						logger::info("ChatUI: SteamID fetched late: {}", g_steamID);
						ChatClient::GetSingleton().SetSteamID(g_steamID);
					}
				}

				if (g_steamID == 0) {
					logger::warn("ChatUI: SendMessage blocked — SteamID unavailable");
					g_api->Invoke(g_view, "addSystemMessage('Unable to verify Steam status. Try again in a moment.')");
					return;
				}

				if (text.size() > 6 && text.substr(0, 6) == "/name ") {
					std::string newName = text.substr(6);
					while (!newName.empty() && newName.front() == ' ') newName.erase(newName.begin());
					while (!newName.empty() && newName.back() == ' ') newName.pop_back();
					if (!newName.empty()) {
						logger::info("ChatUI: /name command, renaming to '{}'", newName);
						ChatClient::GetSingleton().SetUsername(newName);
						ChatClient::GetSingleton().SendRename(newName);
						::SaveUsername(newName);
						g_api->Invoke(g_view, ("addSystemMessage(\"Username changed to: " + EscapeJS(newName) + "\")").c_str());
					} else {
						logger::warn("ChatUI: /name command ignored — name was blank after trim");
					}
					return;
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				std::string locName = "";
				if (player) {
					if (auto* loc = player->currentLocation) {
						if (loc->GetFullName()) locName = loc->GetFullName();
					} else if (auto* cell = player->GetParentCell()) {
						if (cell->GetFullName()) locName = cell->GetFullName();
					}
				}

				if (text.size() > 4 && text.substr(0, 4) == "/me ")
					logger::info("ChatUI: /me emote — relying on server to format as [EMOTE]");
				logger::info("ChatUI: sending message, location='{}'", locName);
				ChatClient::GetSingleton().Send(text, locName);
			});

			g_api->RegisterJSListener(view, "CloseChat", [](const char*) {
				logger::info("ChatUI: CloseChat from JS");
				ToggleChat();
			});

			g_api->RegisterJSListener(view, "OpenURL", [](const char* urlArgs) {
				std::string url = UnquoteJS(urlArgs);
				logger::info("ChatUI: OpenURL '{}'", url);
				ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
			});

			g_api->RegisterJSListener(view, "SetPrivacyAccepted", [](const char*) {
				logger::info("ChatUI: privacy accepted");
				::SavePrivacyPolicy();
			});

			g_api->RegisterJSListener(view, "SetTutorialSeen", [](const char*) {
				logger::info("ChatUI: tutorial seen");
				::SaveTutorialSeen();
			});

			g_api->RegisterJSListener(view, "SaveChatEnabled", [](const char* jsonArgs) {
				std::string val = UnquoteJS(jsonArgs);
				logger::info("ChatUI: SaveChatEnabled '{}'", val);
				::SaveChatEnabled(val == "true");
			});

			g_api->RegisterJSListener(view, "SaveFontSize", [](const char* jsonArgs) {
				std::string val = UnquoteJS(jsonArgs);
				logger::info("ChatUI: SaveFontSize '{}'", val);
				try { ::SaveFontSize(std::stoi(val)); } catch (...) { logger::warn("ChatUI: SaveFontSize invalid '{}'", val); }
			});

			g_api->RegisterJSListener(view, "SaveOpacity", [](const char* jsonArgs) {
				std::string val = UnquoteJS(jsonArgs);
				logger::info("ChatUI: SaveOpacity '{}'", val);
				try { ::SaveOpacity(std::stoi(val)); } catch (...) { logger::warn("ChatUI: SaveOpacity invalid '{}'", val); }
			});

			// Use double-quote delimiter so usernames with single quotes don't break the JS
			std::string setUsernameJS = "if(window.SetUsernameInput) window.SetUsernameInput(\"" + EscapeJS(ChatClient::GetSingleton().GetUsername()) + "\");";
			g_api->Invoke(view, setUsernameJS.c_str());

			std::string setupJS = "if(window.setupOnboarding) window.setupOnboarding("
				+ std::string(g_introDismissed ? "true" : "false") + ", "
				+ std::string(g_privacyAccepted ? "true" : "false") + ", "
				+ std::string(g_tutorialSeen ? "true" : "false") + ");";
			g_api->Invoke(view, setupJS.c_str());

			// Push persisted appearance settings from INI (overrides localStorage defaults)
			std::string appearanceJS =
				"if(document.getElementById('fontSlider')){ document.getElementById('fontSlider').value=" + std::to_string(g_fontSize) + "; onFontSize(" + std::to_string(g_fontSize) + "); }"
				"if(document.getElementById('opacitySlider')){ document.getElementById('opacitySlider').value=" + std::to_string(g_bgOpacity) + "; onOpacity(" + std::to_string(g_bgOpacity) + "); }"
				"if(window.setChatEnabled) setChatEnabled(" + std::string(g_chatEnabled ? "true" : "false") + ");";
			g_api->Invoke(view, appearanceJS.c_str());

			logger::info("ChatUI: all JS listeners registered");

			// Flush messages and online count that arrived before the view was ready
			if (auto* ti = F4SE::GetTaskInterface()) {
				ti->AddTask([]() { OnMessagesReceived(); });
				int cachedCount = ChatClient::GetSingleton().GetOnlineCount();
				ti->AddTask([cachedCount]() { UpdateOnlineCount(cachedCount); });
			} else {
				logger::warn("ChatUI: OnDomReady — task interface unavailable, queued messages may be lost");
			}
		}

		void Initialize()
		{
			logger::info("ChatUI: initializing");
			g_api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
			if (!g_api) {
				logger::error("ChatUI: PrismaUI API unavailable — chat will not work");
				return;
			}
			logger::info("ChatUI: PrismaUI API acquired");
			KeyHandler::RegisterSink();
			(void)KeyHandler::GetSingleton()->Register(0x7A, KeyEventType::KEY_DOWN, []() { // F11
				ToggleChat();
			});
			(void)KeyHandler::GetSingleton()->Register(0x1B, KeyEventType::KEY_DOWN, []() { // Escape
				if (g_chatOpen) ToggleChat();
			});
			logger::info("ChatUI: key bindings registered (F11=toggle, Esc=close)");
		}

		void CreateView()
		{
			if (!g_api) {
				logger::error("ChatUI: CreateView called but API is null");
				return;
			}
			if (g_view != 0) {
				logger::info("ChatUI: CreateView skipped — view already exists ({})", g_view);
				return;
			}

			g_view = g_api->CreateView("chat.html", OnDomReady);
			logger::info("ChatUI: view created ({})", g_view);

			g_api->RegisterConsoleCallback(g_view, [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel level, const char* msg) {
				switch (level) {
				case PRISMA_UI_API::ConsoleMessageLevel::Error:   logger::error("[JS] {}", msg); break;
				case PRISMA_UI_API::ConsoleMessageLevel::Warning: logger::warn("[JS] {}", msg);  break;
				default:                                          logger::info("[JS] {}", msg);  break;
				}
			});

			// Keep view ALWAYS shown so background HUD intervals work
			g_api->Show(g_view);
			logger::info("ChatUI: view shown (background), waiting for DOM ready");
		}

		void ToggleChat()
		{
			if (!g_api || !g_api->IsValid(g_view)) {
				logger::warn("ChatUI: ToggleChat called but view is invalid (api={}, view={})",
					(void*)g_api, g_view);
				return;
			}

			g_chatOpen = !g_chatOpen;
			logger::info("ChatUI: chat {}", g_chatOpen ? "opened" : "closed");

			if (g_chatOpen) {
				if (!g_introDismissed) {
					::SaveIntroDismissed();
				}
				// MUST use disableFocusMenu=false so the engine pushes FocusMenu and unclips the mouse
				g_api->Focus(g_view, false, false);
				g_api->Invoke(g_view, "onChatOpened()");

				// Hide the vanilla game cursor (which is pushed by the engine due to unpaused kUsesCursor)
				auto ui = RE::UI::GetSingleton();
				if (ui) {
					auto cursorMenu = ui->GetMenu("CursorMenu");
					if (cursorMenu && cursorMenu->uiMovie) {
						cursorMenu->uiMovie->SetVisible(false);
					}
				}
			} else {
				g_api->Unfocus(g_view);
				g_api->Invoke(g_view, "onChatClosed()");

				// Restore the vanilla game cursor
				auto ui = RE::UI::GetSingleton();
				if (ui) {
					auto cursorMenu = ui->GetMenu("CursorMenu");
					if (cursorMenu && cursorMenu->uiMovie) {
						cursorMenu->uiMovie->SetVisible(true);
					}
				}
			}
		}

		bool IsChatOpen()
		{
			return g_chatOpen;
		}

		void OnMessagesReceived()
		{
			if (!g_api || !g_api->IsValid(g_view)) return;

			auto msgs = ChatClient::GetSingleton().GetNewMessages();
			logger::info("ChatUI: dispatching {} message(s) to UI", msgs.size());
			std::string batch_js = "";
			for (const auto& m : msgs) {
				logger::info("ChatUI: render msg sender='{}' time='{}' emote={}", m.sender, m.timestamp, m.isEmote);
				batch_js += "receiveMessage(\""
					+ EscapeJS(m.sender) + "\", \""
					+ EscapeJS(m.text)   + "\", \""
					+ EscapeJS(m.timestamp) + "\", "
					+ (m.isEmote ? "true" : "false") + ");\n";
			}
			if (!batch_js.empty()) {
				g_api->Invoke(g_view, batch_js.c_str());
			}
		}

		void UpdateOnlineCount(int count)
		{
			if (!g_api || !g_api->IsValid(g_view)) return;
			logger::info("ChatUI: online count → {}", count);
			g_api->Invoke(g_view, ("updateOnlineCount(" + std::to_string(count) + ")").c_str());
		}
	}
}
