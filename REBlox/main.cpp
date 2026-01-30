#include <iostream>
#include <cstdint>
#include <thread>
#include <d3d11.h>
#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_impl_win32.h"
#include "thirdparty/imgui/imgui_impl_dx11.h"
#include "src/memory/memory.h"
#include "src/window/gui/gui.h"
#include <algorithm>
#include "globals/reblox.h"

#undef min
#undef max

#pragma comment(lib, "d3d11.lib")

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

static bool ShowProcessPicker = false;
static std::vector<reblox::memory::PE32> ProcessList;

// Memory View Variables
static uint64_t memoryViewAddress = 0;
static std::vector<uint8_t> memoryViewBuffer;
static const size_t BYTES_PER_ROW = 16;
static const size_t ROWS_TO_DISPLAY = 32;
static const size_t BUFFER_SIZE = BYTES_PER_ROW * ROWS_TO_DISPLAY;

void ResetAttachedProcess()
{
	reblox::memory::state.pid = 0;
	reblox::memory::state.proc = nullptr;
	reblox::memory::state.process_base = 0;
	ProcessList = reblox::memory::get_processes();
}

bool ReadMemoryForView(uint64_t address, size_t size, std::vector<uint8_t>& buffer)
{
	if (reblox::memory::state.proc == nullptr || reblox::memory::state.pid == 0)
		return false;

	buffer.resize(size);
	SIZE_T bytesRead = 0;

	BOOL success = ReadProcessMemory(
		reblox::memory::state.proc,
		(LPCVOID)address,
		buffer.data(),
		size,
		&bytesRead
	);

	if (!success || bytesRead == 0)
	{
		std::fill(buffer.begin(), buffer.end(), 0); // fill with "??"
		return false;
	}

	return true;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	// Thread for refreshing automatically every x seconds
	{
		std::thread([]()
			{
				while (true)
				{
					ProcessList = reblox::memory::get_processes();
					std::this_thread::sleep_for(std::chrono::seconds(3));
				}
			}
		).detach();
	}

	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"REBlox", NULL };
	RegisterClassEx(&wc);
	HWND hwnd = CreateWindow(wc.lpszClassName, L"REBlox", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateRenderTarget();
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGui::Begin("Main Window");

		enum class _tab
		{
			home,
			memory,
			memoryview
		} static tab = _tab::home;

		// Tab selection
		{
			if (ImGui::Button("Home"))
			{
				tab = _tab::home;
			}
			if (reblox::memory::state.process_base != 0)
			{
				sl;
				if (ImGui::Button("Memory"))
				{
					tab = _tab::memory;
				}
				sl;
				if (ImGui::Button("Memory View"))
				{
					tab = _tab::memoryview;
					if (memoryViewAddress == 0)
						memoryViewAddress = reblox::memory::state.process_base;
				}
			}
		}

		ImGui::Separator();

		if (tab == _tab::home)
		{
			if (ImGui::Button("Select Process"))
			{
				ShowProcessPicker = true;
				reblox::gui_shortcuts::focusOnProcessPicker = true;
				ProcessList = reblox::memory::get_processes();
			}
			sl;
			if (ImGui::Button("Detach"))
			{
				ResetAttachedProcess();
			}

			if (reblox::memory::state.pid != 0)
			{
				ImGui::Text("Attached to PID: %d", reblox::memory::state.pid);
				ImGui::Text("Base Address: 0x%llX", reblox::memory::state.process_base);
			}
			else
			{
				ImGui::Text("Status: Not Attached");
			}
		}
		else if (tab == _tab::memory)
		{
			static char addrBuf[32];
			snprintf(addrBuf, sizeof(addrBuf), "0x%llX", reblox::memory::baseReadWriteAddress);

			if (ImGui::InputText("Address", addrBuf, sizeof(addrBuf), ImGuiInputTextFlags_CharsHexadecimal))
			{
				uint64_t value = 0;

				if (sscanf_s(addrBuf, "%llx", &value) == 1)
				{
					reblox::memory::baseReadWriteAddress = value;
				}
			}

			// Adding offsets
			{
				ImGui::Dummy({ 0, 5 });
				ImGui::Checkbox("Offsets", &reblox::memory::addOffsets);

				if (reblox::memory::addOffsets)
				{
					static uintptr_t offsetValue = 0;
					ImGui::InputScalar("Offset", ImGuiDataType_U64, &offsetValue, nullptr, nullptr, "%p");
					if (ImGui::Button("Add offset"))
					{
						reblox::memory::relativeOffsets.push_back(offsetValue);
					}
					ImGui::SameLine();
					if (ImGui::Button("Remove offset"))
					{
						reblox::memory::relativeOffsets.pop_back();
					}
					ImGui::SameLine();
					if (ImGui::Button("Clear offsets"))
					{
						reblox::memory::relativeOffsets.clear();
					}

					if (!reblox::memory::relativeOffsets.empty())
					{
						int listBoxHeight = std::min((int)reblox::memory::relativeOffsets.size(), 7) * 20;

						ImGui::BeginListBox("##Offsets", ImVec2(0, listBoxHeight));

						for (size_t i = 0; i < reblox::memory::relativeOffsets.size(); ++i)
						{
							ImGui::Text("Offset %zu: %p", i, reblox::memory::relativeOffsets[i]);
						}

						ImGui::EndListBox();
					}
				}
				ImGui::Dummy(ImVec2(0, 2));
			}

			if (ImGui::Button("Read"))
			{

			}
		}
		else if (tab == _tab::memoryview)
		{
			ImGui::Text("Memory View");
			ImGui::Separator();

			// Address input
			static char addressBuf[32];
			snprintf(addressBuf, sizeof(addressBuf), "0x%llX", memoryViewAddress);

			ImGui::Text("Address:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(200);
			if (ImGui::InputText("##MemViewAddress", addressBuf, sizeof(addressBuf), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue))
			{
				uint64_t value = 0;
				if (sscanf_s(addressBuf, "%llx", &value) == 1)
				{
					memoryViewAddress = value;
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Refresh"))
			{
				ReadMemoryForView(memoryViewAddress, BUFFER_SIZE, memoryViewBuffer);
			}

			ImGui::SameLine();
			if (ImGui::Button("Go to Base"))
			{
				memoryViewAddress = reblox::memory::state.process_base;
				ReadMemoryForView(memoryViewAddress, BUFFER_SIZE, memoryViewBuffer);
			}

			ImGui::Separator();

			static uint64_t lastAddress = 0;
			if (memoryViewBuffer.empty() || lastAddress != memoryViewAddress)
			{
				ReadMemoryForView(memoryViewAddress, BUFFER_SIZE, memoryViewBuffer);
				lastAddress = memoryViewAddress;
			}

			ImGui::BeginChild("MemoryViewScroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
			ImGui::Text("Address           00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   ASCII");
			ImGui::Separator();

			for (size_t row = 0; row < ROWS_TO_DISPLAY; ++row)
			{
				uint64_t rowAddress = memoryViewAddress + (row * BYTES_PER_ROW);
				char hexLine[128] = "";
				char asciiLine[32] = "";
				ImGui::Text("%016llX", rowAddress);
				ImGui::SameLine();

				for (size_t col = 0; col < BYTES_PER_ROW; ++col)
				{
					size_t idx = row * BYTES_PER_ROW + col;

					if (idx < memoryViewBuffer.size())
					{
						uint8_t byte = memoryViewBuffer[idx];

						// cool spacing
						if (col == 8)
							strcat_s(hexLine, " ");

						char hexByte[8];
						snprintf(hexByte, sizeof(hexByte), " %02X", byte);
						strcat_s(hexLine, hexByte);

						if (byte >= 32 && byte <= 126)
							asciiLine[col] = (char)byte;
						else
							asciiLine[col] = '.';
					}
					else
					{
						strcat_s(hexLine, " ??");
						asciiLine[col] = '?';
					}
				}

				asciiLine[BYTES_PER_ROW] = '\0';

				ImGui::SameLine();
				ImGui::Text("%s", hexLine);
				ImGui::SameLine();
				ImGui::Text("  %s", asciiLine);
			}

			ImGui::PopFont();

			ImGui::EndChild();

			ImGui::Separator();
			if (ImGui::Button("Previous Page"))
			{
				if (memoryViewAddress >= BUFFER_SIZE)
				{
					memoryViewAddress -= BUFFER_SIZE;
					ReadMemoryForView(memoryViewAddress, BUFFER_SIZE, memoryViewBuffer);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Next Page"))
			{
				memoryViewAddress += BUFFER_SIZE;
				ReadMemoryForView(memoryViewAddress, BUFFER_SIZE, memoryViewBuffer);
			}
		}

		ImGui::End();

		if (ShowProcessPicker)
		{
			ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
			// using if statements if the window is resized to very small or off screen it will show errors
			//if (ImGui::Begin("Select Process", &ShowProcessPicker))
			ImGui::Begin("Select Process", &ShowProcessPicker);
			{
				static bool autoSelectPending = false;
				static char searchBuffer[256] = "";
				static int selectedIndex = -1;
				static char lastSearch[256] = "";

				ImGui::SetNextItemWidth(-1);

				// Handling shortcuts
				//if (!io.WantTextInput)
				{
					if (ImGui::IsKeyPressed(ImGuiKey_K) && ImGui::GetIO().KeyAlt)
					{
						reblox::gui_shortcuts::focusOnProcessPicker = true;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_A) && ImGui::GetIO().KeyAlt)
					{
						reblox::gui_shortcuts::attachShortcutPressed = true;
					}
				}

				// for selecting first process automatically
				if (strcmp(searchBuffer, lastSearch) != 0)
				{
					strcpy_s(lastSearch, searchBuffer);
					autoSelectPending = true;
				}

				if (reblox::gui_shortcuts::focusOnProcessPicker)
				{
					ImGui::SetKeyboardFocusHere();
					reblox::gui_shortcuts::focusOnProcessPicker = false;
				}

				ImGui::InputTextWithHint("##search", "Search processes (Alt + K = focus)", searchBuffer, IM_ARRAYSIZE(searchBuffer));

				static constexpr size_t bottomPadding = 31;

				ImGui::Separator();
				//if (ImGui::BeginChild("ProcessList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.5f)))
				ImGui::BeginChild("ProcessList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.5f));
				{
					std::string searchStr = searchBuffer;
					std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

					if (autoSelectPending)
					{
						int firstVisibleIndex = -1;
						for (int n = 0; n < ProcessList.size(); n++)
						{
							std::string name = reblox::memory::WStringToString(ProcessList[n].szExeFile);
							std::string lowerName = name;
							std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

							if (searchStr.empty() || lowerName.find(searchStr) != std::string::npos)
							{
								firstVisibleIndex = n;
								break;
							}
						}

						if (firstVisibleIndex != -1)
							selectedIndex = firstVisibleIndex;

						autoSelectPending = false;
					}

					for (int n = 0; n < ProcessList.size(); n++)
					{
						std::string name = reblox::memory::WStringToString(ProcessList[n].szExeFile);
						std::string lowerName = name;
						std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

						if (searchStr.empty() || lowerName.find(searchStr) != std::string::npos)
						{
							bool isSelected = (selectedIndex == n);
							if (ImGui::Selectable((name + " (PID: " + std::to_string(ProcessList[n].th32ProcessID) + ")").c_str(), isSelected))
							{
								selectedIndex = n;
							}

							// I don't like this because its buggy :(
							//if (isSelected)
							//	ImGui::SetScrollHereY();
						}
					}

					ImGui::EndChild();
				}

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + bottomPadding);
				ImGui::Separator();
				if ((ImGui::Button("Attach (Alt + A)") || reblox::gui_shortcuts::attachShortcutPressed) && selectedIndex != -1)
				{
					reblox::gui_shortcuts::attachShortcutPressed = false;

					if (reblox::memory::attach_to_process(ProcessList[selectedIndex].szExeFile))
					{
						ShowProcessPicker = false;
					}
					else
					{
						// Penguu why would you change this MessageBox? It obviously failed to Attach
						ResetAttachedProcess();
						MessageBoxA(0, "Failed to attach! Try Running as Admin.", "Error!", 0);
						//MessageBoxA(0, "No process selected.", "Error!", 0);
					}
				}
				ImGui::SameLine();
				// Is this nesseccary we have the X button
				/*if (ImGui::Button("Cancel"))
				{
					ShowProcessPicker = false;
				}
				ImGui::SameLine();*/
				if (ImGui::Button("Refresh List"))
				{
					ProcessList = reblox::memory::get_processes();
				}
				ImGui::End();
			}
		}

		// Rendering
		ImGui::Render();
		const float clear_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		g_pSwapChain->Present(1, 0); // Vsync
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	DestroyWindow(hwnd);
	UnregisterClass(wc.lpszClassName, wc.hInstance);
}

bool CreateDeviceD3D(HWND hWnd)
{
	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, &sd,
		&g_pSwapChain, &g_pd3dDevice,
		nullptr, &g_pd3dDeviceContext) != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}

void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_ResizeWidth = (UINT)LOWORD(lParam);
		g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
