#pragma once
typedef struct _HIDP_PREPARSED_DATA* PHIDP_PREPARSED_DATA;
struct HIDP_CAPS { USHORT Usage; USHORT UsagePage; USHORT InputReportByteLength; USHORT OutputReportByteLength; };
typedef long NTSTATUS;
#define HIDP_STATUS_SUCCESS ((NTSTATUS)0x00110000L)
extern "C" { BOOL HidD_GetPreparsedData(HANDLE, PHIDP_PREPARSED_DATA*); BOOL HidD_FreePreparsedData(PHIDP_PREPARSED_DATA); NTSTATUS HidP_GetCaps(PHIDP_PREPARSED_DATA, HIDP_CAPS*); }
