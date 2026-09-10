/* A real Lua C frame and luaL_requiref, not a mock of debug.getlocal.
 * No host shell commands are executed. The OS opener substitutes a C probe. */
#include <stdint.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static int loop_token, root_token, loop_key, dataman_token, destructor_token;
static int entries, commands, fail_reentry, retain_root;
static uintptr_t original_root[4];
static void *live_dataman;

static int error_handler(lua_State *L) { (void)L; return 1; }

/* Mirrors the actual first-use stack operations in 0x0c6c89e4. In particular,
 * rawset consumes key/value and the final pop also consumes the original root.
 * The previous fixture's lua_settop(L, 3) concealed this firmware behavior. */
static void loop_context(lua_State *L)
{
    lua_pushlightuserdata(L, &loop_key);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushlightuserdata(L, &loop_key);
        *(void **)lua_newuserdatauv(L, sizeof(void *), 1) = &loop_token;
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
    lua_pop(L, 1);
}

static void root_context(lua_State *L)
{
    uintptr_t *arg = lua_touserdata(L, 3);
    if (!arg) return; /* Allows deliberately malformed-frame tests. */
    if (arg != original_root) {
        lua_getfield(L, LUA_REGISTRYINDEX, "luavgl_key");
        int matches = arg == lua_touserdata(L, -1);
        lua_pop(L, 1);
        if (!matches) luaL_error(L, "unexpected root pointer");
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "luavgl_key");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        uintptr_t *p = lua_newuserdatauv(L, sizeof(original_root), 1);
        memset(p, 0, sizeof(original_root));
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "luavgl_key");
    }
    uintptr_t *p = lua_touserdata(L, -1);
    memcpy(p, arg, 3 * sizeof(*p));
    if (entries == 1) p[3] = (uintptr_t)&destructor_token;
    lua_pop(L, 1);
    if (arg[3]) {
        lua_getfield(L, LUA_REGISTRYINDEX, "dataman.meta");
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            uintptr_t *d = lua_newuserdatauv(L, 2 * sizeof(uintptr_t), 1);
            memset(d, 0, 2 * sizeof(*d));
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, "dataman.meta");
        }
        uintptr_t *d = lua_touserdata(L, -1);
        d[0] = arg[3];
        if (entries == 1) live_dataman = d;
        lua_pop(L, 1);
    }
}
static int probe_execute(lua_State *L)
{
    const char *command = luaL_checkstring(L, 1);
    (void)command;
    commands++;
    lua_getglobal(L, "__band11_test_execute");
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        return 1;
    }
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    lua_pushliteral(L, "exit");
    lua_pushinteger(L, 0);
    return 3;
}
static int probe_open(lua_State *L)
{
    int count = lua_gettop(L);
    lua_getglobal(L, "__band11_fixture_open");
    lua_insert(L, 1);
    lua_call(L, count, 1);
    return 1;
}
static int open_os(lua_State *L)
{
    luaopen_os(L);
    lua_pushcfunction(L, probe_execute);
    lua_setfield(L, -2, "execute");
    return 1;
}
static int pmain(lua_State *L)
{
    entries++;
    root_context(L);
    if (fail_reentry && entries > 1) return luaL_error(L, "fixture init failure");
    luaL_requiref(L, "os", open_os, 1);
    lua_pushnil(L);
    lua_setfield(L, -2, "execute");
    lua_pop(L, 1);
    if (!retain_root) loop_context(L);
    lua_pushcfunction(L, error_handler);
    int handler = lua_gettop(L);
    lua_getglobal(L, "__band11_test_entry");
    if (lua_pcall(L, 0, 0, handler) != LUA_OK) return lua_error(L);
    return 0;
}
static int reset(lua_State *L)
{
    entries = commands = 0;
    fail_reentry = lua_toboolean(L, 1);
    retain_root = lua_toboolean(L, 2);
    original_root[0] = (uintptr_t)&root_token;
    original_root[1] = 0x1234; original_root[2] = 0x5678;
    original_root[3] = (uintptr_t)&dataman_token;
    live_dataman = NULL;
    lua_pushnil(L); lua_setfield(L, LUA_REGISTRYINDEX, "luavgl_key");
    lua_pushnil(L); lua_setfield(L, LUA_REGISTRYINDEX, "dataman.meta");
    lua_pushlightuserdata(L, &loop_key); lua_pushnil(L);
    lua_rawset(L, LUA_REGISTRYINDEX);
    return 0;
}
static int context_ok(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "luavgl_key");
    uintptr_t *p = lua_touserdata(L, -1);
    int root_ok = p && !memcmp(p, original_root, 3 * sizeof(*p)) &&
                  p[3] == (uintptr_t)&destructor_token;
    lua_getfield(L, LUA_REGISTRYINDEX, "dataman.meta");
    uintptr_t *d = lua_touserdata(L, -1);
    lua_pushboolean(L, root_ok);
    lua_pushboolean(L, d == live_dataman && d && d[0] == original_root[3] && d[1] == 0);
    return 2;
}
static int counts(lua_State *L)
{
    lua_pushinteger(L, entries);
    lua_pushinteger(L, commands);
    return 2;
}
int luaopen_band11_fixture(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, pmain); lua_setfield(L, -2, "pmain");
    lua_pushcfunction(L, reset); lua_setfield(L, -2, "reset");
    lua_pushcfunction(L, counts); lua_setfield(L, -2, "counts");
    lua_pushcfunction(L, context_ok); lua_setfield(L, -2, "context_ok");
    lua_pushcfunction(L, probe_execute); lua_setfield(L, -2, "execute");
    lua_pushcfunction(L, probe_open); lua_setfield(L, -2, "open");
    lua_pushinteger(L, (lua_Integer)(uintptr_t)probe_open); lua_setfield(L, -2, "open_address");
    lua_pushinteger(L, (lua_Integer)(uintptr_t)pmain); lua_setfield(L, -2, "pmain_address");
    lua_pushinteger(L, (lua_Integer)(uintptr_t)error_handler); lua_setfield(L, -2, "error_handler_address");
    lua_pushinteger(L, (lua_Integer)(uintptr_t)probe_execute); lua_setfield(L, -2, "execute_address");
    lua_pushlightuserdata(L, &loop_token); lua_setfield(L, -2, "loop");
    lua_pushlightuserdata(L, original_root); lua_setfield(L, -2, "root");
    return 1;
}
