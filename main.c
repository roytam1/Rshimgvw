#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>

extern HINSTANCE g_hInstance;

LONG ImageView_Main(HWND hwnd, LPCWSTR szFileName);

INT SHIMGVW_Main(INT argc, LPWSTR *argv)
{
	wchar_t path[MAX_PATH];
	path[0] = 0;
	OPENFILENAMEW ofn;

	if (argc == 2)
	{
		GetFullPathName(argv[1], MAX_PATH, path, NULL);
	}
	else
	{
		memset(&ofn, 0, sizeof(OPENFILENAMEW));
		ofn.lStructSize = sizeof(OPENFILENAMEW);
		ofn.hwndOwner = 0;
		ofn.lpstrFile = path;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrFilter = L"All\0*.*\0Images\0*.BMP;*.GIF;*.PNG;*.JPG;*.JPEG;*.TIF;*.TIFF;*.ICO;*.WMF;*.EMF\0";
		ofn.nFilterIndex = 2;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		if (!GetOpenFileName(&ofn))
		{
			return NO_ERROR;
		}
	}

	if (!PathFileExistsW(path))
	{
		return ERROR_PATH_NOT_FOUND;
	}

	return ImageView_Main(NULL, path);

    /*if (3 <= argc)
    {
        MessageBoxW(NULL, L"Invalid number of arguments", L"ReactOS shimgvw", MB_ICONERROR);
        return 1;
    }

    if (argc == 1)
        return ImageView_Main(NULL, NULL);

    return ImageView_Main(NULL, argv[1]);*/
}

INT WINAPI
WinMain(HINSTANCE  hInstance,
         HINSTANCE hPrevInstance,
         LPSTR     lpCmdLine,
         INT       nCmdShow)
{
    INT argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_hInstance = hInstance;

    // Initialize OLE (required for DoDragDrop)
    OleInitialize(NULL);

    INT ret = SHIMGVW_Main(argc, argv);
    LocalFree(argv);

    OleUninitialize();
    return ret;
}
