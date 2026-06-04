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

	void ChatClient::Initialize(const std::string& url, const std::string& username, const std::string& userID)
	{
		// Capture the old websocket outside the lock so we can stop() it without holding _mutex.
		// stop() blocks until the bg thread exits; that thread acquires _mutex inside the message
		// handler, so stopping while holding the lock causes a deadlock.
		std::unique_ptr<ix::WebSocket> oldSocket;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_webSocket) {
				logger::info("ChatClient: re-initializing, shutting down existing connection");
				oldSocket = std::move(_webSocket);
				_connected = false;
			}
			_url = url;
			_username = username;
			_userID = userID;
		}

		if (oldSocket) {
			oldSocket->stop();
			oldSocket.reset();
			ix::uninitNetSystem();
		}

		std::lock_guard<std::mutex> lock(_mutex);

		logger::info("ChatClient: initializing — url='{}' username='{}' userID={}",
			url, username, userID);

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
								auto colonPos = rest.find(": ");
								if (colonPos != std::string::npos) {
									histMsg.sender = rest.substr(0, colonPos);
									histMsg.text   = rest.substr(colonPos + 2);
								} else {
									histMsg.text = rest;
								}
								logger::info("ChatClient: history msg from '{}' at {}", histMsg.sender, ts);
							}
							_messageQueue.push_back(histMsg);
							gotMessage = true;
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
							size_t colonPos = payload.find(": ");
							if (colonPos != std::string::npos) {
								chatMsg.sender = payload.substr(0, colonPos);
								chatMsg.text   = payload.substr(colonPos + 2);
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
				std::string connectedUrl;
				{
					std::lock_guard<std::mutex> lock(_mutex);
					_connected = true;
					connectedUrl = _url;
				}
				logger::info("ChatClient: connected to {}", connectedUrl);
				if (auto* ti = F4SE::GetTaskInterface())
					ti->AddTask([]() { ChatUI::UpdateConnectionStatus(true); });
			} else if (msg->type == ix::WebSocketMessageType::Close) {
				{
					std::lock_guard<std::mutex> lock(_mutex);
					_connected = false;
					_disconnectedAt = std::chrono::steady_clock::now();
				}
				logger::warn("ChatClient: disconnected (code={} reason='{}')", msg->closeInfo.code, msg->closeInfo.reason);
				if (auto* ti = F4SE::GetTaskInterface())
					ti->AddTask([]() { ChatUI::UpdateConnectionStatus(false); });
			} else if (msg->type == ix::WebSocketMessageType::Error) {
				{
					std::lock_guard<std::mutex> lock(_mutex);
					_connected = false;
					_disconnectedAt = std::chrono::steady_clock::now();
				}
				logger::error("ChatClient: websocket error — {}", msg->errorInfo.reason);
				if (auto* ti = F4SE::GetTaskInterface())
					ti->AddTask([]() { ChatUI::UpdateConnectionStatus(false); });
			}
		});

		_webSocket->start();
		logger::info("ChatClient: websocket started, connecting...");
	}

	void ChatClient::Shutdown()
	{
		std::unique_ptr<ix::WebSocket> oldSocket;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_webSocket) {
				oldSocket = std::move(_webSocket);
				_connected = false;
			}
		}
		if (oldSocket) {
			oldSocket->stop();
			oldSocket.reset();
			ix::uninitNetSystem();
		}
	}

	void ChatClient::SendRename(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket && _connected) {
			logger::info("ChatClient: sending rename to '{}'", name);
			_webSocket->send("[RENAME]" + _userID + "|" + name);
		} else {
			logger::warn("ChatClient: SendRename skipped — not connected (ws={} connected={})",
				(void*)_webSocket.get(), _connected);
		}
	}

	void ChatClient::Send(const std::string& text, const std::string& location)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (_webSocket && _connected) {
			std::string payload = _userID + "|" + _username;
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

	void ChatClient::SetUserID(const std::string& id)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userID = id;
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
}
