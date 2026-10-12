#ifndef MIPSYNC_SCENE_LOADER_H
#define MIPSYNC_SCENE_LOADER_H

int ps1_scene_request_load_path(const char* path);
int ps1_scene_request_load_index(int index);
void ps1_scene_process_load(void);

#endif
