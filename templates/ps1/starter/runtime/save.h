#ifndef MIPSYNC_SAVE_H
#define MIPSYNC_SAVE_H
#include "fixedp.h"
#include <stdint.h>
#define PS1_SAVE_ENTRIES 24
#define PS1_SAVE_KEY_BYTES 48
#define PS1_SAVE_STRING_BYTES 96
#define PS1_SAVE_BLOCK_BYTES 8192
enum { PS1_SAVE_INT=1, PS1_SAVE_FLOAT=2, PS1_SAVE_BOOL=3, PS1_SAVE_STRING=4 };
void ps1_save_init(const char* game);
int ps1_save_set(const char* key, int type, fix16_t number, const char* text);
int ps1_save_get(const char* key, int type, fix16_t* number, const char** text);
int ps1_save_write(const char* slot);
int ps1_save_read(const char* slot);
#define PS1_SAVE_SNAPSHOT_BYTES 3840
unsigned ps1_save_snapshot(void* out,unsigned capacity);
int ps1_save_restore_snapshot(const void* data,unsigned size);
int ps1_save_has_values(void);
int ps1_save_prepare_handoff(void);
void ps1_save_cancel_handoff(void);
void ps1_save_restore_handoff(void);
/* Platform boundary: BIOS memory-card files on hardware, fault-injected in tests. */
int ps1_save_card_read(const char* file, void* data, unsigned size);
int ps1_save_card_write(const char* file, unsigned offset, const void* data, unsigned size, int create);
#endif
