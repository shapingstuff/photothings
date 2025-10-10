// module_family.h
#pragma once

// Module API used by main.ino
void module_family_setup();       // called once after shared init
void module_family_activate();    // called when module selected (optional)
void module_family_deactivate();  // called when switching away (optional)
void module_family_loop();        // called repeatedly while active