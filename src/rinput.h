#pragma once
#ifndef RINPUT_H
#define RINPUT_H

#include <windows.h>

void rinput_wm_input(LPARAM lParam);
void rinput_register();
void rinput_on_main_window_create();
void rinput_on_main_window_destory();
bool rinput_is_enabled();
void rinput_get_last_offset(long* x, long* y);
void rinput_reset_offset();
void rinput_mouse_loop();
void rinput_init();
void rinput_patch();

#endif