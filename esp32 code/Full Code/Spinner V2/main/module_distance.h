#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void module_distance_setup();
void module_distance_activate();
void module_distance_deactivate();
void module_distance_loop();
void module_distance_enable(bool on);
bool module_distance_isEnabled();

#ifdef __cplusplus
}
#endif