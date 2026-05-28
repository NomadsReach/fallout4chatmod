#pragma once

namespace FalloutChat
{
	namespace Renderer
	{
		void InstallHooks();
		void UninstallHooks();

		void ToggleChat(bool forceState = false, bool state = false);
		bool IsChatOpen();
	}
}
