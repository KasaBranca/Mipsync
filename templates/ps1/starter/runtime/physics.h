#ifndef MIPSYNC_PHYSICS_H
#define MIPSYNC_PHYSICS_H

#include "fixedp.h"
#include <stdint.h>

/* Swept, sliding character controller for the entity currently executing a
 * script. Includes skin separation and penetration recovery. */
void ps1_physics_set_active_entity(unsigned int entity_index);
void ps1_physics_begin_frame(void);
void ps1_physics_move(fix16_t vx, fix16_t vy, fix16_t vz, fix16_t dt);
void ps1_physics_move_kinematic(fix16_t vx, fix16_t vy, fix16_t vz, fix16_t dt);
int  ps1_physics_is_grounded(void);
int  ps1_physics_is_grounded_within(fix16_t distance);

/* Sweeps a small camera volume from its look pivot to the requested position.
 * Non-convex mesh triangles are tested two-sided so Plane colliders also keep
 * the camera on the visible side. Returns non-zero when the position changed. */
int ps1_physics_camera_sweep(unsigned int target_entity_index,
                             unsigned int camera_entity_index,
                             const fix16_t from[3],
                             const fix16_t desired[3],
                             fix16_t radius,
                             fix16_t out_position[3]);

/* Collider details getters. */
int16_t ps1_physics_get_collider_shape(unsigned int entity_index);
void    ps1_physics_get_collider_center(unsigned int entity_index, fix16_t out_center[3]);
void    ps1_physics_get_collider_half_extents(unsigned int entity_index, fix16_t out_half_extents[3]);
fix16_t ps1_physics_get_collider_radius(unsigned int entity_index);
fix16_t ps1_physics_get_collider_capsule_height(unsigned int entity_index);
int     ps1_physics_is_trigger(unsigned int entity_index);

#endif /* MIPSYNC_PHYSICS_H */
