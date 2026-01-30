#pragma once
#include <string>
#include <cstdint>
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <DbgHelp.h>
#pragma comment (lib, "dbghelp.lib")

namespace reblox::memory {
	using PE32 = PROCESSENTRY32W;
	using ME32 = MODULEENTRY32W;

	struct {
		HANDLE proc;
		std::int32_t pid;
		std::uint64_t process_base;
	} inline state;

	inline auto get_processes( void ) -> std::vector<PE32> {
		auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		std::vector<PE32> ret;

		PE32 process_entry{};
		process_entry.dwSize = sizeof(PE32);

		if (!Process32First(snapshot, &process_entry)) goto cleanup;
		do {
			ret.push_back(process_entry);
		} while (Process32Next(snapshot, &process_entry));

	cleanup:
		CloseHandle(snapshot);
		return ret;
	}

	inline auto get_pid( std::wstring str ) -> std::int32_t {
		for (auto& process : get_processes()) {
			if (str == process.szExeFile) {
				return static_cast<std::int32_t>(process.th32ProcessID);
			}
		}

		return 0;
	}

	inline auto open_process( std::int32_t pid ) -> HANDLE {
		return OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, static_cast<DWORD>(pid)); // i think?
	}

	inline auto get_module_base(std::wstring mod) -> std::uint64_t {
		auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, state.pid);
		std::uint64_t ret = 0;

		MODULEENTRY32W module_entry{};
		module_entry.dwSize = sizeof(MODULEENTRY32W);

		if (!Module32First(snapshot, &module_entry)) goto cleanup;
		do {
			if (mod == module_entry.szModule) {
				ret = reinterpret_cast<std::uint64_t>(module_entry.modBaseAddr);
				goto cleanup;
			}
		} while (Module32Next(snapshot, &module_entry));

	cleanup:
		CloseHandle(snapshot);
		return ret;
	}

	inline auto attach_to_process(std::wstring process_name) -> bool {
		DWORD pid = get_pid(process_name);
		state.pid = static_cast<std::int32_t>(pid);
		state.proc = open_process(pid);
		if (state.proc == nullptr) return false;

		state.process_base = get_module_base(process_name);
		return state.process_base != 0;
	}

	// UD Trust
	inline std::string WStringToString(const std::wstring& wstr) {
		if (wstr.empty()) return std::string();
		int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
		std::string strTo(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
		return strTo;
	} // 0x108 why is this in a namespace dedicated to interacting with external processes

	template <typename t>
	inline t read_memory( std::uint64_t address ) {
		t buffer{};
		ReadProcessMemory(state.proc, reinterpret_cast<LPCVOID>(address), &buffer, sizeof(t), nullptr);
		return buffer;
	}

	template <>
	inline std::string read_memory<std::string>( std::uint64_t address ) {
		std::string buffer;
		char temp[256];
		ReadProcessMemory(state.proc, reinterpret_cast<LPCVOID>(address), &temp, sizeof(temp), nullptr);
		temp[255] = '\0'; // fix warning | would crash (?) on std::string initializer in case of no null terminator
		buffer = std::string(temp);
		return buffer;
	}

	template <typename t>
	inline bool write_memory( std::uint64_t address, t value ) {
		return WriteProcessMemory(state.proc, reinterpret_cast<LPVOID>(address), &value, sizeof(t), nullptr);
	}

	template <>
	inline bool write_memory<std::string>( std::uint64_t address, std::string value ) {
		return WriteProcessMemory(state.proc, reinterpret_cast<LPVOID>(address), value.c_str(), value.size() + 1, nullptr);
	}

	namespace rtti {
		// I guess ill leak da rtti schtuff... ref: https://www.lukaszlipski.dev/post/rtti-msvc/
		typedef const struct _s_RTTICompleteObjectLocator // I think its nice to keep the name fucked on more internal things in the codebase. Same reason for things like reinterpret_cast being so verbose
		{ //                                                                    ^ and im too lazy to change it :P
			unsigned long signature;
			unsigned long offset;
			unsigned long cdOffset;
			int           pTypeDescriptor;
			int           pClassDescriptor;
			int           pSelf;
		};

		typedef struct TypeDescriptor
		{
			const void* pVFTable;
			void* spare;
			//char        name[]; //needs to be done dynamically(ish)
		};

		inline std::string get_mangled_object_name(std::uint64_t object_addr) { // C++ needs a verbose parent namespace access operator
			if (!state.proc || !object_addr)
				return {};

			std::uint64_t vfptr = read_memory<std::uint64_t>(object_addr);
			if (!vfptr)
				return {};

			std::uint64_t col_ptr_addr = vfptr - sizeof(std::uint64_t);
			std::uint64_t col_addr = read_memory<std::uint64_t>(col_ptr_addr);
			if (!col_addr)
				return {};

			_s_RTTICompleteObjectLocator col =
				read_memory<_s_RTTICompleteObjectLocator>(col_addr);

			if (col.signature != 1)
				return {};

			std::uint64_t image_base = col_addr - col.pSelf;
			if (!image_base)
				return {};

			std::uint64_t type_desc_addr = image_base + col.pTypeDescriptor;
			if (!type_desc_addr)
				return {};

			TypeDescriptor type_desc =
				read_memory<TypeDescriptor>(type_desc_addr);

			std::uint64_t name_addr = type_desc_addr + sizeof(TypeDescriptor);
			return read_memory<std::string>(name_addr);
		}

		inline std::string demangle_msvc_rtti(const std::string& name)
		{
			char buffer[1024]{};

			if (UnDecorateSymbolName(
				name.c_str(),
				buffer,
				sizeof(buffer),
				UNDNAME_NAME_ONLY))
			{
				return buffer;
			}

			return name;
		}

		inline std::tuple<std::string, std::string> get_object_name(std::uint64_t object_addr) { // [0] = mangled, [1] = demangled
			std::string mangled = get_mangled_object_name(object_addr);

			return std::make_tuple(mangled, demangle_msvc_rtti(mangled));
		}
	}
}
