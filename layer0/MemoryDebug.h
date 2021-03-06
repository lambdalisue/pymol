

/* 
A* -------------------------------------------------------------------
B* This file contains source code for the PyMOL computer program
C* Copyright (c) Schrodinger, LLC. 
D* -------------------------------------------------------------------
E* It is unlawful to modify or remove this copyright notice.
F* -------------------------------------------------------------------
G* Please see the accompanying LICENSE file for further information. 
H* -------------------------------------------------------------------
I* Additional authors of this source file include:
-* 
-* 
-*
Z* -------------------------------------------------------------------
*/
#ifndef _H_MemoryDebug
#define _H_MemoryDebug

#include "os_std.h"
#include "PyMOLGlobals.h"

/* This file can be included by C and C++ programs for
   debugging of malloc, realloc, free in C and in addition,
   of new and delete in C++ 

  it also includes a two functions for variable length arrays

  In order to use the debugging system, you must do the following...

  * use mmalloc for malloc
  * use mcalloc for calloc
  * use mrealloc for mrealloc
  * use mfree for free

*/


/* ==================== Master Switch ============================= 
 * Define _MemoryDebug_ON to enable the debugging system...*/


/* WARNING!!! MemoryDebug is not thread safe...it must be disabled
   for stable multi-threaded operation within the PyMOL core */

#define _MemoryDebug_OFF


/* ================================================================ 
 * Don't touch below unless you know what you are doing */

void UtilMemCpy(void *dst, void *src, unsigned int *size);

typedef struct VLARec {
  ov_size size, unit_size;
  float grow_factor;
  int auto_zero;
} VLARec;


/* NOTE: in VLACheck, rec is a zero based array index, not a record count */
#define VLACheck(ptr,type,rec) (ptr=(type*)(((((ov_size)rec)>=((VLARec*)(ptr))[-1].size) ? VLAExpand(ptr,((ov_size)rec)) : (ptr))))

#define VLAlloc(type,init_size) (type*)VLAMalloc(init_size,sizeof(type),5,0)
#define VLACalloc(type,init_size) (type*)VLAMalloc(init_size,sizeof(type),5,1)
#define VLASize(ptr,type,size) {ptr=(type*)VLASetSize(ptr,size);}
#define VLASizeForSure(ptr,type,size) {ptr=(type*)VLASetSizeForSure(ptr,size);}
#define VLAFreeP(ptr) {if(ptr) {VLAFree(ptr);ptr=NULL;}}
#define VLACopy(ptr,type) (type*)VLANewCopy(ptr);
#define VLAInsert(ptr,type,index,count) {ptr=(type*)VLAInsertRaw(ptr,index,count);}
#define VLADelete(ptr,type,index,count) {ptr=(type*)VLADeleteRaw(ptr,index,count);}

#define Alloc(type,size) (type*)mmalloc(sizeof(type)*(size))
#define Calloc(type,size) (type*)mcalloc(sizeof(type),size)
#define Realloc(ptr,type,size) (type*)mrealloc(ptr,sizeof(type)*(size))

#define FreeP(ptr) {if(ptr) {mfree(ptr);ptr=NULL;}}
#define DeleteP(ptr) {if(ptr) {delete ptr;ptr=NULL;}}
#define DeleteAP(ptr) {if(ptr) {delete[] ptr;ptr=NULL;}}

void *VLAExpand(void *ptr, ov_size rec);        /* NOTE: rec is index (total-1) */
void *MemoryReallocForSure(void *ptr, unsigned int newSize);
void *MemoryReallocForSureSafe(void *ptr, unsigned int newSize, unsigned int oldSize);

void *VLADeleteRaw(void *ptr, int index, unsigned int count);
void *VLAInsertRaw(void *ptr, int index, unsigned int count);

#ifndef _MemoryDebug_ON
void *VLAMalloc(ov_size init_size, ov_size unit_size, unsigned int grow_factor, int auto_zero); /*growfactor 1-10 */

#else
#define VLAMalloc(a,b,c,d) _VLAMalloc(__FILE__,__LINE__,a,b,c,d)

void *_VLAMalloc(const char *file, int line, ov_size init_size, ov_size unit_size, unsigned int grow_factor, int auto_zero);    /*growfactor 1-10 */
#endif

void VLAFree(void *ptr);
void *VLASetSize(void *ptr, unsigned int newSize);
void *VLASetSizeForSure(void *ptr, unsigned int newSize);

unsigned int VLAGetSize(const void *ptr);
void *VLANewCopy(const void *ptr);
void MemoryZero(char *p, char *q);

#ifndef _MemoryDebug_ON


/* _MemoryDebug_ON not defined */

#define mcalloc calloc
#define mmalloc malloc
#define mrealloc realloc
#define mfree free
#define mstrdup strdup
#define ReallocForSure(ptr,type,size) (type*)MemoryReallocForSure(ptr,sizeof(type)*(size))
#define ReallocForSureSafe(ptr,type,size,old_size) (type*)MemoryReallocForSure(ptr,sizeof(type)*(size),sizeof(type)*(old_size))

#define MemoryDebugDump()

#ifdef __cplusplus
#define mnew new
#endif

#define MD_FILE_LINE_Call
#define MD_FILE_LINE_Decl
#define MD_FILE_LINE_Nest
#define MD_FILE_LINE_PTR_Call

#else


/* _MemoryDebug_ON is defined */

#ifdef __cplusplus
extern "C" {
#endif

#define _MDPointer 1

#define MD_FILE_LINE_Call ,__FILE__,__LINE__
#define MD_FILE_LINE_Decl ,const char *file,int line
#define MD_FILE_LINE_Nest ,file,line
#define MD_FILE_LINE_PTR_Call ,__FILE__,__LINE__,_MDPointer

#define mmalloc(x) MemoryDebugMalloc(x,__FILE__,__LINE__,_MDPointer)
#define mcalloc(x,y) MemoryDebugCalloc(x,y,__FILE__,__LINE__,_MDPointer)
#define mrealloc(x,y) MemoryDebugRealloc(x,y,__FILE__,__LINE__,_MDPointer)
#define mfree(x) MemoryDebugFree(x,__FILE__,__LINE__,_MDPointer)
#define mstrdup(x) MemoryDebugStrDup(x,__FILE__,__LINE__,_MDPointer)

#define ReallocForSure(ptr,type,size) (type*)MemoryDebugReallocForSure(ptr,sizeof(type)*(size),__FILE__,__LINE__,_MDPointer)
#define ReallocForSureSafe(ptr,type,size,old_size) (type*)MemoryDebugReallocForSureSafe(ptr,sizeof(type)*(size),\
                    sizeof(type)*(old_size),__FILE__,__LINE__,_MDPointer)

  void *MemoryDebugMalloc(size_t size, const char *file, int line, int type);
  void *MemoryDebugCalloc(size_t nmemb, size_t size, const char *file, int line,
                          int type);
  void *MemoryDebugRealloc(void *ptr, size_t size, const char *file, int line, int type);
  void *MemoryDebugReallocForSure(void *ptr, size_t size, const char *file,
                                  int line, int type);
  void *MemoryDebugReallocForSureSafe(void *ptr, size_t size, size_t old_size,
                                      const char *file, int line, int type);

  void MemoryDebugFree(void *ptr, const char *file, int line, int type);
  void MemoryDebugQuietFree(void *ptr, int type);

  char *MemoryDebugStrDup(const char *s, const char *file, int line, int type);

  void MemoryDebugDump(void);
  int MemoryDebugUsage(void);

#ifdef __cplusplus
} void *operator   new(size_t size, const char *file, int line);

#define mnew new(__FILE__,__LINE__)

#endif

#endif

/*
 * Templated version of the `VLACopy` macro
 */
template <typename T>
T * VLACopy2(const T * vla) {
  return VLACopy((void*)vla, T);
}

#endif
