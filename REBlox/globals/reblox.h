#pragma once
#include <string>
#include <vector>
#include <iostream>

namespace reblox
{
	namespace memory
	{
		enum struct ReadWriteType
		{
			Float,
			Int,
			Double,
			Unsigned_Int,
			Uintptr_t
		};

		static bool addOffsets;
		static std::vector<uintptr_t> relativeOffsets;
		static uintptr_t baseReadWriteAddress; // like in cheat engine when you add the offsets it needs a base
		static ReadWriteType readWriteType;
	}

	namespace gui_shortcuts
	{
		bool attachShortcutPressed = false;
		bool focusOnProcessPicker = false;
	}
}