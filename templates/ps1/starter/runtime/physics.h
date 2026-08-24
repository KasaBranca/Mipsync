#ifndef MIPSYNC_PHYSICS_H
#define MIPSYNC_PHYSICS_H

#include "fixedp.h"
#include <stdint.h>

/* Simple character controller for the entity currently executing a script.
 * Integrates velocity over deltaTime, clamps to a flat floor at y=0. */
void ps1_physics_set_active_entity(unsigned int entity_index);
void ps1_physics_move(fix16_t vx, fix16_t vy, fix16_t vz, fix16_t dt);
int  ps1_physics_is_grounded(void);

/* Collider details getters. */
int16_t ps1_physics_get_collider_shape(unsigned int entity_index);
void    ps1_physics_get_collider_center(unsigned int entity_index, fix16_t out_center[3]);
void    ps1_physics_get_collider_half_extents(unsigned int entity_index, fix16_t out_half_extents[3]);
fix16_t ps1_physics_get_collider_radius(unsigned int entity_index);
fix16_t ps1_physics_get_collider_capsule_height(unsigned int entity_index);
int     ps1_physics_is_trigger(unsigned int entity_index);

#endif /* MIPSYNC_PHYSICS_H */
