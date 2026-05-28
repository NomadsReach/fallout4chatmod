#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>

namespace FalloutChat
{
	struct ChatMessage
	{
		std::string sender;
		std::string text;
		std::string timestamp;
		bool isEmote{ false };
	};

	class ChatClient
	{
	public:
		static ChatClient& GetSingleton();

		void Initialize(const std::string& url, const std::string& username, uint64_t steamID);
		void Shutdown();

		void Send(const std::string& text, const std::string& location = "");
		void SendRename(const std::string& name);
		void SetUsername(const std::string& name);
		std::string GetUsername() const;
		void SetSteamID(uint64_t id);
		std::vector<ChatMessage> GetNewMessages();
		bool IsConnected() const;
		int  GetOnlineCount() const;
		std::chrono::steady_clock::time_point GetDisconnectTime() const;

	private:
		ChatClient() = default;
		~ChatClient() { Shutdown(); }

		std::string _url;
		std::string _username;
		uint64_t _steamID{ 0 };
		bool _connected{ false };
		int  _onlineCount{ 0 };
		std::chrono::steady_clock::time_point _disconnectedAt{};
		mutable std::mutex _mutex;
		std::vector<ChatMessage> _messageQueue;

		std::unique_ptr<ix::WebSocket> _webSocket;
	};
}
