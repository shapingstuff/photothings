// module_date.h
#pragma once

// Module API used by main.ino
void module_date_setup();
void module_date_activate();
void module_date_deactivate();
void module_date_loop();

void module_date_enable(bool on);
bool module_date_isEnabled();