/*
 * Keep a DSI *command mode* panel (a52q: S6E3FC3) showing the cont-splash
 * framebuffer. In command mode the MDP only sends a frame when kicked, so
 * whatever we draw stays invisible after ABL's last splash frame. ABL leaves
 * the pipe configured (SSPP -> LM -> PP with tear check -> INTF), so turning
 * on the pingpong's autorefresh (one frame per panel TE) plus one CTL_START
 * makes the hardware refetch the framebuffer on every vsync by itself.
 *
 * On a video mode panel (sweet) no pingpong has tear check enabled and this
 * does nothing. Register offsets: msm sde_hw_{ctl,pingpong,sspp}.c and the
 * live DT (qcom,sde-*-off) of atoll.
 */
#include <stdint.h>
#include "console.h"
#include "display.h"

#define MDP_BASE		0x0ae00000UL
#define CTL_OFF(n)		(0x2000UL + (n) * 0x200UL)
#define NR_CTL			3
#define CTL_TOP			0x014
#define CTL_FLUSH		0x018
#define CTL_START		0x01c
#define CTL_INTF_ACTIVE		0x0f4
#define PP_OFF(n)		(0x71000UL + (n) * 0x800UL)
#define NR_PP			2
#define PP_TEAR_CHECK_EN	0x000
#define PP_AUTOREFRESH_CONFIG	0x030
#define PP_LINE_COUNT		0x02c
#define NR_SSPP			4
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE0	0x24
#define SSPP_SRC_FORMAT		0x30

static const uint32_t sspp_off[NR_SSPP] = { 0x5000, 0x25000, 0x27000, 0x29000 };

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

struct disp_state disp;

void disp_init(void)
{
	for (int i = 0; i < NR_SSPP; i++) {
		uintptr_t s = MDP_BASE + sspp_off[i];
		uint32_t addr = rd32(s + SSPP_SRC0_ADDR);
		if (addr >= FB_BASE && addr < FB_BASE + 0x01000000UL) {
			disp.sspp = i;
			disp.src_addr = addr;
			disp.src_size = rd32(s + SSPP_SRC_SIZE);
			disp.stride = rd32(s + SSPP_SRC_YSTRIDE0);
			disp.format = rd32(s + SSPP_SRC_FORMAT);
			disp.found_sspp = 1;
			break;
		}
	}
	for (int i = 0; i < NR_CTL; i++) {
		uintptr_t c = MDP_BASE + CTL_OFF(i);
		if (rd32(c + CTL_TOP) || rd32(c + CTL_INTF_ACTIVE)) {
			disp.ctl = i;
			disp.ctl_top = rd32(c + CTL_TOP);
			disp.found_ctl = 1;
			break;
		}
	}
	for (int i = 0; i < NR_PP; i++) {
		uintptr_t p = MDP_BASE + PP_OFF(i);
		uint32_t te = rd32(p + PP_TEAR_CHECK_EN);
		if (te & 1) {
			disp.pp = i;
			disp.tear = te;
			disp.ar_before = rd32(p + PP_AUTOREFRESH_CONFIG);
			disp.cmd_mode = 1;
			break;
		}
	}
	if (!disp.cmd_mode || !disp.found_ctl)
		return;
	wr32(MDP_BASE + PP_OFF(disp.pp) + PP_AUTOREFRESH_CONFIG, (1U << 31) | 1);
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(MDP_BASE + CTL_OFF(disp.ctl) + CTL_START, 1);
	__asm__ volatile("dsb sy" ::: "memory");
	disp.ar_after = rd32(MDP_BASE + PP_OFF(disp.pp) + PP_AUTOREFRESH_CONFIG);
}

/* One extra frame; harmless with autorefresh running. */
void disp_kick(void)
{
	if (disp.cmd_mode && disp.found_ctl) {
		__asm__ volatile("dsb sy" ::: "memory");
		wr32(MDP_BASE + CTL_OFF(disp.ctl) + CTL_START, 1);
	}
}

void disp_report(void)
{
	con_hex2("DISP", "cmd=", disp.cmd_mode, "pp=", disp.pp);
	con_hex2(" tear", "en=", disp.tear, "lines=", rd32(MDP_BASE + PP_OFF(disp.pp) + PP_LINE_COUNT));
	con_hex2(" auto", "was=", disp.ar_before, "now=", disp.ar_after);
	con_hex2(" ctl", "n=", disp.found_ctl ? disp.ctl : 0xff, "top=", disp.ctl_top);
	con_hex2(" sspp", "n=", disp.found_sspp ? disp.sspp : 0xff, "addr=", disp.src_addr);
	con_hex2(" sspp", "size=", disp.src_size, "stride=", disp.stride);
	con_hex2(" sspp", "fmt=", disp.format, 0, 0);
}
