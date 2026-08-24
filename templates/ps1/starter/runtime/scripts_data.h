#ifndef MIPSYNC_SCRIPTS_DATA_H
#define MIPSYNC_SCRIPTS_DATA_H

/*
 * Symbols defined by templates/ps1/starter/generated/scripts_data.c, which
 * is regenerated every time the engine runs `Build PS1`. See
 * src/mips/PS1Export.cpp::EmitScriptsDataC. The starter prebuilt ships a
 * stub scripts_data.c whose `g_mipsync_script_count` is zero, so the
 * runtime degrades gracefully when no user scripts are present.
 */

typedef struct mipsync_script_blob {
    const char*           class_name;
    const unsigned char*  data;
    unsigned int          size;
} mipsync_script_blob;

extern const mipsync_script_blob g_mipsync_scripts[];
extern const unsigned int        g_mipsync_script_count;

#endif /* MIPSYNC_SCRIPTS_DATA_H */
