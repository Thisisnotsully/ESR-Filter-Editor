#pragma once
#define GET_X_LPARAM(lp) ((int)(short)(WORD)(((DWORD_PTR)(lp)) & 0xffff))
#define GET_Y_LPARAM(lp) ((int)(short)(WORD)((((DWORD_PTR)(lp)) >> 16) & 0xffff))
