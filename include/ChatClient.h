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
		bool isSystem{ false };
	};

	class ChatClient
	{
	public:
		static ChatClient& GetSingleton();

		void Initialize(const std::string& url, const std::string& username, const std::string& userID);
		void Shutdown();

		void Send(const std::string& text, const std::string& location = "");
		void SendRename(const std::string& name);
		void SetUsername(const std::string& name);
		std::string GetUsername() const;
		void SetUserID(const std::string& id);
		std::vector<ChatMessage> GetNewMessages();
		bool IsConnected() const;
		int  GetOnlineCount() const;

	private:
		ChatClient() = default;
		~ChatClient() { Shutdown(); }

		std::string _url;
		std::string _username;
		std::string _userID;
		bool _connected{ false };
		int  _onlineCount{ 0 };
		std::chrono::steady_clock::time_point _disconnectedAt{};
		mutable std::mutex _mutex;
		std::vector<ChatMessage> _messageQueue;

		std::unique_ptr<ix::WebSocket> _webSocket;
	};
}
