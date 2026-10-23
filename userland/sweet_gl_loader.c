/*
 * libsweet-gl.dylib: inserted into backboardd (DYLD_INSERT_LIBRARIES in its launchd job, see
 * tools/a52q-install-gl.sh), it loads the OpenGL ES bridge behind iOS's EAGLContext so that
 * CoreAnimation's render server draws through Mesa's freedreno on the Adreno 618 instead of its own
 * software rasterizer.
 *
 * The bridge (/redwood/lib/libredwood-gl.dylib, userland/src/eagl.c) and Mesa
 * (/redwood/lib/libOSMesa.8.dylib) are HackiOS's, by OOSEX GANG,
 * https://github.com/oosexgang/hackios-xnu-userland (MIT), built by userland/build-gl.sh. HackiOS's
 * own injected library (libredwood.dylib) does more in backboardd -- window server fixes at iOS 16.5
 * shared-cache addresses, its own touch input -- which this system (iOS 16.7.16, touch from the
 * kernel's HID service) does not want; this is only its opengl_es() part.
 *
 * Without the kernel's RedwoodGPU service (the GPU did not come up, or boot-arg sweetgpukeep is not
 * set) or with boot-arg sweetgl=0, nothing is loaded and CoreAnimation keeps its software renderer.
 */
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define BRIDGE "/redwood/lib/libredwood-gl.dylib"

static void
say(const char *line)
{
	/* backboardd's stderr is the console in the bring-up image (patch-launchd-plist.py) */
	fprintf(stderr, "sweet-gl: %s\n", line);
}

__attribute__((constructor))
static void
sweet_gl_init(void)
{
	char arguments[1024] = "", line[512];
	size_t size = sizeof(arguments) - 1;
	const char *name = getprogname();

	if (name == NULL || strcmp(name, "backboardd") != 0) {
		return;
	}
	if (sysctlbyname("kern.bootargs", arguments, &size, NULL, 0) == 0 && strstr(arguments, "sweetgl=0") != NULL) {
		say("sweetgl=0: CoreAnimation keeps its software renderer");
		return;
	}
	io_service_t gpu = IOServiceGetMatchingService(0, IOServiceMatching("RedwoodGPU"));
	if (gpu == 0) {
		say("no RedwoodGPU service (GPU not kept on): CoreAnimation keeps its software renderer");
		return;
	}
	IOObjectRelease(gpu);
	if (dlopen(BRIDGE, RTLD_NOW) == NULL) {
		snprintf(line, sizeof(line), "%s: %s", BRIDGE, dlerror());
		say(line);
		return;
	}
	say("OpenGL ES bridge loaded: CoreAnimation draws through Mesa freedreno on the Adreno");
}
