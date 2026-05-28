#include "PCH.h"
#include "Renderer.h"
#include "ChatClient.h"
#include <shellapi.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <set>
#include <cmath>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <winhttp.h>
#include <wincodec.h>
#include "RE/Bethesda/MenuCursor.h"
#include "icons/IconsFontAwesome6.h"
#include "icons/IconsFontAwesome6Brands.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern bool g_privacyAccepted;
extern void SavePrivacyPolicy();
extern void SaveUsername(const std::string& newName);
extern uint64_t g_steamID;
extern uint64_t FetchSteamID();
extern bool g_chatEnabled;
extern void SaveChatEnabled(bool enabled);

namespace FalloutChat
{
	namespace Renderer
	{
		typedef HRESULT(APIENTRY* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
		Present_t oPresent = nullptr;

		typedef HRESULT(APIENTRY* ResizeBuffers_t)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
		ResizeBuffers_t oResizeBuffers = nullptr;

		WNDPROC oWndProc = nullptr;

		bool g_initialized = false;
		bool g_chatOpen = false;
		HWND g_hwnd = nullptr;
		ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
		ID3D11Device* g_pDevice = nullptr;
		ID3D11DeviceContext* g_pDeviceContext = nullptr;

		std::vector<ChatMessage> g_chatHistory;
		char g_inputBuffer[256] = "";

		static std::set<std::string> s_mutedPlayers;
		static int s_unreadCount = 0;

		struct CachedImage { ID3D11ShaderResourceView* srv = nullptr; int width = 0, height = 0; };
		struct PendingUpload { std::string url; std::vector<uint8_t> pixels; int width = 0, height = 0; };

		static std::unordered_map<std::string, CachedImage> s_imageCache;
		static std::set<std::string> s_downloading;
		static std::mutex s_imageMutex;
		static std::vector<PendingUpload> s_pendingUploads;

		static const ImVec4 SENDER_PALETTE[] = {
			ImVec4(0.2f, 0.8f, 1.0f, 1.0f),
			ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
			ImVec4(0.9f, 0.4f, 0.9f, 1.0f),
			ImVec4(0.4f, 0.95f, 0.4f, 1.0f),
			ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
			ImVec4(0.5f, 0.85f, 1.0f, 1.0f),
			ImVec4(1.0f, 1.0f, 0.4f, 1.0f),
			ImVec4(0.85f, 0.6f, 1.0f, 1.0f),
		};

		static ImVec4 SenderColor(const std::string& name)
		{
			uint32_t hash = 5381;
			for (char c : name) hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
			constexpr size_t N = sizeof(SENDER_PALETTE) / sizeof(SENDER_PALETTE[0]);
			return SENDER_PALETTE[hash % N];
		}

		static float s_passiveAlpha = 0.0f;
		static std::chrono::steady_clock::time_point s_lastMessageArrival{};
		static bool s_passiveEverTriggered = false;

		static constexpr float PASSIVE_FADE_IN  = 0.25f;
		static constexpr float PASSIVE_FADE_OUT = 1.5f;
		static float s_passiveHoldTime = 5.0f;
		static float s_passiveMaxAlpha = 0.85f;

		static const char* kTickerMessages[] = {
			"WELCOME TO FALLOUT 4 GLOBAL CHAT",
			"MADE WITH LOVE FROM FALLEN WORLD TEAM"
		};
		static constexpr int   kTickerCount = 2;
		static constexpr float kTickerSpeed = 80.0f;

		static std::string CurrentTimestamp()
		{
			auto now = std::chrono::system_clock::now();
			auto t = std::chrono::system_clock::to_time_t(now);
			std::tm tm_now;
			localtime_s(&tm_now, &t);
			std::ostringstream oss;
			oss << std::put_time(&tm_now, "%H:%M:%S");
			return oss.str();
		}

		static std::string GetPlayerLocation()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) return "";

			if (auto* loc = player->currentLocation) {
				const char* name = loc->GetFullName();
				if (name && name[0] != '\0') return name;
			}

			if (auto* cell = player->GetParentCell()) {
				const char* name = cell->GetFullName();
				if (name && name[0] != '\0') return name;
			}

			return "";
		}

		static std::string ExtractImageUrl(const std::string& text)
		{
			auto pos = text.find("https://");
			if (pos == std::string::npos) return {};
			auto endPos = text.find_first_of(" \t\n\r", pos);
			std::string url = (endPos == std::string::npos) ? text.substr(pos) : text.substr(pos, endPos - pos);

			static const char* kExts[] = { ".gif", ".png", ".jpg", ".jpeg", ".webp", nullptr };
			for (int i = 0; kExts[i]; ++i)
				if (url.find(kExts[i]) != std::string::npos) return url;

			if (url.find("tenor.com/view/") != std::string::npos ||
				url.find("giphy.com/gifs/") != std::string::npos ||
				url.find("giphy.com/media/") != std::string::npos)
				return url;

			return {};
		}

		static std::vector<uint8_t> FetchUrl(const std::string& url, size_t limit = 8 * 1024 * 1024)
		{
			std::wstring wurl(url.begin(), url.end());
			URL_COMPONENTSW uc = {};
			uc.dwStructSize = sizeof(uc);
			wchar_t host[256] = {}, path[2048] = {};
			uc.lpszHostName = host; uc.dwHostNameLength = 256;
			uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
			if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
				logger::warn("FetchUrl: WinHttpCrackUrl failed for {}", url);
				return {};
			}

			HINTERNET hSess = WinHttpOpen(
				L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/120.0",
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
			if (!hSess) { logger::warn("FetchUrl: WinHttpOpen failed"); return {}; }

			HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
			if (!hConn) { WinHttpCloseHandle(hSess); logger::warn("FetchUrl: WinHttpConnect failed"); return {}; }

			DWORD flags = (url.rfind("https://", 0) == 0) ? WINHTTP_FLAG_SECURE : 0;
			HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr, nullptr, nullptr, flags);
			if (hReq) {
				WinHttpAddRequestHeaders(hReq,
					L"Accept: text/html,image/gif,image/*,*/*\r\nAccept-Language: en-US,en;q=0.9",
					static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
			}
			if (!hReq || !WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(hReq, nullptr)) {
				if (hReq) WinHttpCloseHandle(hReq);
				WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
				logger::warn("FetchUrl: request failed for {}", url);
				return {};
			}

			DWORD statusCode = 0;
			DWORD statusSize = sizeof(statusCode);
			WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				nullptr, &statusCode, &statusSize, nullptr);
			logger::warn("FetchUrl: {} -> HTTP {}", url, statusCode);

			std::vector<uint8_t> bytes;
			DWORD read = 0;
			uint8_t buf[8192];
			while (bytes.size() < limit && WinHttpReadData(hReq, buf, sizeof(buf), &read) && read > 0)
				bytes.insert(bytes.end(), buf, buf + read);
			WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
			logger::warn("FetchUrl: downloaded {} bytes from {}", bytes.size(), url);
			return bytes;
		}

		static std::string ExtractOgImage(const std::vector<uint8_t>& htmlBytes)
		{
			std::string html(htmlBytes.begin(), htmlBytes.end());
			// Match both attribute orderings of the og:image meta tag
			for (const auto& needle : { std::string("property=\"og:image\" content=\""), std::string("name=\"og:image\" content=\"") }) {
				auto pos = html.find(needle);
				if (pos != std::string::npos) {
					pos += needle.size();
					auto end = html.find('"', pos);
					if (end != std::string::npos) return html.substr(pos, end - pos);
				}
			}
			// Try reversed attribute order: content="..." property="og:image"
			size_t pos = 0;
			while ((pos = html.find("og:image", pos)) != std::string::npos) {
				auto tagStart = html.rfind('<', pos);
				if (tagStart != std::string::npos) {
					auto tagEnd = html.find('>', pos);
					std::string tag = html.substr(tagStart, tagEnd != std::string::npos ? tagEnd - tagStart : 200);
					auto cpos = tag.find("content=\"");
					if (cpos != std::string::npos) {
						cpos += 9;
						auto cend = tag.find('"', cpos);
						if (cend != std::string::npos) return tag.substr(cpos, cend - cpos);
					}
				}
				pos += 8;
			}
			return {};
		}

		static bool DecodeAndQueue(const std::string& cacheKey, std::vector<uint8_t>& bytes)
		{
			IWICImagingFactory* factory = nullptr;
			HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
			if (!factory) { logger::warn("DecodeAndQueue: WICFactory failed hr={:08X}", (uint32_t)hr); return false; }

			IWICStream* stream = nullptr;
			factory->CreateStream(&stream);
			if (!stream) { factory->Release(); logger::warn("DecodeAndQueue: CreateStream failed"); return false; }
			stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()));

			IWICBitmapDecoder* decoder = nullptr;
			hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
			stream->Release();
			if (!decoder) { factory->Release(); logger::warn("DecodeAndQueue: CreateDecoder failed hr={:08X} bytes={}", (uint32_t)hr, bytes.size()); return false; }

			IWICBitmapFrameDecode* frame = nullptr;
			decoder->GetFrame(0, &frame);
			decoder->Release();
			if (!frame) { factory->Release(); logger::warn("DecodeAndQueue: GetFrame failed"); return false; }

			UINT w = 0, h = 0;
			frame->GetSize(&w, &h);
			if (w == 0 || h == 0 || w > 4096 || h > 4096) {
				frame->Release(); factory->Release();
				logger::warn("DecodeAndQueue: bad dimensions {}x{}", w, h);
				return false;
			}

			IWICFormatConverter* conv = nullptr;
			factory->CreateFormatConverter(&conv);
			if (!conv) { frame->Release(); factory->Release(); logger::warn("DecodeAndQueue: CreateFormatConverter failed"); return false; }
			hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
			frame->Release();
			if (FAILED(hr)) { conv->Release(); factory->Release(); logger::warn("DecodeAndQueue: converter init failed hr={:08X}", (uint32_t)hr); return false; }

			std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
			hr = conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data());
			conv->Release();
			factory->Release();
			if (FAILED(hr)) { logger::warn("DecodeAndQueue: CopyPixels failed hr={:08X}", (uint32_t)hr); return false; }

			logger::warn("DecodeAndQueue: success {}x{} for {}", w, h, cacheKey);
			std::lock_guard<std::mutex> lock(s_imageMutex);
			s_pendingUploads.push_back({ cacheKey, std::move(pixels), static_cast<int>(w), static_cast<int>(h) });
			return true;
		}

		static void DownloadAndDecode(std::string url)
		{
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

			auto bytes = FetchUrl(url);
			if (bytes.empty()) { CoUninitialize(); return; }

			// If it's an HTML page (Tenor/Giphy), resolve to the direct image via og:image
			std::string head(bytes.begin(), bytes.begin() + (bytes.size() < 64 ? bytes.size() : 64));
			if (head.find("<!") != std::string::npos || head.find("<h") != std::string::npos) {
				std::string directUrl = ExtractOgImage(bytes);
				if (directUrl.empty()) { CoUninitialize(); return; }
				bytes = FetchUrl(directUrl);
				if (bytes.empty()) { CoUninitialize(); return; }
			}

			DecodeAndQueue(url, bytes);
			CoUninitialize();
		}

		static void StartImageDownload(const std::string& url)
		{
			std::lock_guard<std::mutex> lock(s_imageMutex);
			if (s_imageCache.count(url) || s_downloading.count(url)) return;
			s_downloading.insert(url);
			std::thread(DownloadAndDecode, url).detach();
		}

		void HandleSendInput()
		{
			std::string text(g_inputBuffer);
			memset(g_inputBuffer, 0, sizeof(g_inputBuffer));
			if (text.empty()) return;

			if (g_steamID == 0) {
				g_steamID = FetchSteamID();
				if (g_steamID != 0)
					ChatClient::GetSingleton().SetSteamID(g_steamID);
			}

			auto sysMsg = [&](const std::string& txt) {
				ChatMessage m;
				m.sender    = "System";
				m.text      = txt;
				m.timestamp = CurrentTimestamp();
				g_chatHistory.push_back(m);
			};

			if (g_steamID == 0) {
				sysMsg("Unable to verify Steam status. Try again in a moment.");
				return;
			}

			if (text.size() > 6 && text.substr(0, 6) == "/name ") {
				std::string newName = text.substr(6);
				while (!newName.empty() && newName.front() == ' ') newName.erase(newName.begin());
				while (!newName.empty() && newName.back() == ' ') newName.pop_back();
				if (!newName.empty()) {
					ChatClient::GetSingleton().SetUsername(newName);
					ChatClient::GetSingleton().SendRename(newName);
					::SaveUsername(newName);
					sysMsg("Username changed to: " + newName);
				}
				return;
			}

			if (text.size() > 6 && text.substr(0, 6) == "/mute ") {
				std::string target = text.substr(6);
				while (!target.empty() && target.back() == ' ') target.pop_back();
				if (!target.empty()) {
					s_mutedPlayers.insert(target);
					sysMsg("Muted: " + target);
				}
				return;
			}

			if (text.size() > 8 && text.substr(0, 8) == "/unmute ") {
				std::string target = text.substr(8);
				while (!target.empty() && target.back() == ' ') target.pop_back();
				if (!target.empty()) {
					s_mutedPlayers.erase(target);
					sysMsg("Unmuted: " + target);
				}
				return;
			}

			ChatClient::GetSingleton().Send(text, GetPlayerLocation());
		}

		void ToggleChat(bool forceState, bool state)
		{
			g_chatOpen = forceState ? state : !g_chatOpen;

			if (g_chatOpen)
				s_unreadCount = 0;

			ImGuiIO& io = ImGui::GetIO();
			if (g_chatOpen) {
				io.MouseDrawCursor = true;
				if (auto menuCursor = RE::MenuCursor::GetSingleton()) {
					menuCursor->RegisterCursor();
					menuCursor->ClearConstraints();
				}
				::ClipCursor(nullptr);
			} else {
				io.MouseDrawCursor = false;
				if (auto menuCursor = RE::MenuCursor::GetSingleton()) {
					menuCursor->UnregisterCursor();
				}
				memset(g_inputBuffer, 0, sizeof(g_inputBuffer));
			}

			if (auto controlMap = RE::ControlMap::GetSingleton()) {
				if (g_chatOpen)
					++controlMap->byTextEntryCount;
				else if (controlMap->byTextEntryCount > 0)
					--controlMap->byTextEntryCount;
				controlMap->ignoreKeyboardMouse = g_chatOpen;
			}
		}

		bool IsChatOpen()
		{
			return g_chatOpen;
		}

		LRESULT CALLBACK WndProc_Hook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
		{
			if (uMsg == WM_KEYDOWN && wParam == VK_F11) {
				ToggleChat(true, !g_chatOpen);
				return 1;
			}

			if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE && g_chatOpen) {
				ToggleChat(true, false);
				return 1;
			}

			if (g_chatOpen) {
				if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
					return 1;
				}

				switch (uMsg) {
				case WM_KEYDOWN:
				case WM_KEYUP:
				case WM_CHAR:
				case WM_SYSKEYDOWN:
				case WM_SYSKEYUP:
				case WM_LBUTTONDOWN:
				case WM_LBUTTONUP:
				case WM_RBUTTONDOWN:
				case WM_RBUTTONUP:
				case WM_MBUTTONDOWN:
				case WM_MBUTTONUP:
				case WM_MOUSEMOVE:
				case WM_MOUSEWHEEL:
				case WM_XBUTTONDOWN:
				case WM_XBUTTONUP:
					return 1;
				}
			}

			return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
		}

		void ApplyFalloutTheme()
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowRounding = 8.0f;
			style.FrameRounding = 4.0f;
			style.PopupRounding = 4.0f;
			style.ScrollbarRounding = 12.0f;
			style.GrabRounding = 4.0f;

			ImVec4* colors = style.Colors;

			ImVec4 mainColor = ImVec4(0.0f, 1.0f, 0.3f, 1.0f);
			ImVec4 bgDark = ImVec4(0.02f, 0.08f, 0.04f, 0.75f);
			ImVec4 textCol = ImVec4(0.4f, 1.0f, 0.6f, 1.0f);

			colors[ImGuiCol_Text] = textCol;
			colors[ImGuiCol_TextDisabled] = ImVec4(0.2f, 0.6f, 0.3f, 1.0f);
			colors[ImGuiCol_WindowBg] = bgDark;
			colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
			colors[ImGuiCol_PopupBg] = ImVec4(0.01f, 0.05f, 0.02f, 0.95f);
			colors[ImGuiCol_Border] = mainColor;
			colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

			colors[ImGuiCol_FrameBg] = ImVec4(0.02f, 0.12f, 0.05f, 0.6f);
			colors[ImGuiCol_FrameBgHovered] = ImVec4(0.05f, 0.2f, 0.08f, 0.8f);
			colors[ImGuiCol_FrameBgActive] = ImVec4(0.1f, 0.3f, 0.12f, 1.0f);

			colors[ImGuiCol_TitleBg] = bgDark;
			colors[ImGuiCol_TitleBgActive] = ImVec4(0.03f, 0.15f, 0.06f, 0.9f);
			colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.4f);

			colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
			colors[ImGuiCol_ScrollbarBg] = ImVec4(0.01f, 0.05f, 0.02f, 0.5f);
			colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.0f, 0.8f, 0.2f, 0.6f);
			colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.0f, 1.0f, 0.3f, 0.8f);
			colors[ImGuiCol_ScrollbarGrabActive] = mainColor;

			colors[ImGuiCol_CheckMark] = mainColor;
			colors[ImGuiCol_SliderGrab] = ImVec4(0.0f, 0.8f, 0.2f, 0.8f);
			colors[ImGuiCol_SliderGrabActive] = mainColor;

			colors[ImGuiCol_Button] = ImVec4(0.02f, 0.12f, 0.05f, 0.6f);
			colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.8f, 0.2f, 0.8f);
			colors[ImGuiCol_ButtonActive] = mainColor;

			colors[ImGuiCol_Header] = ImVec4(0.02f, 0.15f, 0.05f, 0.6f);
			colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.8f, 0.2f, 0.8f);
			colors[ImGuiCol_HeaderActive] = mainColor;

			colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.8f, 0.2f, 0.4f);
			colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.0f, 0.8f, 0.2f, 0.8f);
			colors[ImGuiCol_ResizeGripActive] = mainColor;

			colors[ImGuiCol_Tab] = ImVec4(0.02f, 0.12f, 0.05f, 0.6f);
			colors[ImGuiCol_TabHovered] = ImVec4(0.0f, 0.8f, 0.2f, 0.8f);
			colors[ImGuiCol_TabActive] = mainColor;
		}

		HRESULT APIENTRY Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
		{
			if (!g_initialized) {
				ID3D11Device* device = nullptr;
				if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) {
					ID3D11DeviceContext* context = nullptr;
					device->GetImmediateContext(&context);

					DXGI_SWAP_CHAIN_DESC desc = {};
					pSwapChain->GetDesc(&desc);
					g_hwnd = desc.OutputWindow;

					g_pDevice = device;
					g_pDeviceContext = context;

					IMGUI_CHECKVERSION();
					ImGui::CreateContext();

					{
						ImGuiIO& io = ImGui::GetIO();
						io.Fonts->AddFontDefault();

						static const ImWchar brands_ranges[] = { ICON_MIN_FAB, ICON_MAX_FAB, 0 };
						ImFontConfig brands_cfg;
						brands_cfg.MergeMode = true;
						brands_cfg.GlyphMinAdvanceX = 13.0f;
						io.Fonts->AddFontFromFileTTF("Data\\F4SE\\Plugins\\fa-brands-400.ttf", 13.0f, &brands_cfg, brands_ranges);

						static const ImWchar solid_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
						ImFontConfig solid_cfg;
						solid_cfg.MergeMode = true;
						solid_cfg.GlyphMinAdvanceX = 13.0f;
						io.Fonts->AddFontFromFileTTF("Data\\F4SE\\Plugins\\fa-solid-900.ttf", 13.0f, &solid_cfg, solid_ranges);
					}

					ApplyFalloutTheme();

					ImGui_ImplWin32_Init(g_hwnd);
					ImGui_ImplDX11_Init(g_pDevice, g_pDeviceContext);

					oWndProc = (WNDPROC)SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc_Hook);

					g_initialized = true;
				}
			}

			if (g_initialized && !g_pRenderTargetView) {
				ID3D11Texture2D* pBackBuffer = nullptr;
				if (SUCCEEDED(pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
					g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
					pBackBuffer->Release();
				}
			}

			if (g_initialized && g_pRenderTargetView) {
				auto newMsgs = ChatClient::GetSingleton().GetNewMessages();
				if (!newMsgs.empty()) {
					g_chatHistory.insert(g_chatHistory.end(), newMsgs.begin(), newMsgs.end());
					if (g_chatHistory.size() > 100) {
						g_chatHistory.erase(g_chatHistory.begin(), g_chatHistory.begin() + (g_chatHistory.size() - 100));
					}
					s_lastMessageArrival = std::chrono::steady_clock::now();
					s_passiveEverTriggered = true;
					if (!g_chatOpen)
						s_unreadCount += static_cast<int>(newMsgs.size());
				}

				{
					std::lock_guard<std::mutex> lock(s_imageMutex);
					if (!s_pendingUploads.empty()) {
						auto* dev = g_pDevice;
						for (auto& up : s_pendingUploads) {
							D3D11_TEXTURE2D_DESC td = {};
							td.Width            = static_cast<UINT>(up.width);
							td.Height           = static_cast<UINT>(up.height);
							td.MipLevels        = 1;
							td.ArraySize        = 1;
							td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
							td.SampleDesc.Count = 1;
							td.Usage            = D3D11_USAGE_DEFAULT;
							td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
							D3D11_SUBRESOURCE_DATA sd = {};
							sd.pSysMem      = up.pixels.data();
							sd.SysMemPitch  = static_cast<UINT>(up.width) * 4;
							ID3D11Texture2D* tex = nullptr;
							if (SUCCEEDED(dev->CreateTexture2D(&td, &sd, &tex))) {
								D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
								svd.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
								svd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
								svd.Texture2D.MipLevels       = 1;
								ID3D11ShaderResourceView* srv = nullptr;
								dev->CreateShaderResourceView(tex, &svd, &srv);
								tex->Release();
								if (srv) s_imageCache[up.url] = { srv, up.width, up.height };
							}
						}
						s_pendingUploads.clear();
					}
				}

				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();

				if (g_chatOpen) {
					if (auto menuCursor = RE::MenuCursor::GetSingleton()) {
						menuCursor->ClearConstraints();
					}
					::ClipCursor(nullptr);
					if (auto controlMap = RE::ControlMap::GetSingleton()) {
						controlMap->ignoreKeyboardMouse = true;
					}
				}

				if (g_chatOpen && !::g_privacyAccepted) {
					ImGui::SetNextWindowSize(ImVec2(500, 250), ImGuiCond_FirstUseEver);
					ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
					ImGui::Begin("Data Privacy Policy", &g_chatOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Action Required:");
					ImGui::Separator();
					ImGui::TextWrapped(
						"Before you join the Fallen World global chat, please review our Data Privacy Policy.\n\n"
						"By using this chat system, you agree that your messages, username, and "
						"game reports may be stored and reviewed by server administrators to enforce "
						"our language and moderation policies.\n\n"
						"Severe violations will result in automated blocking and potential bans. "
						"We do not sell or share your data with any third parties."
					);
					ImGui::Spacing(); ImGui::Spacing();
					if (ImGui::Button("I Agree & Continue", ImVec2(200, 30))) {
						::SavePrivacyPolicy();
					}
					ImGui::SameLine();
					if (ImGui::Button("Decline & Close", ImVec2(200, 30))) {
						ToggleChat(true, false);
					}
					ImGui::End();
				}

				if (s_passiveEverTriggered && !g_chatOpen) {
					float elapsed = std::chrono::duration<float>(
						std::chrono::steady_clock::now() - s_lastMessageArrival).count();
					if (elapsed < PASSIVE_FADE_IN)
						s_passiveAlpha = elapsed / PASSIVE_FADE_IN;
					else if (elapsed < PASSIVE_FADE_IN + s_passiveHoldTime)
						s_passiveAlpha = 1.0f;
					else if (elapsed < PASSIVE_FADE_IN + s_passiveHoldTime + PASSIVE_FADE_OUT)
						s_passiveAlpha = 1.0f - (elapsed - PASSIVE_FADE_IN - s_passiveHoldTime) / PASSIVE_FADE_OUT;
					else
						s_passiveAlpha = 0.0f;
				}

				bool showChat = (g_chatOpen && ::g_privacyAccepted) ||
				                (g_chatEnabled && s_passiveEverTriggered && s_passiveAlpha > 0.01f && !g_chatOpen);

				if (showChat) {
					float alpha = g_chatOpen ? 1.0f : (s_passiveAlpha * s_passiveMaxAlpha);

					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
					ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
					ImGui::SetNextWindowPos(ImVec2(12.0f, ImGui::GetIO().DisplaySize.y - 312.0f), ImGuiCond_FirstUseEver);
					ImGui::SetNextWindowBgAlpha(0.75f);

					ImGuiWindowFlags wflags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
					if (!g_chatOpen) {
						wflags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
						          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
					}

					bool closeRequested = false;
					ImGui::Begin("Fallout Multi-Chat", g_chatOpen ? &closeRequested : nullptr, wflags);

					if (g_chatOpen) {
						float winW = ImGui::GetWindowWidth();
						bool wideEnough = winW >= 320.0f;

						if (wideEnough) {
							ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "Made with love by the Fallen World team");
							ImGui::SameLine(winW - 220.0f);

							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.396f, 0.949f, 0.85f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.345f, 0.396f, 0.949f, 1.0f));
							ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.271f, 0.322f, 0.878f, 1.0f));
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
							if (ImGui::Button(ICON_FA_DISCORD " Discord", ImVec2(100, 0)))
								ShellExecuteA(NULL, "open", "https://discord.com/invite/TAueAV8Utk", NULL, NULL, SW_SHOWNORMAL);
							ImGui::PopStyleColor(4);

							ImGui::SameLine();
							if (ImGui::Button(ICON_FA_GLOBE " Website", ImVec2(76, 0)))
								ShellExecuteA(NULL, "open", "https://fallenworld.nexus/", NULL, NULL, SW_SHOWNORMAL);

							ImGui::SameLine();
						} else {
							ImGui::SameLine(winW - 34.0f);
						}

						if (ImGui::Button(ICON_FA_GEAR, ImVec2(26, 0)))
							ImGui::OpenPopup("##ChatSettings");

						if (ImGui::BeginPopup("##ChatSettings")) {
							ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Chat Settings");
							ImGui::Separator();
							bool prevEnabled = g_chatEnabled;
							ImGui::Checkbox("Enable Chat", &g_chatEnabled);
							if (g_chatEnabled != prevEnabled) {
								::SaveChatEnabled(g_chatEnabled);
								if (!g_chatEnabled && g_chatOpen)
									ToggleChat(true, false);
							}
							ImGui::Spacing();
							ImGui::SetNextItemWidth(180.0f);
							ImGui::SliderFloat("Passive opacity", &s_passiveMaxAlpha, 0.1f, 1.0f, "%.2f");
							ImGui::SetNextItemWidth(180.0f);
							ImGui::SliderFloat("Hold time (sec)", &s_passiveHoldTime, 1.0f, 15.0f, "%.0f");
							if (!s_mutedPlayers.empty()) {
								ImGui::Separator();
								ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Muted players:");
								for (const auto& name : s_mutedPlayers)
									ImGui::Text("  %s", name.c_str());
							}
							ImGui::Separator();
							ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Commands");
							ImGui::TextDisabled("/me <action>       - Emote (e.g. /me waves)");
							ImGui::TextDisabled("/name <username>   - Change your display name");
							ImGui::TextDisabled("/mute <player>     - Mute a player locally");
							ImGui::TextDisabled("/unmute <player>   - Unmute a player");
							ImGui::TextDisabled("/report <player> <reason>  - Report a player");
							ImGui::TextDisabled("F11  - Open / close chat");
							ImGui::Separator();
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.396f, 0.949f, 0.85f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.345f, 0.396f, 0.949f, 1.0f));
							ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.271f, 0.322f, 0.878f, 1.0f));
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
							if (ImGui::Button(ICON_FA_DISCORD " Discord", ImVec2(180, 0)))
								ShellExecuteA(NULL, "open", "https://discord.com/invite/TAueAV8Utk", NULL, NULL, SW_SHOWNORMAL);
							ImGui::PopStyleColor(4);
							if (ImGui::Button(ICON_FA_GLOBE " Website", ImVec2(180, 0)))
								ShellExecuteA(NULL, "open", "https://fallenworld.nexus/", NULL, NULL, SW_SHOWNORMAL);
							ImGui::EndPopup();
						}

						ImGui::Separator();

						bool isConnected = ChatClient::GetSingleton().IsConnected();
						int  onlineCount = ChatClient::GetSingleton().GetOnlineCount();
						if (isConnected) {
							ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
							if (onlineCount > 0) {
								ImGui::SameLine();
								ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), ICON_FA_USER " %d online", onlineCount);
							}
						} else {
							ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Disconnected - Reconnecting...");
						}
						ImGui::Separator();
					}

					// ── Ticker strip ────────────────────────────────────────────────────────
					{
						ImVec2  stripMin = ImGui::GetCursorScreenPos();
						float   winW     = ImGui::GetContentRegionAvail().x;
						float   lineH    = ImGui::GetTextLineHeight();
						float   stripH   = lineH + 4.0f;
						ImVec2  stripMax(stripMin.x + winW, stripMin.y + stripH);

						ImDrawList* dl = ImGui::GetWindowDrawList();
						dl->PushClipRect(stripMin, stripMax, true);

						ImU32 tickerColor = ImGui::ColorConvertFloat4ToU32(
							ImVec4(0.0f, 1.0f, 0.3f, 1.0f));

						float msgWidths[kTickerCount];
						float tapePositions[kTickerCount];
						float totalCycle = 0.0f;
						for (int i = 0; i < kTickerCount; ++i) {
							msgWidths[i]    = ImGui::CalcTextSize(kTickerMessages[i]).x;
							tapePositions[i] = totalCycle + winW;
							totalCycle      += winW + msgWidths[i];
						}

						float offset = fmodf(static_cast<float>(ImGui::GetTime()) * kTickerSpeed, totalCycle);

						for (int i = 0; i < kTickerCount; ++i) {
							for (int wrap = 0; wrap < 2; ++wrap) {
								float screenX = tapePositions[i] - offset
											  + (wrap == 0 ? 0.0f : totalCycle);
								if (screenX < winW + msgWidths[i] && screenX + msgWidths[i] > 0.0f) {
									dl->AddText(
										ImVec2(stripMin.x + screenX, stripMin.y + 2.0f),
										tickerColor,
										kTickerMessages[i]);
								}
							}
						}

						dl->PopClipRect();
						ImGui::Dummy(ImVec2(winW, stripH));
						ImGui::Separator();
					}
					// ── End ticker strip ─────────────────────────────────────────────────────

					float footerHeight = g_chatOpen
						? (ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing())
						: 0.0f;
					ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

					for (const auto& msg : g_chatHistory) {
						if (!msg.sender.empty() && s_mutedPlayers.count(msg.sender)) continue;

						ImGui::TextColored(ImVec4(0.3f, 0.5f, 0.3f, 1.0f), "[%s]", msg.timestamp.c_str());
						ImGui::SameLine();

						if (msg.isEmote) {
							std::string emoteText = msg.text;
							if (!emoteText.empty() && emoteText[0] == ' ') emoteText = emoteText.substr(1);
							ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "* %s %s", msg.sender.c_str(), emoteText.c_str());
						} else {
							ImGui::TextColored(SenderColor(msg.sender), "%s:", msg.sender.c_str());
							ImGui::SameLine();
							ImGui::TextWrapped("%s", msg.text.c_str());
						}

						{
							std::string imgUrl = ExtractImageUrl(msg.text);
							if (!imgUrl.empty()) {
								StartImageDownload(imgUrl);
								std::lock_guard<std::mutex> lock(s_imageMutex);
								auto it = s_imageCache.find(imgUrl);
								if (it != s_imageCache.end() && it->second.srv) {
									constexpr float kMaxW = 280.0f;
									float rawW = static_cast<float>(it->second.width);
									float dispW = rawW < kMaxW ? rawW : kMaxW;
									float dispH = static_cast<float>(it->second.height) * (dispW / static_cast<float>(it->second.width));
									ImGui::Image(reinterpret_cast<ImTextureID>(it->second.srv), ImVec2(dispW, dispH));
								} else {
									ImGui::TextDisabled("[loading...]");
								}
							}
						}
					}

					if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
						ImGui::SetScrollHereY(1.0f);

					ImGui::EndChild();

					if (g_chatOpen) {
						ImGui::Separator();
						ImGui::PushItemWidth(-60.0f);
						bool reclaimFocus = false;
						if (ImGui::InputText("##Input", g_inputBuffer, IM_ARRAYSIZE(g_inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
							HandleSendInput();
							reclaimFocus = true;
						}
						ImGui::PopItemWidth();
						ImGui::SetItemDefaultFocus();
						if (reclaimFocus || ImGui::IsWindowAppearing())
							ImGui::SetKeyboardFocusHere(-1);
						ImGui::SameLine();
						if (ImGui::Button("Send", ImVec2(50, 0)))
							HandleSendInput();
					}

					ImGui::End();
					ImGui::PopStyleVar();

					if (closeRequested)
						ToggleChat(true, false);
				}

				// ── Unread badge ─────────────────────────────────────────────────────
				if (g_chatEnabled && !g_chatOpen && s_unreadCount > 0) {
					ImGuiIO& badgeIO = ImGui::GetIO();
					float pulse = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 3.0f);
					ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.8f + 0.2f * pulse, 0.3f, 0.9f));
					ImU32 bgColor   = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.12f, 0.04f, 0.88f));

					char badgeText[32];
					snprintf(badgeText, sizeof(badgeText), "%d new", s_unreadCount);

					ImVec2 textSize = ImGui::CalcTextSize(badgeText);
					constexpr float padX = 7.0f, padY = 4.0f;
					ImVec2 badgePos(18.0f, badgeIO.DisplaySize.y - 24.0f);
					ImVec2 rectMin(badgePos.x - padX, badgePos.y - padY);
					ImVec2 rectMax(badgePos.x + textSize.x + padX, badgePos.y + textSize.y + padY);

					auto* dl = ImGui::GetForegroundDrawList();
					dl->AddRectFilled(rectMin, rectMax, bgColor, 6.0f);
					dl->AddRect(rectMin, rectMax, glowColor, 6.0f, 0, 1.5f);
					dl->AddText(badgePos, glowColor, badgeText);
				}

				ImGui::EndFrame();
				ImGui::Render();

				g_pDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			}

			return oPresent(pSwapChain, SyncInterval, Flags);
		}

		HRESULT APIENTRY Hook_ResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
		{
			if (g_pRenderTargetView) {
				g_pRenderTargetView->Release();
				g_pRenderTargetView = nullptr;
			}

			return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
		}

		void InstallHooks()
		{
			// Create a temporary D3D11 device + swap chain to read the IDXGISwapChain vtable,
			// then release both immediately. This avoids depending on BSGraphics::RendererData
			// which was removed in the NG CommonLibF4.
			DXGI_SWAP_CHAIN_DESC sd = {};
			sd.BufferCount = 1;
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = GetForegroundWindow();
			sd.SampleDesc.Count = 1;
			sd.Windowed = TRUE;
			sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

			ID3D11Device* dummyDevice = nullptr;
			IDXGISwapChain* dummySwapChain = nullptr;
			HRESULT hr = D3D11CreateDeviceAndSwapChain(
				nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
				nullptr, 0, D3D11_SDK_VERSION,
				&sd, &dummySwapChain, &dummyDevice, nullptr, nullptr);

			if (FAILED(hr) || !dummySwapChain) {
				logger::critical("Failed to create dummy D3D11 device for vtable (hr={:08X})", static_cast<uint32_t>(hr));
				if (dummyDevice) dummyDevice->Release();
				return;
			}

			void** vtable = *reinterpret_cast<void***>(dummySwapChain);
			// Copy the vtable pointers we need before releasing the dummy objects
			void* presentVtbl = vtable[8];
			void* resizeVtbl  = vtable[13];
			dummySwapChain->Release();
			dummyDevice->Release();

			if (MH_Initialize() != MH_OK) {
				logger::critical("Failed to initialize MinHook!");
				return;
			}

			if (MH_CreateHook(presentVtbl, &Hook_Present, reinterpret_cast<LPVOID*>(&oPresent)) != MH_OK) {
				logger::critical("Failed to hook IDXGISwapChain::Present");
				return;
			}

			if (MH_CreateHook(resizeVtbl, &Hook_ResizeBuffers, reinterpret_cast<LPVOID*>(&oResizeBuffers)) != MH_OK) {
				logger::critical("Failed to hook IDXGISwapChain::ResizeBuffers");
				return;
			}

			if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
				logger::critical("Failed to enable hooks");
				return;
			}
		}

		void UninstallHooks()
		{
			MH_DisableHook(MH_ALL_HOOKS);
			MH_Uninitialize();

			if (g_hwnd && oWndProc) {
				SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
			}

			if (g_pRenderTargetView) {
				g_pRenderTargetView->Release();
				g_pRenderTargetView = nullptr;
			}

			if (g_pDeviceContext) {
				g_pDeviceContext->Release();
				g_pDeviceContext = nullptr;
			}

			if (g_pDevice) {
				g_pDevice->Release();
				g_pDevice = nullptr;
			}

			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}
	}
}
