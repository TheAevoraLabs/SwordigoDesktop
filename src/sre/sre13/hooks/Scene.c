#include "../caver/Scene.h"

static Scene *g_scene = NULL;

Scene *scene_from_L(lua_State *L) {
	lua_getglobal(L, "scene");
	if (!lua_islightuserdata(L, -1)) return NULL;
	const void *scene = lua_topointer(L, -1);
	lua_pop(L, 1);
	return (Scene *)scene;
}

Scene *scene_get(void) {
	return g_scene;
}

HOOK_SYMBOL(
	Scene_Update_Hook,
	"_ZN5Caver5Scene6UpdateEf",
	void, (Scene *scene, float dt)
) {
	g_scene = scene;
	if (orig_Scene_Update_Hook) {
		orig_Scene_Update_Hook(scene, dt);
	}
}

HOOK_SYMBOL(
	Scene_LoadFromFile_Hook,
	"_ZN5Caver5Scene12LoadFromFileERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE",
	int64_t, (Scene *scene, String *filePath)
) {
	if (filePath) {
		const char *s = String_get(filePath);
		if (s && s[0]) {
			extern char g_sre_current_scene_name[];
			int i = 0;
			for (; i < 127 && s[i]; ++i) g_sre_current_scene_name[i] = s[i];
			g_sre_current_scene_name[i] = '\0';

			/* Start the per-level texture-identity set from empty.
			 *
			 * This reset MUST happen here, in guest code, and not in the host.
			 * The host reads the counters and the name list straight out of
			 * guest memory - that is just data, and it is safe - but it must
			 * never CALL into this library: the guest is ARM64 running under the
			 * emulator, so a host call through `g_guest_memory + vaddr` executes
			 * ARM64 bytes as host x86-64 and takes the whole process down. That
			 * was a real SIGSEGV, not a hypothetical one. A scene load is a
			 * guest event, so the guest does it. */
			{
				extern void sre_texture_reset_unique_set(void);
				sre_texture_reset_unique_set();
			}
		}
	}
	if (orig_Scene_LoadFromFile_Hook) {
		return orig_Scene_LoadFromFile_Hook(scene, filePath);
	}
	return 0;
}

G_DL_SYMBOL(
	Scene_SetPaused,
	"_ZN5Caver5Scene9SetPausedEb",
	void, (Scene *scene, bool paused)
);

G_DL_SYMBOL(
	Scene_ObjectWithIdentifier,
	"_ZN5Caver5Scene20ObjectWithIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE",
	void*, (Scene *scene, String *id)
);

G_DL_SYMBOL(
	Scene_AddObject,
	"_ZN5Caver5Scene9AddObjectERKN5boost13intrusive_ptrINS_11SceneObjectEEE",
	void, (Scene *scene, void *intrusive_object)
);

G_DL_SYMBOL(
	Scene_RemoveObject,
	"_ZN5Caver5Scene12RemoveObjectERKN5boost13intrusive_ptrINS_11SceneObjectEEEb",
	void, (Scene *scene, void *intrusive_object, bool unknown)
);

G_DL_SYMBOL(
	Scene_ActivateObject,
	"_ZN5Caver5Scene14ActivateObjectERKN5boost13intrusive_ptrINS_11SceneObjectEEE",
	void, (Scene *scene, void *intrusive_object)
);

G_DL_SYMBOL(
	Scene_GetAllObjects,
	"_ZN5Caver5Scene13GetAllObjectsEPNSt6__ndk16vectorIN5boost13intrusive_ptrINS_11SceneObjectEEENS1_9allocatorIS6_EEEE",
	void, (Scene *scene, void *vector)
);

G_DL_SYMBOL(
	Scene_Update,
	"_ZN5Caver5Scene6UpdateEf",
	void, (Scene *scene, float dt)
);
