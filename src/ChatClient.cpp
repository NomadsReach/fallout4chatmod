#include "PCH.h"
#include "ChatClient.h"
#include "ChatUI.h"
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

	void ChatClient::ShutdownNoLock()
	{
		if (_webSocket) {
			_webSocket->stop();
			_webSocket.reset();
			ix::uninitNetSystem();
		}
		_connected = false;
	}

	void ChatClient::Initialize(const std::string& url, const std::string& username, uint64_t steamID)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket) {
			logger::info("ChatClient: re-initializing, shutting down existing connection");
			ShutdownNoLock();
		}

		_url = url;
		_username = username;
		_steamID = steamID;

		logger::info("ChatClient: initializing — url='{}' username='{}' steamID={}",
			url, username, steamID);

		ix::initNetSystem();

		_webSocket = std::make_unique<ix::WebSocket>();
		_webSocket->setUrl(_url);
		_webSocket->enableAutomaticReconnection();
		_webSocket->setMinWaitBetweenReconnectionRetries(1000);
		_webSocket->setMaxWaitBetweenReconnectionRetries(30000);
		_webSocket->setPingInterval(30);

		_webSocket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
			if (msg->type == ix::WebSocketMessageType::Message) {
				bool gotMessage = false;
				{
					std::lock_guard<std::mutex> innerLock(_mutex);
					const std::string& payload = msg->str;
					logger::info("ChatClient: raw message ({} bytes)", payload.size());

					if (payload.rfind("[COUNT]:", 0) == 0) {
						try {
							int prev = _onlineCount;
							_onlineCount = std::stoi(payload.substr(8));
							logger::info("ChatClient: online count {} -> {}", prev, _onlineCount);
							int newCount = _onlineCount;
							if (auto* ti = F4SE::GetTaskInterface())
								ti->AddTask([newCount]() { ChatUI::UpdateOnlineCount(newCount); });
						} catch (...) {
							logger::warn("ChatClient: failed to parse COUNT payload '{}'", payload);
						}
					} else if (payload.rfind("[HISTORY]", 0) == 0) {
						std::string body = payload.substr(9);
						auto pipePos = body.find('|');
						if (pipePos != std::string::npos) {
							std::string ts   = body.substr(0, pipePos);
							std::string rest = body.substr(pipePos + 1);

							// We removed the 30-minute cutoff so we can always show the last 20 messages
							bool tooOld = false;

							if (!tooOld) {
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
									logger::info("ChatClient: history emote from '{}' at {}", histMsg.sender, ts);
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
									logger::info("ChatClient: history msg from '{}' at {}", histMsg.sender, ts);
								}
								_messageQueue.push_back(histMsg);
								gotMessage = true;
							}
						} else {
							logger::warn("ChatClient: malformed HISTORY payload — no pipe separator");
						}
					} else {
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
							logger::info("ChatClient: emote from '{}'", chatMsg.sender);
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
							logger::info("ChatClient: chat msg from '{}'", chatMsg.sender);
						}

						auto now = std::chrono::system_clock::now();
						auto time_t_now = std::chrono::system_clock::to_time_t(now);
						std::tm tm_now;
						localtime_s(&tm_now, &time_t_now);
						std::ostringstream oss;
						oss << std::put_time(&tm_now, "%H:%M:%S");
						chatMsg.timestamp = oss.str();

						_messageQueue.push_back(chatMsg);
						gotMessage = true;
					}
				}
				// Dispatch to game thread — Invoke must not be called from WebSocket worker
				if (gotMessage) {
					if (auto* ti = F4SE::GetTaskInterface())
						ti->AddTask([]() { ChatUI::OnMessagesReceived(); });
					else
						logger::error("ChatClient: F4SE task interface unavailable, message dropped");
				}
			} else if (msg->type == ix::WebSocketMessageType::Open) {
				_connected = true;
				logger::info("ChatClient: connected to {}", _url);
			} else if (msg->type == ix::WebSocketMessageType::Close) {
				_connected = false;
				_disconnectedAt = std::chrono::steady_clock::now();
				logger::warn("ChatClient: disconnected (code={} reason='{}')", msg->closeInfo.code, msg->closeInfo.reason);
			} else if (msg->type == ix::WebSocketMessageType::Error) {
				_connected = false;
				_disconnectedAt = std::chrono::steady_clock::now();
				logger::error("ChatClient: websocket error — {}", msg->errorInfo.reason);
			}
		});

		_webSocket->start();
		logger::info("ChatClient: websocket started, connecting...");
	}

	void ChatClient::Shutdown()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		ShutdownNoLock();
	}

	void ChatClient::SendRename(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket && _connected) {
			logger::info("ChatClient: sending rename to '{}'", name);
			_webSocket->send("[RENAME]" + std::to_string(_steamID) + "|" + name);
		} else {
			logger::warn("ChatClient: SendRename skipped — not connected (ws={} connected={})",
				(void*)_webSocket.get(), _connected);
		}
	}

	void ChatClient::Send(const std::string& text, const std::string& location)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket && _connected) {
			std::string payload = std::to_string(_steamID) + "|" + _username;
			if (!location.empty()) payload += "|" + location;
			payload += ": " + text;
			logger::info("ChatClient: sending {} bytes", payload.size());
			_webSocket->send(payload);
		} else {
			logger::warn("ChatClient: Send skipped — not connected (ws={} connected={})",
				(void*)_webSocket.get(), _connected);
		}
	}

	std::vector<ChatMessage> ChatClient::GetNewMessages()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		std::vector<ChatMessage> msgs = std::move(_messageQueue);
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
