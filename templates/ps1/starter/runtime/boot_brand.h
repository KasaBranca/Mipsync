#ifndef MIPSYNC_BOOT_BRAND_H
#define MIPSYNC_BOOT_BRAND_H
/* Fallback branding when the BIOS logo was skipped. Scene handoffs skip it. */
void ps1_boot_brand_show(void);
void ps1_boot_brand_prepare_scene_load(void);
void ps1_boot_brand_cancel_scene_load(void);
#endif
