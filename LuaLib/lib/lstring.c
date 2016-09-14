/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/


#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


/*
 重新分配hash表大小
*/
void luaS_resize (lua_State *L, int newsize) {
  GCObject **newhash;
  stringtable *tb;
  int i;
  if (G(L)->gcstate == GCSsweepstring)
    return;  /* cannot resize during GC traverse */
	//创建新空间
  newhash = luaM_newvector(L, newsize, GCObject *);
  tb = &G(L)->strt;
	//初始化
  for (i=0; i<newsize; i++) newhash[i] = NULL;
  /* rehash 把老的hash表里的值换到新hash表中*/
  for (i=0; i<tb->size; i++) {
    GCObject *p = tb->hash[i];
		//循环冲突节点
    while (p) {  /* for each node in the list */
      GCObject *next = p->gch.next;  /* save next */
      unsigned int h = gco2ts(p)->hash;
      int h1 = lmod(h, newsize);  /* new position 根据hash值计算相对于newsize的位置*/
      lua_assert(cast_int(h%newsize) == lmod(h, newsize));
      p->gch.next = newhash[h1];  /* chain it 把旧的冲突节点放在新的冲突链表上*/
      newhash[h1] = p;
      p = next;
    }
  }
	//释放旧的hash表
  luaM_freearray(L, tb->hash, tb->size, TString *);
  tb->size = newsize;
  tb->hash = newhash;
}

/*
 创建新字符串
*/
static TString *newlstr (lua_State *L, const char *str, size_t l,
                                       unsigned int h) {
  TString *ts;
  stringtable *tb;
	//字符长度是否越界
  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
	//创建TString内存空间，大小等于TString大小加上字符串大小。
	//可以看出字符串是直接放在TString内存块地址后面的
  ts = cast(TString *, luaM_malloc(L, (l+1)*sizeof(char)+sizeof(TString)));
  ts->tsv.len = l;
  ts->tsv.hash = h;
  ts->tsv.marked = luaC_white(G(L));
  ts->tsv.tt = LUA_TSTRING;
  ts->tsv.reserved = 0;
	memcpy(ts+1, str, l*sizeof(char));	/* 复制字符串到TString内存块地址后面的位置上。*/
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */
  tb = &G(L)->strt;
  h = lmod(h, tb->size);	/*通过hash值，转换为具体下标位置*/
  ts->tsv.next = tb->hash[h];  /* chain new entry 新的字符串存到hash表里，并把next指向之前冲突的字符串*/
  tb->hash[h] = obj2gco(ts);
  tb->nuse++;
	//hash表空间不够？
  if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded 重新设置hash表大小*/
  return ts;
}

/*
 创建字符串，如果在全局stringtable里有则不用创建。
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  GCObject *o;
  unsigned int h = cast(unsigned int, l);  /* seed */
  size_t step = (l>>5)+1;  /* if string is too long, don't hash all its chars */
  size_t l1;
  for (l1=l; l1>=step; l1-=step)  /* compute hash 计算hash值*/
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
	//遍历在冲突位置上的TString，查找是否已经存在相同的字符串
  for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
       o != NULL;
       o = o->gch.next) {
		//转化为TString类型
    TString *ts = rawgco2ts(o);
		//判断长度和字符串是否相同
    if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
      /* string may be dead */
      if (isdead(G(L), o)) changewhite(o);
      return ts;
    }
  }
	//全局string表没有找到，创建新的字符串。
  return newlstr(L, str, l, h);  /* not found */
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));
  u->uv.marked = luaC_white(G(L));  /* is not finalized */
  u->uv.tt = LUA_TUSERDATA;
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = e;
  /* chain it on udata list (after main thread) */
  u->uv.next = G(L)->mainthread->next;
  G(L)->mainthread->next = obj2gco(u);
  return u;
}

