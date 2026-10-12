#pragma once
#define EnterCriticalSection test_ps1_enter_critical
#define ExitCriticalSection test_ps1_exit_critical
int EnterCriticalSection(void);
void ExitCriticalSection(void);
void StopPAD(void);
void StartPAD(void);
void InitPAD(unsigned char*, int, unsigned char*, int);
void ChangeClearPAD(int);
