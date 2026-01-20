// --------------------------------------+
// System Dynamic Link Library Proxy
// by momo5502
// --------------------------------------+
//#include "StdInc.h"

#include "framework.h"
#include <helper.hpp>
#include <map>


// Static class
// --------------------------------------+
class SDLLP
{
private:
	static std::map<std::string, HINSTANCE> mLibraries;
	static const char* mTargetLibrary;
	static void	Log(const char* message, ...);
	static void	LoadLibrary(const char* library);
	static bool	IsLibraryLoaded(const char* library);
	static const char* DetermineLibrary();
	static void GetModuleDirectory(CHAR* path, size_t size);
public:
	static FARPROC GetExport(const char* function);
	static const char* GetTargetLibrary();
};

//	Class variable declarations
// --------------------------------------+
std::map<std::string, HINSTANCE> SDLLP::mLibraries;
const char* SDLLP::mTargetLibrary = nullptr;

// Get the directory of the current module
// --------------------------------------+
void SDLLP::GetModuleDirectory(CHAR* path, size_t size)
{
	GetModuleFileNameA(NULL, path, size);
	// Remove the executable filename to get just the directory
	char* lastSlash = strrchr(path, '\\');
	if (lastSlash) *(lastSlash + 1) = '\0';
}

// Determine which library to proxy
// --------------------------------------+
const char* SDLLP::DetermineLibrary()
{
	if (mTargetLibrary) return mTargetLibrary;

	Log("[SDLLP] Determining target library.");


		Log("[SDLLP] d3d9Hooked enabled in configuration.");

		// Check for d3d9Hooked.dll in the game directory
		CHAR mPath[MAX_PATH];
		GetModuleDirectory(mPath, MAX_PATH);
		strcat_s(mPath, "d3d9Hooked.dll");

		if (GetFileAttributesA(mPath) != INVALID_FILE_ATTRIBUTES)
		{
			Log("[SDLLP] d3d9Hooked.dll found in game directory, using d3d9Hooked.");
			mTargetLibrary = "d3d9Hooked.dll";
			return mTargetLibrary;
		}

		Log("[SDLLP] d3d9Hooked.dll not found, falling back to d3d9.dll.");
	

	mTargetLibrary = "d3d9.dll";
	return mTargetLibrary;
}

// Get the target library name
// --------------------------------------+
const char* SDLLP::GetTargetLibrary()
{
	return DetermineLibrary();
}

// Load necessary library
// --------------------------------------+
void SDLLP::LoadLibrary(const char* library)
{
	Log("[SDLLP] Loading library '%s'.", library);
	CHAR mPath[MAX_PATH];

	// For d3d9Hooked.dll, load from game directory
	if (strcmp(library, "d3d9Hooked.dll") == 0)
	{
		GetModuleDirectory(mPath, MAX_PATH);
		strcat_s(mPath, library);
	}
	// For d3d9.dll, load from system directory
	else
	{
		GetSystemDirectoryA(mPath, MAX_PATH);
		strcat_s(mPath, "\\");
		strcat_s(mPath, library);
	}

	mLibraries[library] = ::LoadLibraryA(mPath);
	if (!IsLibraryLoaded(library)) Log("[SDLLP] Unable to load library '%s'.", library);
}

// Check if export already loaded
// --------------------------------------+
bool SDLLP::IsLibraryLoaded(const char* library)
{
	return (mLibraries.find(library) != mLibraries.end() && mLibraries[library]);
}

// Get export address
// --------------------------------------+
FARPROC SDLLP::GetExport(const char* function)
{
	const char* library = GetTargetLibrary();
	Log("[SDLLP] Export '%s' requested from %s.", function, library);
	if (!IsLibraryLoaded(library)) LoadLibrary(library);
	FARPROC address = GetProcAddress(mLibraries[library], function);
	if (!address) Log("[SDLLP] Unable to load export '%s' from library '%s'.", function, library);
	return address;
}

// Write debug string
// --------------------------------------+
void SDLLP::Log(const char* message, ...)
{
	CHAR buffer[1024];
	va_list ap;
	va_start(ap, message);
	vsprintf_s(buffer, message, ap);
	va_end(ap);
	OutputDebugStringA(buffer);
}

// Macro to declare an export
// --------------------------------------+
#define EXPORT(_export) extern "C" __declspec(naked) __declspec(dllexport) void _export() { static FARPROC function = 0; if(!function) function = SDLLP::GetExport(__FUNCTION__); __asm { jmp function } }  

// --------------------------------------+
//	Export functions
// --------------------------------------+
EXPORT(D3DPERF_BeginEvent)
EXPORT(D3DPERF_EndEvent)
EXPORT(Direct3DCreate9)