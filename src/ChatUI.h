#pragma once

#include "PrismaUI_F4_API.h"
#include <string>

namespace FalloutChat
{
	namespace ChatUI
	{
		void Initialize();
		void CreateView();
		
		void ToggleChat();
		bool IsChatOpen();
		
		// Called by ChatClient when new messages arrive
		void OnMessagesReceived();

		// Called by ChatClient when online count changes
		void UpdateOnlineCount(int count);

		// Called by ChatClient when connection state changes
		void UpdateConnectionStatus(bool connected);
	}
}
