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
 * One part of HackiOS's injected library is needed after all, ported here with this system's
 * addresses: CoreAnimation's OpenGL ES renderer has no shader for some of its "functions" (the lock
 * screen wallpaper's "cst", colour matrices, vibrancy, some blurs) and draws those with no program.
 * The bridge stands in for them (eagl.c plain_draw_begin()), but only once it is told the function
 * and its numbers, which inject.c gets by replacing two entries of the virtual table of
 * CA::OGL::GLESContext: function_supported (slot 0x378) and function_uniform (slot 0x388). Without
 * that, the wallpaper came out a red gradient and the app icons blank (boot 0103). iOS 16.7.16
 * (20H392) addresses, from research/dyld; the slots and the context layout (function number at
 * *(context + 0x10) + 0x10) are the same as in HackiOS's iOS 16.5.
 *
 * Without the kernel's RedwoodGPU service (the GPU did not come up, or boot-arg sweetgpukeep is not
 * set) or with boot-arg sweetgl=0, nothing is loaded and CoreAnimation keeps its software renderer.
 */
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define BRIDGE "/redwood/lib/libredwood-gl.dylib"

extern const void *_dyld_get_shared_cache_range(size_t *length);

#define CACHE_BASE              0x180000000UL   /* where the shared cache was built to be */
#define VT_GLES_CONTEXT         0x1d5b2f6e8UL   /* vtable for CA::OGL::GLESContext (20H392) */
#define VSLOT(vtable, offset)   ((vtable) + 0x10 + (offset))
#define FUNCTION_SUPPORTED      0x1884b2164UL   /* CA::OGL::GLContext::function_supported() const */
#define FUNCTION_UNIFORM        0x1884b21acUL   /* CA::OGL::GLContext::function_uniform(unsigned, unsigned long, unsigned long, float const*) */

static uintptr_t slide;
static int (*orig_function_supported)(void *context);
static void (*orig_function_uniform)(void *context, unsigned index, size_t components, size_t count, const float *values);
static void (*gl_function_values)(void *context, unsigned function, unsigned index, size_t components, size_t count,
    const float *values);
static void (*gl_function_refused)(unsigned function);

static void
say(const char *line)
{
	/* backboardd's stderr is the console in the bring-up image (patch-launchd-plist.py) */
	fprintf(stderr, "sweet-gl: %s\n", line);
}

static unsigned
ca_function(void *context)
{
	return *(*(uint8_t **)((char *)context + 0x10) + 0x10);
}

static int
my_function_supported(void *context)
{
	int supported = orig_function_supported(context);
	if (!supported && gl_function_refused != NULL) {
		gl_function_refused(ca_function(context));
	}
	return supported;
}

static void
my_function_uniform(void *context, unsigned index, size_t components, size_t count, const float *values)
{
	gl_function_values(context, ca_function(context), index, components, count, values);
	orig_function_uniform(context, index, components, count, values);
}

/* HackiOS inject.c replace_virtual(): one entry of a virtual table, which must now hold `expected` */
static void *
replace_virtual(const char *what, uintptr_t slot, uintptr_t expected, void *replacement)
{
	char line[160];
	uintptr_t *entry = (uintptr_t *)(slot + slide);
	vm_address_t page = (vm_address_t)entry & ~(vm_address_t)(vm_page_size - 1);
	void *original;

	if (*entry != expected + slide) {
		snprintf(line, sizeof(line), "%s: found %#lx, expected %#lx; left alone", what, *entry - slide, expected);
		say(line);
		return NULL;
	}
	if (vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) !=
	    KERN_SUCCESS) {
		snprintf(line, sizeof(line), "%s: cannot make the table writable", what);
		say(line);
		return NULL;
	}
	original = (void *)*entry;
	*entry = (uintptr_t)replacement;
	vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ);
	return original;
}

static void
watch_core_animation_functions(void *bridge)
{
	size_t length = 0;
	const void *cache = _dyld_get_shared_cache_range(&length);

	gl_function_values = (void (*)(void *, unsigned, unsigned, size_t, size_t, const float *))dlsym(bridge,
	    "redwood_gl_function_values");
	gl_function_refused = (void (*)(unsigned))dlsym(bridge, "redwood_gl_function_refused");
	if (cache == NULL || gl_function_values == NULL) {
		say("no shared cache or no redwood_gl_function_values: CoreAnimation's functions are not followed");
		return;
	}
	slide = (uintptr_t)cache - CACHE_BASE;
	orig_function_supported = (int (*)(void *))replace_virtual("function_supported",
	    VSLOT(VT_GLES_CONTEXT, 0x378), FUNCTION_SUPPORTED, my_function_supported);
	orig_function_uniform = (void (*)(void *, unsigned, size_t, size_t, const float *))replace_virtual(
		"function_uniform", VSLOT(VT_GLES_CONTEXT, 0x388), FUNCTION_UNIFORM, my_function_uniform);
	say(orig_function_supported && orig_function_uniform ?
	    "CoreAnimation's functions without shaders are followed (function_supported, function_uniform)" :
	    "CoreAnimation's functions are NOT all followed");
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
	void *bridge = dlopen(BRIDGE, RTLD_NOW);
	if (bridge == NULL) {
		snprintf(line, sizeof(line), "%s: %s", BRIDGE, dlerror());
		say(line);
		return;
	}
	say("OpenGL ES bridge loaded: CoreAnimation draws through Mesa freedreno on the Adreno");
	watch_core_animation_functions(bridge);
}
