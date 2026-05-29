#include "PCH.h"
#include "ChatClient.h"
#include <ixwebsocket/IXNetSystem.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace FalloutChat
{
	ChatClient& ChatClient::GetSingleton()
	{
		static ChatClient instance;
		return instance;
	};

	void ChatClient::Initialize(const std::string& url, const std::string& username, uint64_t steamID)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket) {
			Shutdown();
		}

		_url = url;
		_username = username;
		_steamID = steamID;

		ix::initNetSystem();

		_webSocket = std::make_unique<ix::WebSocket>();
		_webSocket->setUrl(_url);
		_webSocket->enableAutomaticReconnection();
		_webSocket->setMinWaitBetweenReconnectionRetries(1000);
		_webSocket->setMaxWaitBetweenReconnectionRetries(30000);
		_webSocket->setPingInterval(30);

		_webSocket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
			if (msg->type == ix::WebSocketMessageType::Message) {
				std::lock_guard<std::mutex> innerLock(_mutex);
				const std::string& payload = msg->str;

				if (payload.rfind("[COUNT]:", 0) == 0) {
					try { _onlineCount = std::stoi(payload.substr(8)); } catch (...) {}
					return;
				}

				if (payload.rfind("[HISTORY]", 0) == 0) {
					std::string body = payload.substr(9);
					auto pipePos = body.find('|');
					if (pipePos == std::string::npos) return;
					std::string ts   = body.substr(0, pipePos);
					std::string rest = body.substr(pipePos + 1);

					ChatMessage histMsg;
					histMsg.timestamp = ts;

					if (rest.rfind("[EMOTE]", 0) == 0) {
						std::string emoteBody = rest.substr(7);
						auto delim = emoteBody.find('\x01');
						if (delim != std::string::npos) {
							histMsg.sender = emoteBody.substr(0, delim);
							histMsg.text   = emoteBody.substr(delim + 1);
						} else {
							histMsg.text = emoteBody;
						}
						histMsg.isEmote = true;
					} else {
						auto colonPos = rest.find(':');
						if (colonPos != std::string::npos) {
							histMsg.sender = rest.substr(0, colonPos);
							histMsg.text   = rest.substr(colonPos + 1);
							if (!histMsg.text.empty() && histMsg.text[0] == ' ')
								histMsg.text = histMsg.text.substr(1);
						} else {
							histMsg.text = rest;
						}
					}
					_messageQueue.push_back(histMsg);
					return;
				}

				ChatMessage chatMsg;

				if (payload.rfind("[EMOTE]", 0) == 0) {
					std::string body = payload.substr(7);
					auto delim = body.find('\x01');
					if (delim != std::string::npos) {
						chatMsg.sender = body.substr(0, delim);
						chatMsg.text   = body.substr(delim + 1);
					} else {
						chatMsg.text = body;
					}
					chatMsg.isEmote = true;
				} else {
					size_t colonPos = payload.find(':');
					if (colonPos != std::string::npos) {
						chatMsg.sender = payload.substr(0, colonPos);
						chatMsg.text   = payload.substr(colonPos + 1);
						if (!chatMsg.text.empty() && chatMsg.text[0] == ' ')
							chatMsg.text = chatMsg.text.substr(1);
					} else {
						chatMsg.sender = "Server";
						chatMsg.text   = payload;
					}
				}

				auto now = std::chrono::system_clock::now();
				auto time_t_now = std::chrono::system_clock::to_time_t(now);
				std::tm tm_now;
				localtime_s(&tm_now, &time_t_now);
				std::ostringstream oss;
				oss << std::put_time(&tm_now, "%H:%M:%S");
				chatMsg.timestamp = oss.str();

				_messageQueue.push_back(chatMsg);
			} else if (msg->type == ix::WebSocketMessageType::Open) {
				_connected = true;
			} else if (msg->type == ix::WebSocketMessageType::Close || msg->type == ix::WebSocketMessageType::Error) {
				_connected = false;
				_disconnectedAt = std::chrono::steady_clock::now();
			}
		});

		_webSocket->start();
	}

	void ChatClient::Shutdown()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket) {
			_webSocket->stop();
			_webSocket.reset();
			ix::uninitNetSystem();
		}
		_connected = false;
	}

	void ChatClient::SendRename(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket && _connected)
			_webSocket->send("[RENAME]" + std::to_string(_steamID) + "|" + name);
	}

	void ChatClient::Send(const std::string& text, const std::string& location)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket && _connected) {
			std::string payload = std::to_string(_steamID) + "|" + _username;
			if (!location.empty()) payload += "|" + location;
			payload += ": " + text;
			_webSocket->send(payload);
		}
	}

	std::vector<ChatMessage> ChatClient::GetNewMessages()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		std::vector<ChatMessage> msgs = std::move(_messageQueue);
		_messageQueue.clear();
		return msgs;
	}

	bool ChatClient::IsConnected() const
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return _connected;
	}

	void ChatClient::SetUsername(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_username = name;
	}

	void ChatClient::SetSteamID(uint64_t id)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_steamID = id;
	}

	std::string ChatClient::GetUsername() const
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return _username;
	}

	int ChatClient::GetOnlineCount() const
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return _onlineCount;
	}

	std::chrono::steady_clock::time_point ChatClient::GetDisconnectTime() const
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return _disconnectedAt;
	}
}
