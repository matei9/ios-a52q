#pragma once
#include <stdint.h>

struct disp_state {
	int cmd_mode, found_ctl, found_sspp;
	uint32_t pp, ctl, sspp;
	uint32_t tear, ar_before, ar_after, ctl_top;
	uint32_t src_addr, src_size, stride, format;
};

extern struct disp_state disp;

void disp_init(void);
void disp_kick(void);
void disp_report(void);
