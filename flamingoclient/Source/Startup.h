#pragma once

HWND CreateOwnerWindow();

int Run(LPTSTR /*lpstrCmdLine*/ = NULL, int nCmdShow = SW_SHOWDEFAULT);

BOOL InitSocket();

void UnInitSocket();