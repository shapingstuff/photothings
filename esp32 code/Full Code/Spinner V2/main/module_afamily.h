#ifndef MODULE_AFAMILY_H
#define MODULE_AFAMILY_H

#include <Arduino.h>

void module_afamily_setup();
void module_afamily_activate();
void module_afamily_deactivate();
void module_afamily_loop();
void module_afamily_enable(bool on);
bool module_afamily_isEnabled();

#endif // MODULE_AFAMILY_H