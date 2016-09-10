//
//  main.c
//  LuaLib
//
//  Created by 卓锐 on 16/8/4.
//  Copyright © 2016年 卓锐. All rights reserved.
//


#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

//#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"

int toLuaSum(lua_State *L){
    if (lua_type(L, 1) == LUA_TNUMBER && lua_type(L, 2) == LUA_TNUMBER) {
        double a = lua_tonumber(L, 1);
        double b = lua_tonumber(L, 2);
        lua_pushnumber(L, (a + b));
    }
    else{
        luaL_error(L, "不是数字");
    }
    return 1;
}


//c函数绑定到lua
void toLuaTest(){
    lua_State *L = luaL_newstate();
    //打开所有LUA函数库，这样lua才能调用内置的方法，例如print等
    luaL_openlibs(L);
    lua_register(L, "sum", toLuaSum);
    luaL_dofile(L, "/Users/zhuorui/Documents/lua_test/LuaTest/LuaTest/src/test.lua");
    lua_close(L);
}

//入栈，读取测试
void luaStackTest(){
    lua_State *L = luaL_newstate();
    lua_pushstring(L, "test string");
    lua_pushnumber(L, 123);
    if (lua_isstring(L, 1)) {
        printf("%s\n", lua_tostring(L, 1));
    } 
    if (lua_isnumber(L, 2)) {
        printf("%f\n", lua_tonumber(L, 2));
    }
    lua_close(L);
}

void stackDump(lua_State* L){
    printf("\nbegin dump lua stack");
    int i = 0;
    int top = lua_gettop(L);
    for (i = top; i > 0; --i) {
        int t = lua_type(L, i);
        printf("\n");
        switch (t) {
            case LUA_TSTRING:
            {
                printf("'%s' ", lua_tostring(L, i));
            }
                break;
            case LUA_TBOOLEAN:
            {
                printf(lua_toboolean(L, i) ? "true " : "false ");
            }break;
            case LUA_TNUMBER:
            {
                printf("%g ", lua_tonumber(L, i));
            }
                break;
            default:
            {
                printf("%s ", lua_typename(L, t));
            }
                break;
        }
    }
    printf("\nend dump lua stack");
}

LUA_API void tableSizeInfo (lua_State *L, int idx) {
    TValue * t;
    lua_lock(L);
    //从栈中取出对应的table
    t = L->top - 1;
    Table *table = hvalue(t);
    printf("\nsizearray %d", table->sizearray);
    printf("\nlsizenode %d", table->lsizenode);
    
    lua_unlock(L);
}

void luaTableTest(){
    lua_State *L = luaL_newstate();
    lua_newtable(L);
    
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);
    
    lua_pushnumber(L, 2);
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);
    
    
    lua_pushstring(L, "key1");
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);
    
    lua_pushstring(L, "key2");
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);

    lua_pushstring(L, "key3");
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);

    lua_pushstring(L, "key4");
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);

    lua_pushstring(L, "key5");
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);
    
    lua_pushnumber(L, 3);
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);
    
    lua_pushnumber(L, 4);
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);
    
    lua_pushnumber(L, 5);
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);
    
    lua_pushnumber(L, 6);
    lua_pushnumber(L, 123);
    stackDump(L);
    lua_settable(L, -3);
    tableSizeInfo(L, 1);

    stackDump(L);
    
}

int main(int argc, const char * argv[]) {
    // insert code here...
    luaTableTest();
    

    printf("Hello, World!\n");
    return 0;
}
