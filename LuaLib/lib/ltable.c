/*
** $Id: ltable.c,v 2.32.1.2 2007/12/28 15:32:23 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest `n' such that at
** least half the slots between 0 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the `original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <math.h>
#include <string.h>

#define ltable_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"


/*
** max size of array part is 2^MAXBITS
*/
#if LUAI_BITSINT > 26
#define MAXBITS		26
#else
#define MAXBITS		(LUAI_BITSINT-2)
#endif

#define MAXASIZE	(1 << MAXBITS)


#define hashpow2(t,n)      (gnode(t, lmod((n), sizenode(t))))
  
#define hashstr(t,str)  hashpow2(t, (str)->tsv.hash)
#define hashboolean(t,p)        hashpow2(t, p)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))


#define hashpointer(t,p)	hashmod(t, IntPoint(p))


/*
** number of ints inside a lua_Number
*/
#define numints		cast_int(sizeof(lua_Number)/sizeof(int))



#define dummynode		(&dummynode_)

static const Node dummynode_ = {
  {{NULL}, LUA_TNIL},  /* value */
  {{{NULL}, LUA_TNIL, NULL}}  /* key */
};


/*
** hash for lua_Numbers
 通过数字key取hash表里的值
*/
static Node *hashnum (const Table *t, lua_Number n) {
  unsigned int a[numints];
  int i;
  //-0与0都是在0的位置
  if (luai_numeq(n, 0))  /* avoid problems with -0 */
    return gnode(t, 0);
  memcpy(a, &n, sizeof(a));
  for (i = 1; i < numints; i++) a[0] += a[i];
  return hashmod(t, a[0]);
}



/*
** returns the `main' position of an element in a table (that is, the index
** of its hash value)
 通过key在hash表中，找到对应位置的值
*/
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMBER:
      return hashnum(t, nvalue(key));
    case LUA_TSTRING:
      return hashstr(t, rawtsvalue(key));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    default:
      return hashpointer(t, gcvalue(key));
  }
}


/*
** returns the index for `key' if `key' is an appropriate key to live in
** the array part of the table, -1 otherwise.
 实际上是判断这个key是否为整数
*/
static int arrayindex (const TValue *key) {
  if (ttisnumber(key)) {
    lua_Number n = nvalue(key);
    int k;
    lua_number2int(k, n);
    if (luai_numeq(cast_num(k), n))
      return k;
  }
  return -1;  /* `key' did not match some condition */
}


/*
** returns the index of a `key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signalled by -1.
 返回key的位置下标
*/
static int findindex (lua_State *L, Table *t, StkId key) {
  int i;
  if (ttisnil(key)) return -1;  /* first iteration 没错，第一次遍历的情况，因为key初始为nil*/
  i = arrayindex(key);
  //在array里的情况
  if (0 < i && i <= t->sizearray)  /* is `key' inside array part? */
    return i-1;  /* yes; that's the index (corrected to C) */
  //在hash表里的情况
  else {
    Node *n = mainposition(t, key);
    do {  /* check whether `key' is somewhere in the chain */
      /* key may be dead already, but it is ok to use it in `next' */
      if (luaO_rawequalObj(key2tval(n), key) ||
            (ttype(gkey(n)) == LUA_TDEADKEY && iscollectable(key) &&
             gcvalue(gkey(n)) == gcvalue(key))) {
        //计算key在hash表里的位置
        i = cast_int(n - gnode(t, 0));  /* key index in hash table */
        /* hash elements are numbered after array ones */
        //这里为什么要加上数组的长度呢？因为，在luaH_next里，是先遍历数组，在遍历hash的。
        return i + t->sizearray;
      }
      else n = gnext(n);
    } while (n);
    luaG_runerror(L, "invalid key to " LUA_QL("next"));  /* key not found */
    return 0;  /* to avoid warnings */
  }
}

//寻找当前key的下一个nkey，把nkey赋值给当前key，并把nkey对应的value压栈
int luaH_next (lua_State *L, Table *t, StkId key) {
  int i = findindex(L, t, key);  /* find original element */
  //就是这个细节i++没注意，在这里停留了很久。
  //i++，即会从下一个key值开始遍历，因为有可能是空的，所以需要遍历到不为空为止。
  for (i++; i < t->sizearray; i++) {  /* try first array part */
    if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
      setnvalue(key, cast_num(i+1));
      //在栈里面，value赋值给栈顶的空位置，即L->top = &t->array[i]
      //在函数外面会执行L->top++的。
      setobj2s(L, key+1, &t->array[i]);
      return 1;
    }
  }
  //i - t->sizearray,求出hash下标的真正位置
  for (i -= t->sizearray; i < sizenode(t); i++) {  /* then hash part */
    if (!ttisnil(gval(gnode(t, i)))) {  /* a non-nil value? */
      setobj2s(L, key, key2tval(gnode(t, i)));
      setobj2s(L, key+1, gval(gnode(t, i)));
      return 1;
    }
  }
  return 0;  /* no more elements */
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/

/*
 重新计算数组大小，*narray记录数组大小，返回na为实际存在数组的个数
 算法说明：当前1到2^n次方区间内的不为nil的个数必须满足大于2^n / 2，否则只能2^(n - x)来存，然后下标超出的部分用hash表存。
 例如：
 a[0] = 1, 数组大小1,hash大小0
 a[0] = 1, a[1] = 1，数组大小2, hash大小0
 a[0] = 1, a[1] = 1, a[5] = 1, 数组大小4, hash大小1，a[5]本来是在8这个区间里的，但是有用个数3 < 8 / 2，所以a[5]放在了hash表里
 a[0] = 1, a[1] = 1, a[5] = 1, a[6] = 1, 数组大小4，hash大小2,有用个数4 < 8 / 2，所以a[5],a[6]放在hash表里
 a[0] = 1, a[1] = 1, a[5] = 1, a[6] = 1, a[7] = 1，数组大小8，hash大小0, 有用个数5 > 8 / 2
*/
static int computesizes (int nums[], int *narray) {
  int i;
  int twotoi;  /* 2^i */
  int a = 0;  /* number of elements smaller than 2^i */
  int na = 0;  /* number of elements to go to array part */
  int n = 0;  /* optimal size for array part */
  for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  /* more than half elements present? */
        n = twotoi;  /* optimal size (till now) */
        na = a;  /* all elements smaller than n will go to array part */
      }
    }
    if (a == *narray) break;  /* all elements already counted */
  }
  *narray = n;
  lua_assert(*narray/2 <= na && na <= *narray);
  return na;
}

/*
 累计在hash表里的整数key
*/
static int countint (const TValue *key, int *nums) {
  //获取当前key值
  int k = arrayindex(key);
  if (0 < k && k <= MAXASIZE) {  /* is `key' an appropriate array index? */
    //计算这个k在哪个区间，并进行累计
    nums[ceillog2(k)]++;  /* count as such */
    return 1;
  }
  else
    return 0;
}

//统计2的n次方区间内有多少个不为nil的个数，存放到nums
//例如
//   nums[0]存放2^0，即统计&t->array[0]
//   nums[1]存放2^1到2^2次方有多少个不为nil的，即统计&t->array[1]到&t->array[3]
//   nums[2]存放2^2到2^3次方有多少个不为nil的，即统计&t->array[4]到&t->array[7]
//放回所有不为nil的个数
static int numusearray (const Table *t, int *nums) {
  int lg;
  int ttlg;  /* 2^lg */
  int ause = 0;  /* summation of `nums' 统计所有不为nil的个数*/
  int i = 1;  /* count to traverse all array keys */
  for (lg=0, ttlg=1; lg<=MAXBITS; lg++, ttlg*=2) {  /* for each slice */
    int lc = 0;  /* counter */
    int lim = ttlg;
    if (lim > t->sizearray) {
      lim = t->sizearray;  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg-1), 2^lg] */
    //统计2的(n - 1)次方到2的n次方的区间里，有多少个值
    for (; i <= lim; i++) {
      if (!ttisnil(&t->array[i-1]))
        lc++;
    }
    //存放在当前区间
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}

/*
 统计在hash表里，以整数为key的个数，并按2^(n - 1)到2^n区间进行统计，并返还所有有值的node个数
*/
static int numusehash (const Table *t, int *nums, int *pnasize) {
  int totaluse = 0;  /* total number of elements 统计所有已存在的键，用于返回*/
  int ause = 0;  /* summation of `nums' 统计所有整数key，并与pnasize累加*/
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    if (!ttisnil(gval(n))) {
      ause += countint(key2tval(n), nums);
      totaluse++;
    }
  }
  *pnasize += ause;
  return totaluse;
}

/*
  设置数组的容量
*/
static void setarrayvector (lua_State *L, Table *t, int size) {
  int i;
  //重新设置数组的大小
  luaM_reallocvector(L, t->array, t->sizearray, size, TValue);
  //循环把数组元素初始化为nil类型
  for (i=t->sizearray; i<size; i++)
     setnilvalue(&t->array[i]);
  t->sizearray = size;
}

/*
 设置哈希表的容量
*/
static void setnodevector (lua_State *L, Table *t, int size) {
  int lsize;
  if (size == 0) {  /* no elements to hash part? */
    t->node = cast(Node *, dummynode);  /* use common `dummynode' */
    lsize = 0;
  }
  else {
    int i;
    //实际大小转化为指数形式
    lsize = ceillog2(size);
    if (lsize > MAXBITS)
      luaG_runerror(L, "table overflow");
    //这里实际大小以2的lsize次方来算的
    size = twoto(lsize);
    //创建指定大小的空间
    t->node = luaM_newvector(L, size, Node);
    //循环初始化每个node
    for (i=0; i<size; i++) {
      Node *n = gnode(t, i);
      gnext(n) = NULL;
      setnilvalue(gkey(n));
      setnilvalue(gval(n));
    }
  }
  t->lsizenode = cast_byte(lsize);
  t->lastfree = gnode(t, size);  /* all positions are free */
}

/*
 重新分配数组和hash表空间
*/
static void resize (lua_State *L, Table *t, int nasize, int nhsize) {
  int i;
  int oldasize = t->sizearray;
  int oldhsize = t->lsizenode;
  Node *nold = t->node;  /* save old hash ... 保存当前的hash表，用于后面创建新hash表时，可以重新对各个node赋值*/
  if (nasize > oldasize)  /* array part must grow? 需要扩展数组*/
    setarrayvector(L, t, nasize);
  /* create new hash part with appropriate size 重新分配hash空间*/
  setnodevector(L, t, nhsize);  
  if (nasize < oldasize) {  /* array part must shrink? */
    t->sizearray = nasize;
    /* re-insert elements from vanishing slice 超出部分存放到hash表里*/
    for (i=nasize; i<oldasize; i++) {
      if (!ttisnil(&t->array[i]))
        setobjt2t(L, luaH_setnum(L, t, i+1), &t->array[i]);
    }
    /* shrink array 重新分配数组空间，去掉后面溢出部分*/
    luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
  }
  /* re-insert elements from hash part 从后到前遍历，把老hash表的值搬到新表中*/
  for (i = twoto(oldhsize) - 1; i  >= 0; i--) {
    Node *old = nold+i;
    if (!ttisnil(gval(old)))
      setobjt2t(L, luaH_set(L, t, key2tval(old)), gval(old));
  }
  //释放老hash表空间
  if (nold != dummynode)
    luaM_freearray(L, nold, twoto(oldhsize), Node);  /* free old array */
}


void luaH_resizearray (lua_State *L, Table *t, int nasize) {
  int nsize = (t->node == dummynode) ? 0 : sizenode(t);
  resize(L, t, nasize, nsize);
}

//加入key，重新分配hash与array的空间
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  int nasize, na;//nasize前期累计整数key个数，后期做为数组空间大小，na表示数组不为nil的个数
  int nums[MAXBITS+1];  /* nums[i] = number of keys between 2^(i-1) and 2^i 累计各个区间整数key不为nil的个数，包括hash*/
  int i;
  int totaluse;//记录所有已存在的键，包括hash和array
  for (i=0; i<=MAXBITS; i++) nums[i] = 0;  /* reset counts 初始化所有计数区间*/
  nasize = numusearray(t, nums);  /* count keys in array part 以区间统计数组里不为nil的个数，并获得总数*/
  totaluse = nasize;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part 统计hash表里已有的键，以及整数键的个数已经区间分布*/
  /* count extra key */
  //如果新key是整数类型的情况
  nasize += countint(ek, nums);
  //累计新key
  totaluse++;
  /* compute new size for array part 重新计算数组空间*/
  na = computesizes(nums, &nasize);
  /* resize the table to new computed sizes 重新创建内存空间, nasize为新数组大小，totaluse - na表示所有键的个数减去新数组的个数，即为新hash表需要存放的个数 */
  resize(L, t, nasize, totaluse - na);
}



/*
** }=============================================================
*/

/*
 narray 为数组的大小
 nhash 为哈希表的大小
*/
Table *luaH_new (lua_State *L, int narray, int nhash) {
  Table *t = luaM_new(L, Table);
  luaC_link(L, obj2gco(t), LUA_TTABLE);
  t->metatable = NULL;
  t->flags = cast_byte(~0);
  /* temporary values (kept only if some malloc fails) */
  t->array = NULL;
  t->sizearray = 0;
  t->lsizenode = 0;
  t->node = cast(Node *, dummynode);
  setarrayvector(L, t, narray);
  setnodevector(L, t, nhash);
  return t;
}

/*
 释放数组与hash表空间
*/
void luaH_free (lua_State *L, Table *t) {
  if (t->node != dummynode)
    luaM_freearray(L, t->node, sizenode(t), Node);
  luaM_freearray(L, t->array, t->sizearray, TValue);
  luaM_free(L, t);
}

/*
 在hash表里，获得可用的node。
 设计中，把hash表里，lastfree指针指向最后一个可用的位置。
*/
static Node *getfreepos (Table *t) {
  while (t->lastfree-- > t->node) {
    if (ttisnil(gkey(t->lastfree)))
      return t->lastfree;
  }
  return NULL;  /* could not find a free place */
}



/*
** inserts a new key into a hash table; first, check whether key's main 
** position is free. If not, check whether colliding node is in its main 
** position or not: if it is not, move colliding node to an empty place and 
** put new key in its main position; otherwise (colliding node is in its main 
** position), new key goes to an empty position. 
*/
static TValue *newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp = mainposition(t, key);
  //两种情况，主键有值的情况很好理解。另一种，是t->node没有分配空间的情况，即第一次插入的情况。
  if (!ttisnil(gval(mp)) || mp == dummynode) {
    Node *othern;
    Node *n = getfreepos(t);  /* get a free place */
    if (n == NULL) {  /* cannot find a free place? */
      rehash(L, t, key);  /* grow table */
      return luaH_set(L, t, key);  /* re-insert key into grown table */
    }
    lua_assert(n != dummynode);
    othern = mainposition(t, key2tval(mp));
    //这里想了很久终于明白为什么有othern != mp这种情况，表示mp这个node原本不属于这个位置的，只是占用而已。
    //因为mainposition取出来的node，有可能本来就不是存放在这个位置的。而是之前与某一个位置冲突，而放在lastfree里的。
    //所以othern != mp这种情况，表示的是原来不应存放在这个位置的node移到lastfree，而这个位置被新node占据。
    if (othern != mp) {  /* is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      //这里是遍历找到mp的前一个冲突节点
      while (gnext(othern) != mp) othern = gnext(othern);  /* find previous */
      //把othern的下一个节点（即mp的位置）指向lastfree，mp的值赋值给lastfree
      gnext(othern) = n;  /* redo the chain with `n' in place of `mp' */
      *n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      gnext(mp) = NULL;  /* now `mp' is free */
      setnilvalue(gval(mp));
    }
    //表示mp的位置原本就是属于这个位置的。也就是说与这个位置的哈希值是碰撞的。
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      //这里新node（即n）是链接在冲突链的第二个位置
      gnext(n) = gnext(mp);  /* chain new position */
      gnext(mp) = n;
      mp = n;
    }
  }
  gkey(mp)->value = key->value; gkey(mp)->tt = key->tt;
  luaC_barriert(L, t, key);
  lua_assert(ttisnil(gval(mp)));
  return gval(mp);
}


/*
** search function for integers
  查找整型key对应的数值
*/
const TValue *luaH_getnum (Table *t, int key) {
  /* (1 <= key && key <= t->sizearray) */
  //如果是在数组长度范围的则在数组里面取值，因为有时候key离散太高时，这个key会放在hash表里。
  if (cast(unsigned int, key-1) < cast(unsigned int, t->sizearray))
    return &t->array[key-1];
  else {
    lua_Number nk = cast_num(key);
    Node *n = hashnum(t, nk);
    //这里遍历碰撞链表，找到等于这个key的值
    do {  /* check whether `key' is somewhere in the chain */
      if (ttisnumber(gkey(n)) && luai_numeq(nvalue(gkey(n)), nk))
        return gval(n);  /* that's it */
      else n = gnext(n);
    } while (n);
    return luaO_nilobject;
  }
}


/*
** search function for strings
 通过字符key，在hash表里找到对应的值
*/
const TValue *luaH_getstr (Table *t, TString *key) {
  //找到对应key的node
  Node *n = hashstr(t, key);
  //遍历此node的碰撞链表，如果链表上的node的key值与当前key相等，则取出此值
  do {  /* check whether `key' is somewhere in the chain */
    if (ttisstring(gkey(n)) && rawtsvalue(gkey(n)) == key)
      return gval(n);  /* that's it */
    else n = gnext(n);
  } while (n);
  return luaO_nilobject;
}


/*
** main search function
 通过key获得对应的值
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TSTRING: return luaH_getstr(t, rawtsvalue(key));
    case LUA_TNUMBER: {
      int k;
      lua_Number n = nvalue(key);
      //这里需要判断是不是整型key，如果是浮点那就要在hash表里找。
      lua_number2int(k, n);
      if (luai_numeq(cast_num(k), nvalue(key))) /* index is int? */
        return luaH_getnum(t, k);  /* use specialized version */
      /* else go through */
    }
    default: {
      //这个没什么好说，查找除了string 和 整型key的情况。
      Node *n = mainposition(t, key);
      do {  /* check whether `key' is somewhere in the chain */
        if (luaO_rawequalObj(key2tval(n), key))
          return gval(n);  /* that's it */
        else n = gnext(n);
      } while (n);
      return luaO_nilobject;
    }
  }
}

/*
 通过key获得对应的值，如果没有次key，则创建新key，存放在相应的node中。
 返回此key对应的value，如果是新key,则为nil
*/
TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  t->flags = 0;
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    if (ttisnil(key)) luaG_runerror(L, "table index is nil");
    else if (ttisnumber(key) && luai_numisnan(nvalue(key)))
      luaG_runerror(L, "table index is NaN");
    return newkey(L, t, key);
  }
}

/*
 与TValue *luaH_set功能一样，只是设置的是整数key
*/
TValue *luaH_setnum (lua_State *L, Table *t, int key) {
  const TValue *p = luaH_getnum(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    TValue k;
    setnvalue(&k, cast_num(key));
    return newkey(L, t, &k);
  }
}

/*
 与TValue *luaH_set功能类似
 */
TValue *luaH_setstr (lua_State *L, Table *t, TString *key) {
  const TValue *p = luaH_getstr(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    TValue k;
    setsvalue(L, &k, key);
    return newkey(L, t, &k);
  }
}


static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  /* find `i' and `j' such that i is present and j is not */
  while (!ttisnil(luaH_getnum(t, j))) {
    i = j;
    j *= 2;
    if (j > cast(unsigned int, MAX_INT)) {  /* overflow? */
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (!ttisnil(luaH_getnum(t, i))) i++;
      return i - 1;
    }
  }
  /* now do a binary search between them */
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(luaH_getnum(t, m))) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table `t'. A `boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
 如果array有值，返回最大的key。所以table.getn这个只能统计连续的数组。
*/
int luaH_getn (Table *t) {
  unsigned int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {
    /* there is a boundary in the array part: (binary) search for it */
    unsigned int i = 0;
    while (j - i > 1) {
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }
  /* else must find a boundary in hash part */
  else if (t->node == dummynode)  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}



#if defined(LUA_DEBUG)

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}

int luaH_isdummy (Node *n) { return n == dummynode; }

#endif
