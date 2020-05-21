#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"
#include "Utils/Trampoline.h"

#include <ShlObj.h>
#include <string_view>

#if _DEBUG

#define DEBUG_DOCUMENTS_PATH	0

#else

#define DEBUG_DOCUMENTS_PATH	0

#endif


namespace UTF8PathFixes
{
	static std::wstring UTF8ToWchar(std::string_view text)
	{
		std::wstring result;

		const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), text.size(), nullptr, 0);
		if ( count != 0 )
		{
			result.resize(count);
			MultiByteToWideChar(CP_UTF8, 0, text.data(), text.size(), result.data(), count);
		}

		return result;
	}

	static std::string WcharToUTF8(std::wstring_view text)
	{
		std::string result;

		const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), text.size(), nullptr, 0, nullptr, nullptr);
		if ( count != 0 )
		{
			result.resize(count);
			WideCharToMultiByte(CP_UTF8, 0, text.data(), text.size(), result.data(), count, nullptr, nullptr);
		}

		return result;
	}

	size_t wcstombs_UTF8(char* dest, const wchar_t* src, size_t max)
	{
		return static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, static_cast<int>(max), nullptr, nullptr)) - 1;
	}

	BOOL WINAPI CreateDirectoryUTF8(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
	{
		return CreateDirectoryW(UTF8ToWchar(lpPathName).c_str(), lpSecurityAttributes);
	}

	DWORD WINAPI GetFileAttributesUTF8(LPCSTR lpFileName)
	{
		return GetFileAttributesW(UTF8ToWchar(lpFileName).c_str());
	}

	BOOL WINAPI SetFileAttributesUTF8(LPCSTR lpFileName, DWORD dwFileAttributes)
	{
		return SetFileAttributesW(UTF8ToWchar(lpFileName).c_str(), dwFileAttributes);
	}

	HANDLE WINAPI CreateFileUTF8(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
				DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
	{
		return CreateFileW(UTF8ToWchar(lpFileName).c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
	}

	static void FileDataWToFileDataA(const WIN32_FIND_DATAW* src, WIN32_FIND_DATAA* dest)
	{
		dest->dwFileAttributes = src->dwFileAttributes;
		dest->ftCreationTime = src->ftCreationTime;
		dest->ftLastAccessTime = src->ftLastAccessTime;
		dest->ftLastWriteTime = src->ftLastWriteTime;
		dest->nFileSizeHigh = src->nFileSizeHigh;
		dest->nFileSizeLow = src->nFileSizeLow;
		dest->dwReserved0 = src->dwReserved0;
		dest->dwReserved1 = src->dwReserved1;

		WideCharToMultiByte(CP_UTF8, 0, src->cFileName, -1, dest->cFileName, sizeof(dest->cFileName), nullptr, nullptr);
		WideCharToMultiByte(CP_UTF8, 0, src->cAlternateFileName, -1, dest->cAlternateFileName, sizeof(dest->cAlternateFileName), nullptr, nullptr);
	}

	HANDLE WINAPI FindFirstFileUTF8(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
	{
		WIN32_FIND_DATAW findData;
		HANDLE result = FindFirstFileW(UTF8ToWchar(lpFileName).c_str(), &findData);

		if ( result != INVALID_HANDLE_VALUE )
		{
			FileDataWToFileDataA(&findData, lpFindFileData);
		}

		return result;
	}

	BOOL WINAPI FindNextFileUTF8(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
	{
		WIN32_FIND_DATAW findData;
		BOOL result = FindNextFileW(hFindFile, &findData);
		
		if ( result != FALSE )
		{
			FileDataWToFileDataA(&findData, lpFindFileData);
		}

		return result;
	}

	int WINAPI MultiByteToWideChar_UTF8(UINT /*CodePage*/, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
	{
		return MultiByteToWideChar(CP_UTF8, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
	}
}

#if DEBUG_DOCUMENTS_PATH
HRESULT WINAPI SHGetKnownFolderPath_Fake(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath)
{
	if ( rfid == FOLDERID_Documents )
	{
		constexpr wchar_t debugPath[] = L"H:\\ŻąłóРстуぬねのはen\\Documents";
		*ppszPath = static_cast<PWSTR>(CoTaskMemAlloc( sizeof(debugPath) ));
		memcpy( *ppszPath, debugPath, sizeof(debugPath) );
		return S_OK;
	}

	if ( rfid == FOLDERID_LocalAppData )
	{
		constexpr wchar_t debugPath[] = L"H:\\ŻąłóРстуぬねのはen\\AppData\\Local";
		*ppszPath = static_cast<PWSTR>(CoTaskMemAlloc( sizeof(debugPath) ));
		memcpy( *ppszPath, debugPath, sizeof(debugPath) );
		return S_OK;
	}

	return SHGetKnownFolderPath(rfid, dwFlags, hToken, ppszPath);
}
#endif

static void RedirectImports()
{
	// Redirects:
	// MessageBoxA -> MessageBoxJIS

	const DWORD_PTR instance = reinterpret_cast<DWORD_PTR>(GetModuleHandle(nullptr));
	const PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(instance + reinterpret_cast<PIMAGE_DOS_HEADER>(instance)->e_lfanew);

	// Find IAT
	PIMAGE_IMPORT_DESCRIPTOR pImports = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(instance + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

	for ( ; pImports->Name != 0; pImports++ )
	{
#if DEBUG_DOCUMENTS_PATH
		if ( _stricmp(reinterpret_cast<const char*>(instance + pImports->Name), "shell32.dll") == 0 )
		{
			assert ( pImports->OriginalFirstThunk != 0 );

			const PIMAGE_THUNK_DATA pFunctions = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pImports->OriginalFirstThunk);

			for ( ptrdiff_t j = 0; pFunctions[j].u1.AddressOfData != 0; j++ )
			{
				if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "SHGetKnownFolderPath") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = SHGetKnownFolderPath_Fake;
				}
			}
			
		}
#endif

		if ( _stricmp(reinterpret_cast<const char*>(instance + pImports->Name), "kernel32.dll") == 0 )
		{
			assert ( pImports->OriginalFirstThunk != 0 );

			const PIMAGE_THUNK_DATA pFunctions = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pImports->OriginalFirstThunk);

			for ( ptrdiff_t j = 0; pFunctions[j].u1.AddressOfData != 0; j++ )
			{
				if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "CreateDirectoryA") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = UTF8PathFixes::CreateDirectoryUTF8;
				}
				else if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "GetFileAttributesA") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = UTF8PathFixes::GetFileAttributesUTF8;
				}
				else if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "SetFileAttributesA") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = UTF8PathFixes::SetFileAttributesUTF8;
				}
				else if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "CreateFileA") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = UTF8PathFixes::CreateFileUTF8;
				}
				else if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "FindFirstFileA") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = UTF8PathFixes::FindFirstFileUTF8;
				}
				else if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "FindNextFileA") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = UTF8PathFixes::FindNextFileUTF8;
				}
			}
			
		}
	}
}


void OnInitializeHook()
{
	using namespace Memory::VP;
	using namespace hook;

	RedirectImports();

	// Convert UTF-16 to UTF-8 properly
	{
		using namespace UTF8PathFixes;

		auto wcstombs_pattern = pattern( "41 B8 ? ? ? ? E8 ? ? ? ? 48 8B C7 40 38 7C 05 50" );
		if ( wcstombs_pattern.count(1).size() == 1 )
		{
			auto match = wcstombs_pattern.get_first( 6 );
			Trampoline* trampoline = Trampoline::MakeTrampoline( match );
			InjectHook( match, trampoline->Jump(wcstombs_UTF8) );
		}

		auto multiByteToWideChar_pattern = pattern( "41 C6 06 2A" );
		if ( multiByteToWideChar_pattern.count(1).size() == 1 )
		{
			auto match = multiByteToWideChar_pattern.get_first( 4 + 2 );
			Trampoline* trampoline = Trampoline::MakeTrampoline( match );

			void** funcPtr = trampoline->Pointer<void*>();
			*funcPtr = &MultiByteToWideChar_UTF8;
			WriteOffsetValue( match, funcPtr );
		}
	}

	// Route all functions to their UTF-8 flavors
	{
		auto utf8CP = pattern( "41 BE E9 FD 00 00" ).count(4);
		utf8CP.for_each_result( []( pattern_match match ) {
			Nop( match.get<void>( 6 + 4 ), 2 );
		} );
	}
}