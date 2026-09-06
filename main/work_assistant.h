#pragma once

#include "bsp_button.h"

#include <stdint.h>

void work_assistant_enter(void);
void work_assistant_exit(void);
void work_assistant_key(bsp_btn_t btn, bsp_btn_ev_t ev);
uint32_t work_assistant_screen_idle_timeout_ms(void);
