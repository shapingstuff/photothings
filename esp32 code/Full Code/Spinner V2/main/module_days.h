// module_days.h
#pragma once

// Module API used by main.ino
void module_days_setup();
void module_days_activate();
void module_days_deactivate();
void module_days_loop();

void module_days_enable(bool on);
bool module_days_isEnabled();