#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

HWND create_worker_window()
{
	HWND progman = FindWindow(L"Progman", nullptr);

	// Send magical create-wallpaper-worker-thread-that-we-can-hijack message
	SendMessage(progman, 0x052C, NULL, NULL);

	HWND worker = nullptr;

	// Enum over all windows until we find our newly created worker window
	EnumWindows([](HWND window, LPARAM lparam) -> BOOL
	{
		auto defview = FindWindowEx(window, nullptr, L"SHELLDLL_DefView", nullptr);

		if (defview != nullptr)
		{
			auto &worker = *reinterpret_cast<HWND*>(lparam);
			worker = FindWindowEx(nullptr, window, L"WorkerW", nullptr);
			return false;
		}

		return true;
	}, reinterpret_cast<LPARAM>(&worker));

	if (worker == nullptr)
		throw std::runtime_error("failed to locate worker window");

	return worker;
}

HWND get_window_from_process(DWORD process)
{
	struct handle_data
	{
		DWORD process;
		HWND window;
	} data{ process, nullptr };

	// Loop over all windows until we find one belonging
	// to data.process, and then set data.window and exit.
	EnumWindows([](HWND window, LPARAM lparam) -> BOOL
	{
		DWORD process = 0;
		GetWindowThreadProcessId(window, &process);

		if (process != reinterpret_cast<handle_data*>(lparam)->process)
			return true;

		reinterpret_cast<handle_data*>(lparam)->window = window;
		return false;
	}, reinterpret_cast<LPARAM>(&data));

	return data.window;
}

std::pair<HWND, HANDLE> create_love_window(const std::string &game_folder)
{
	STARTUPINFOA info{};
	PROCESS_INFORMATION pi{};

	std::string cmd = "love.exe " + game_folder;

	if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, false, NORMAL_PRIORITY_CLASS, nullptr, nullptr, &info, &pi))
	{
		throw std::runtime_error("Failed to create love.exe instance");
	}

	// The LÖVE app needs to launch before we acquire the HWND handle,
	// but I can't find any way to check that, so random sleep it is.
	Sleep(1000);

	HWND love_window = get_window_from_process(GetProcessId(pi.hProcess));
	if (love_window == nullptr)
		throw std::runtime_error("couldn't find LÖVE window handle");

	CloseHandle(pi.hThread);
	return{ love_window, pi.hProcess };
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{	
	try
	{
		auto love = create_love_window(lpCmdLine);		
		HWND worker = create_worker_window();

		// Remove all niceness (toolbars, titles, close, minimize, etc.)
		SetWindowLong(love.first, GWL_STYLE, 0);

		RECT size;
		GetWindowRect(worker, &size);

		// The window must be resized before parenting or the ShowWindow call later
		// will crash the LÖVE instance and kill Aero. I have no idea why.
		// Passing in SWP_NOSIZE instead of NULL at the end, will demonstrate this.
		SetWindowPos(love.first, HWND_TOPMOST, 0, 0, size.right, size.bottom, NULL);

		auto parent = SetParent(love.first, worker);
		if (parent == nullptr)
			throw std::runtime_error("couldn't parent the LÖVE window to the worker");

		ShowWindow(love.first, SW_SHOWMAXIMIZED);

		// Wait for the LÖVE instance to die
		WaitForSingleObject(love.second, INFINITE);
		CloseHandle(love.second);
	}
	catch (std::exception &e)
	{
		return -1;
	}
	return 0;
}