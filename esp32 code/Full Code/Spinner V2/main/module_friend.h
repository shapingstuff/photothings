// module_friend.h
#pragma once

// API exposed by the module
void module_friend_setup();       // called once after shared hardware init
void module_friend_activate();    // called when RFID selects this module
void module_friend_deactivate();  // called when switching away
void module_friend_loop();        // called repeatedly while active