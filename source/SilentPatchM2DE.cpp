#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"
#include "Utils/Trampoline.h"

#include <ShlObj.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <string_view>

#pragma comment(lib, "Shlwapi.lib")

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "Comctl32.lib")

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

namespace EmergencySaveMigration
{
	std::string& TrimZeros( std::string& str )
	{
		auto pos = str.find_last_not_of( '\0' );
		if ( pos == std::string::npos )
		{
			str.clear();
		}
		else
		{
			str.erase( pos + 1 );
		}
		return str;
	}

	std::wstring& TrimZeros( std::wstring& str )
	{
		auto pos = str.find_last_not_of( L'\0' );
		if ( pos == std::string::npos )
		{
			str.clear();
		}
		else
		{
			str.erase( pos + 1 );
		}
		return str;
	}


	static size_t (*orgWcstombs)(char* dest, const wchar_t* src, size_t max);
	static void TryToRescueSaves(const char* wrongPath, const wchar_t* correctPath)
	{
		// Prepare save paths for both
		std::string wrongSavePath(MAX_PATH, '\0');		
		PathCombineA(wrongSavePath.data(), wrongPath, "My Games");
		PathAppendA(wrongSavePath.data(), "Mafia II Definitive Edition");
		TrimZeros(wrongSavePath);

		std::wstring correctSavePath(MAX_PATH, '\0');
		PathCombineW(correctSavePath.data(), correctPath, L"My Games");
		PathAppendW(correctSavePath.data(), L"Mafia II Definitive Edition");
		TrimZeros(correctSavePath);

		// Try to figure out if both directories are identical (either same path or pointing to the same directory via a hard link)
		bool sourceExists = false;
		bool destinationExists = false;
		if ( HANDLE sourceDirectory = CreateFileA( wrongSavePath.c_str(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr ); sourceDirectory != INVALID_HANDLE_VALUE )
		{
			// Source exists so we have something to move
			sourceExists = true;
			if ( HANDLE destinationDirectory = CreateFileW( correctSavePath.c_str(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr ); destinationDirectory != INVALID_HANDLE_VALUE )
			{	
				destinationExists = true;
				CloseHandle( destinationDirectory );
			}

			CloseHandle( sourceDirectory );
		}


		if ( sourceExists && !destinationExists )
		{
			auto fnDialogFunc = [] ( HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR ) -> HRESULT
			{
				if ( msg == TDN_CREATED )
				{
					HMODULE gameModule = GetModuleHandle( nullptr );
					if ( HICON icon = LoadIcon( gameModule, MAKEINTRESOURCE(101) ); icon != nullptr )
					{
						SendMessage( hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon) );
					}
				}

				return S_OK;
			};

			// "Lazily" expanded, as if we reach this place we know it's "broken"
			std::wstring wideWrongSavePath(wrongSavePath.begin(), wrongSavePath.end());

			std::wstring contentString;
			contentString.append( L"SilentPatch found your save games in the following location:\n\n" );
			contentString.append( wideWrongSavePath.c_str() );
			contentString.append( L"\n\nThis does not appear to be your real Documents directory. "
									L"Do you wish to relocate them to the following directory instead?\n\n" );
			contentString.append( correctSavePath.c_str() );
			contentString.append( L"\n\nThis way saves will be located in your real Documents directory and "
									L"will be visible in the game both with SilentPatch and/or when the official "
									L"patch fixes this bug." );

			TASKDIALOGCONFIG dialogConfig { sizeof(dialogConfig) };
			dialogConfig.dwFlags = TDF_CAN_BE_MINIMIZED;
			dialogConfig.dwCommonButtons = TDCBF_YES_BUTTON|TDCBF_NO_BUTTON;
			dialogConfig.pszWindowTitle = L"SilentPatch";
			dialogConfig.pszContent = contentString.c_str();
			dialogConfig.nDefaultButton = IDYES;
			dialogConfig.pszMainIcon = TD_INFORMATION_ICON;
			dialogConfig.pfCallback = fnDialogFunc;

			int buttonResult;
			if ( SUCCEEDED(TaskDialogIndirect( &dialogConfig, &buttonResult, nullptr, nullptr )) )
			{
				if ( buttonResult == IDYES )
				{
					// Copying just to be sure it's double null terminated at the end
					auto stringToDoubleNullTerminated = []( std::wstring str )
					{
						str.append( 2, L'\0' );
						return str;
					};

					const std::wstring source = stringToDoubleNullTerminated( wideWrongSavePath );
					const std::wstring destination = stringToDoubleNullTerminated( correctSavePath );

					SHFILEOPSTRUCTW moveOp = {};
					moveOp.wFunc = FO_MOVE;
					moveOp.fFlags = FOF_NOCONFIRMMKDIR;
					moveOp.pFrom = source.c_str();
					moveOp.pTo = destination.c_str();
					const int moveResult = SHFileOperationW( &moveOp );
					if ( moveResult == 0 )
					{
						if ( moveOp.fAnyOperationsAborted == FALSE )
						{
							// Try to delete the original Documents directory (and its parent directories) - it's likely empty now
							do
							{
								PathRemoveFileSpecW( wideWrongSavePath.data() );
							}
							while ( RemoveDirectoryW( wideWrongSavePath.c_str() ) != FALSE );
						}
						else
						{
							MessageBoxW( nullptr, L"Move operation has been aborted by the user.\n\n"
													L"Please verify that all your saves are still present in the source folder - if not, move them back from the destination folder.",
													L"SilentPatch", MB_OK|MB_ICONERROR|MB_SETFOREGROUND );
						}
					}
					else
					{
						MessageBoxW( nullptr, L"Move operation failed.\n\n"
							L"Please verify that all your saves are still present in the source folder - if not, move them back from the destination folder.",
										
							L"SilentPatch", MB_OK|MB_ICONERROR|MB_SETFOREGROUND );
					}
				}
			}
		}
	}

	size_t wcstombs_RescueSavesAndConvertProperly(char* dest, const wchar_t* src, size_t max)
	{
		// First convert the path properly
		size_t result = static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, static_cast<int>(max), nullptr, nullptr)) - 1;

		// Now try to figure out if saves need to be "rescued"
		// Check if the "wrong" path to Documents\My Games\Mafia II Definitive Edition exists (use ANSI functions to replicate stock behaviour properly)
		// If it does, see if it's different to the "correct" path - if it's either different or the "correct" path doesn't exist, offer the player to move files
		// Delete the "wrong" directories without doing it recursively, so it only tries to wipe empty directories
		char wrongDocumentsPath[MAX_PATH] {};
		orgWcstombs(wrongDocumentsPath, src, MAX_PATH);
		TryToRescueSaves(wrongDocumentsPath, src);

		return result;
	}
}

int WINAPI SHFileOperationW_NullTerminate(LPSHFILEOPSTRUCTW lpFileOp)
{
	// Fixes pTo/pFrom strings not having a double null terminator at the end
	// I -know- that the game puts those variables on the stack, so I can cast constness away and fix them in place
	auto fixupNullTerminator = []( PCZZWSTR s )
	{
		PZZWSTR str = const_cast<PZZWSTR>(s);
		const size_t len = wcslen(str);

		str[len+1] = L'\0';
	};

	fixupNullTerminator(lpFileOp->pFrom);
	fixupNullTerminator(lpFileOp->pTo);
	return SHFileOperationW(lpFileOp);
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
		if ( _stricmp(reinterpret_cast<const char*>(instance + pImports->Name), "shell32.dll") == 0 )
		{
			assert ( pImports->OriginalFirstThunk != 0 );

			const PIMAGE_THUNK_DATA pFunctions = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pImports->OriginalFirstThunk);

			for ( ptrdiff_t j = 0; pFunctions[j].u1.AddressOfData != 0; j++ )
			{
				if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "SHFileOperationW") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = SHFileOperationW_NullTerminate;
				}

#if DEBUG_DOCUMENTS_PATH
				if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pFunctions[j].u1.AddressOfData)->Name, "SHGetKnownFolderPath") == 0 )
				{
					void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
					*pAddress = SHGetKnownFolderPath_Fake;
				}
#endif
			}
			
		}

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
			// Convert the path properly AND include an emergency dialog box to move files from the wrong save directory
			// for those intelligent people who worked around the save bug by running as admin...
			using namespace EmergencySaveMigration;

			auto match = wcstombs_pattern.get_first( 6 );
			Trampoline* trampoline = Trampoline::MakeTrampoline( match );
			ReadCall( match, orgWcstombs );
			InjectHook( match, trampoline->Jump(wcstombs_RescueSavesAndConvertProperly) );
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