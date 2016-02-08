
/* 
A* -------------------------------------------------------------------
B* This file contains source code for the PyMOL computer program
C* copyright 1998-2000 by Warren Lyford Delano of DeLano Scientific. 
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
#include"os_python.h"

#include"os_proprietary.h"

#include"os_predef.h"
#include"os_std.h"

#include"Base.h"
#include"OOMac.h"
#include"MemoryDebug.h"
#include"Ortho.h"
#include"Setting.h"
#include"Scene.h"
#include"ButMode.h"
#include"Executive.h"
#include"Editor.h"
#include"P.h"
#include"Util.h"
#include"main.h"
#include"PConv.h"
#include"Wizard.h"
#include"Seq.h"
#include"PyMOLOptions.h"
#include"OVContext.h"
#include"ShaderMgr.h"
#include"Sphere.h"
#include"Selector.h"
#include"Parse.h"

/*
 * Setting level info table
 *
 * Levels are not hierarchical at the atom/bond level, that's why a simple
 * sorted enumeration is not sufficient.
 *
 * global < object < object-state
 *                   object-state < atom < atom-state
 *                   object-state < bond < bond-state
 */
const SettingLevelInfoType SettingLevelInfo[] = {
  {"unused"        , 0x00}, // 0b00000000
  {"global"        , 0x00}, // 0b00000000
  {"object"        , 0x01}, // 0b00000001
  {"object-state"  , 0x03}, // 0b00000011
  {"atom"          , 0x07}, // 0b00000111
  {"atom-state"    , 0x0F}, // 0b00001111
  {"bond"          , 0x13}, // 0b00010011
  {"bond-state"    , 0x33}, // 0b00110011
  {NULL, 0}
};

// The following defines the static SettingInfo table
#define SETTINGINFO_IMPLEMENTATION
#include "SettingInfo.h"

static char *get_s(CSetting * I, int index);

// get level name for setting index (for feedback)
const char * SettingLevelGetName(PyMOLGlobals * G, int index) {
  return SettingLevelInfo[SettingInfo[index].level].name;
}

// check if setting index is valid in given level-mask
bool SettingLevelCheckMask(PyMOLGlobals * G, int index, unsigned char mask) {
  unsigned char validmask = SettingLevelInfo[SettingInfo[index].level].mask;
  return (0 == (mask & ~validmask));
}

// check if setting index is valid in given level
bool SettingLevelCheck(PyMOLGlobals * G, int index, unsigned char level) {
  return SettingLevelCheckMask(G, index, SettingLevelInfo[level].mask);
}

/* ================================================================== */

static CSetting *SettingCopyAll(PyMOLGlobals * G, const CSetting * src, CSetting * dst)
{
  if(!dst) {
    dst = Calloc(CSetting, 1);
  } else {
    SettingPurge(dst);
  }

  SettingInit(G, dst);

  if(dst && src) {

    /* simply overwriting existing data (if any) ... in the future we
       may need to release references etc. before doing this */

    unsigned int size = VLAGetSize(src->info);
    VLACheck(dst->info, SettingRec, size - 1);
    UtilCopyMem(dst->info, src->info, sizeof(SettingRec) * size);
    dst->size = src->size;

    // need to properly copy strings
    for (int index = 0; index < cSetting_INIT; ++index) {
      if (SettingInfo[index].type == cSetting_string
          && src->info[index].str_) {
        dst->info[index].str_ = new std::string(*src->info[index].str_);
      }
    }
  }
  return dst;
}

void SettingStoreDefault(PyMOLGlobals * G)
{
  G->Default = SettingCopyAll(G, G->Setting, G->Default);
}

void SettingPurgeDefault(PyMOLGlobals * G)
{
  if(G->Default) {
    SettingPurge(G->Default);
    FreeP(G->Default);
    G->Default = NULL;
  }
}

void SettingUniqueDetachChain(PyMOLGlobals * G, int unique_id)
{
  CSettingUnique *I = G->SettingUnique;
  OVreturn_word result;
  if(OVreturn_IS_OK(result = OVOneToOne_GetForward(I->id2offset, unique_id))) {
    int offset = result.word;
    int next;

    OVOneToOne_DelForward(I->id2offset, unique_id);

    {
      SettingUniqueEntry *entry;
      while(offset) {
        entry = I->entry + offset;
        next = entry->next;
        entry->next = I->next_free;
        I->next_free = offset;
        offset = next;
      }
    }
  } else {
    /* uncaught error */
  }
}

static void SettingUniqueExpand(PyMOLGlobals * G)
{
  CSettingUnique *I = G->SettingUnique;

  if(!I->next_free) {
    int new_n_alloc = (I->n_alloc * 3) / 2;
    int a;
    VLACheck(I->entry, SettingUniqueEntry, new_n_alloc);
    for(a = I->n_alloc; a < new_n_alloc; a++) {
      I->entry[a].next = I->next_free;
      I->next_free = a;
    }
    I->n_alloc = new_n_alloc;
  }
}

int SettingUniqueCheck(PyMOLGlobals * G, int unique_id, int setting_id)
{
  CSettingUnique *I = G->SettingUnique;
  OVreturn_word result;
  if(OVreturn_IS_OK(result = OVOneToOne_GetForward(I->id2offset, unique_id))) {
    int offset = result.word;
    SettingUniqueEntry *entry;

    while(offset) {
      entry = I->entry + offset;
      if(entry->setting_id == setting_id) {
        return 1;
      }
      offset = entry->next;
    }
  }
  return 0;
}

static int SettingUniqueGetTypedValue(PyMOLGlobals * G, int unique_id, int setting_id,
                                      int setting_type, void *value)
{
  CSettingUnique *I = G->SettingUnique;
  OVreturn_word result;
  if(OVreturn_IS_OK(result = OVOneToOne_GetForward(I->id2offset, unique_id))) {
    int offset = result.word;
    SettingUniqueEntry *entry;
    while(offset) {
      entry = I->entry + offset;
      if(entry->setting_id == setting_id) {
        if(SettingInfo[setting_id].type == setting_type) {
	  if (setting_type == cSetting_float3){
	    float *fv = entry->value.float3_;
	    copy3f(fv, (float*)value);
	  } else {
	    *(int *) value = entry->value.int_;
	  }
        } else {
          switch (setting_type) {
          case cSetting_int:
          case cSetting_color:
          case cSetting_boolean:
            switch (SettingInfo[setting_id].type) {
            case cSetting_float:
              *(int *) value = (int) entry->value.float_;
              break;
            default:
              *(int *) value = entry->value.int_;
              break;
            }
            break;
          case cSetting_float:
            *(float *) value = (float) entry->value.int_;
            break;
          case cSetting_float3:
            {
              float *fv = entry->value.float3_;
              copy3f(fv, (float*)value);
            }
            break;
          }
        }
        return 1;
      }
      offset = entry->next;
    }
  }
  return 0;
}

int SettingUniqueGet_b(PyMOLGlobals * G, int unique_id, int setting_id, int *value)
{
  return SettingUniqueGetTypedValue(G, unique_id, setting_id, cSetting_boolean, value);
}

int SettingUniqueGet_i(PyMOLGlobals * G, int unique_id, int setting_id, int *value)
{
  return SettingUniqueGetTypedValue(G, unique_id, setting_id, cSetting_int, value);
}

int SettingUniqueGet_f(PyMOLGlobals * G, int unique_id, int setting_id, float *value)
{
  return SettingUniqueGetTypedValue(G, unique_id, setting_id, cSetting_float, value);
}

int SettingUniqueGet_color(PyMOLGlobals * G, int unique_id, int setting_id, int *value)
{
  return SettingUniqueGetTypedValue(G, unique_id, setting_id, cSetting_color, value);
}

PyObject *SettingUniqueGetPyObject(PyMOLGlobals * G, int unique_id, int index)
{
  int type = SettingGetType(G, index);

  union {
    int val_i;
    float val_f;
    float val_3f[3];
  };

  const float * ptr_3f = val_3f;

  if (SettingUniqueGetTypedValue(G, unique_id, index, type, val_3f)) {
    switch (type) {
    case cSetting_boolean:
      return CPythonVal_New_Boolean(val_i);
    case cSetting_int:
      return CPythonVal_New_Integer(val_i);
    case cSetting_float:
      return CPythonVal_New_Float(val_f);
    case cSetting_color:
      ptr_3f = ColorGet(G, val_i);
    case cSetting_float3:
      {
        PyObject *result = PyTuple_New(3);
        PyTuple_SET_ITEM(result, 0, CPythonVal_New_Float(ptr_3f[0]));
        PyTuple_SET_ITEM(result, 1, CPythonVal_New_Float(ptr_3f[1]));
        PyTuple_SET_ITEM(result, 2, CPythonVal_New_Float(ptr_3f[2]));
        return result;
      }
    }
  }

  return NULL;
}

static int SettingUniqueEntry_IsSame(SettingUniqueEntry *entry, int setting_type, const void *value){
  if (SettingInfo[entry->setting_id].type != setting_type){
    return 0;
  }
  if (setting_type == cSetting_float3){
    const float *v = (float*)value, *ev = entry->value.float3_;
    return v[0]==ev[0] && v[1]==ev[1] && v[2]==ev[2];
  } else {
    return (entry->value.int_ == *(int *) value);
  }
}

static void SettingUniqueEntry_Set(SettingUniqueEntry *entry, int setting_type, const void *value){
  switch (setting_type) {
    case cSetting_boolean:
    case cSetting_int:
    case cSetting_color:
      entry->value.int_ = *(int *) value;
      break;
    case cSetting_float:
      entry->value.float_ = *(float *) value;
      break;
    case cSetting_float3:
      memcpy(entry->value.float3_, value, sizeof(float) * 3);
      break;
    default:
      printf("SettingUniqueEntry_Set-Error: unsupported type %d\n", setting_type);
  }
}

int SettingUniqueSetTypedValue(PyMOLGlobals * G, int unique_id, int setting_id,
			       int setting_type, const void *value)

/* set value to NULL in order to delete setting */
{
  CSettingUnique *I = G->SettingUnique;
  OVreturn_word result;
  int isset = false;

  if(OVreturn_IS_OK((result = OVOneToOne_GetForward(I->id2offset, unique_id)))) {       /* setting list exists for atom */
    int offset = result.word;
    int prev = 0;
    int found = false;
    while(offset) {
      SettingUniqueEntry *entry = I->entry + offset;
      if(entry->setting_id == setting_id) {
        found = true;           /* this setting is already defined */
        if(value) {             /* if redefining value */
	  if (!SettingUniqueEntry_IsSame(entry, setting_type, value)){
	    SettingUniqueEntry_Set(entry, setting_type, value);
	    isset = true;
	  }
        } else {                /* or NULL value means delete this setting */
          if(!prev) {           /* if first entry in list */
            OVOneToOne_DelForward(I->id2offset, unique_id);
            if(entry->next) {   /* set new list start */
              OVOneToOne_Set(I->id2offset, unique_id, entry->next);
            }
          } else {              /* otherwise excise from middle or end */
            I->entry[prev].next = entry->next;
          }
          entry->next = I->next_free;
          I->next_free = offset;
	  isset = true;
        }
        break;
      }
      prev = offset;
      offset = entry->next;
    }
    if((!found) && value) {     /* setting not found in existing list, so append new value */
      if(!I->next_free)
        SettingUniqueExpand(G);
      if(I->next_free) {
        offset = I->next_free;
        {
          SettingUniqueEntry *entry = I->entry + offset;
          I->next_free = entry->next;
          entry->next = 0;

          if(prev) {            /* append onto existing list */
            I->entry[prev].next = offset;
            entry->setting_id = setting_id;
            SettingUniqueEntry_Set(entry, setting_type, value);
            isset = true;
          } else if(OVreturn_IS_OK(OVOneToOne_Set(I->id2offset, unique_id, offset))) {
            /* create new list */
            entry->setting_id = setting_id;
            SettingUniqueEntry_Set(entry, setting_type, value);
            isset = true;
          }
        }
      }
    }
  } else if(value && (result.status == OVstatus_NOT_FOUND)) {   /* new setting list for atom */
    if(!I->next_free)
      SettingUniqueExpand(G);
    if(I->next_free) {
      int offset = I->next_free;
      SettingUniqueEntry *entry = I->entry + offset;

      if(OVreturn_IS_OK(OVOneToOne_Set(I->id2offset, unique_id, offset))) {
        I->next_free = entry->next;
        entry->setting_id = setting_id;
        entry->next = 0;
        SettingUniqueEntry_Set(entry, setting_type, value);
        isset = true;
      }
    }
  } else {
    /* unhandled error */
  }
  return isset;
}

void SettingUniqueSet_i(PyMOLGlobals * G, int unique_id, int setting_id, int value)
{
  SettingUniqueSetTypedValue(G, unique_id, setting_id, cSetting_int, &value);
}

void SettingUniqueSet_f(PyMOLGlobals * G, int unique_id, int setting_id, float value)
{
  SettingUniqueSetTypedValue(G, unique_id, setting_id, cSetting_float, &value);
}

void SettingUniqueSet_b(PyMOLGlobals * G, int unique_id, int setting_id, int value)
{
  SettingUniqueSetTypedValue(G, unique_id, setting_id, cSetting_boolean, &value);
}

void SettingUniqueSet_color(PyMOLGlobals * G, int unique_id, int setting_id, int value)
{
  SettingUniqueSetTypedValue(G, unique_id, setting_id, cSetting_color, &value);
}

#ifndef _PYMOL_NOPY
bool SettingUniqueSetPyObject(PyMOLGlobals * G, int unique_id, int index, PyObject *value)
{
  if (!value)
    return SettingUniqueSetTypedValue(G, unique_id, index, cSetting_blank, NULL);

  int type = SettingGetType(G, index);

  union {
    int val_i;
    float val_f;
    float val_3f[3];
  };

  switch (type) {
  case cSetting_boolean:
  case cSetting_int:
    ok_assert(1, PConvPyObjectToInt(value, &val_i));
    break;
  case cSetting_float:
    ok_assert(1, PConvPyObjectToFloat(value, &val_f));
    break;
  case cSetting_color:
    if (!PConvPyIntToInt(value, &val_i)) {
      OrthoLineType sval;
      ok_assert(1, PConvPyStrToStr(value, sval, OrthoLineLength));
      val_i = ColorGetIndex(G, sval);
    }
    break;
  case cSetting_float3:
    if (!PConvPyListOrTupleToFloatArrayInPlace(value, val_3f, 3)) {
      OrthoLineType sval;
      ok_assert(1, PConvPyStrToStr(value, sval, OrthoLineLength) &&
          sscanf(sval, "%f%f%f", &val_3f[0], &val_3f[1], &val_3f[2]) == 3);
    }
    break;
  default:
    PRINTFB(G, FB_Python, FB_Errors)
      " Python-Error: atom-state-level setting unsupported type=%d\n", type ENDFB(G);
    return false;
  }

  return SettingUniqueSetTypedValue(G, unique_id, index, type, val_3f);

ok_except1:
  PRINTFB(G, FB_Setting, FB_Errors)
    " Setting-Error: type mismatch\n" ENDFB(G);
  return false;
}
#endif

void SettingUniqueResetAll(PyMOLGlobals * G)
{
  CSettingUnique *I = G->SettingUnique;

  OVOneToOne_Reset(I->id2offset);
  {
    int a;
    I->n_alloc = 10;
    VLAFreeP(I->entry);
    I->entry = VLACalloc(SettingUniqueEntry, I->n_alloc);
    /* note: intentially skip index 0  */
    for(a = 2; a < 10; a++) {
      I->entry[a].next = a - 1;
    }
    I->next_free = I->n_alloc - 1;
  }
}

int SettingUniquePrintAll(PyMOLGlobals * G, int src_unique_id)
{
  int ok = true;
  CSettingUnique *I = G->SettingUnique;
  OVreturn_word src_result;
  printf("SettingUniquePrintAll: ");
  if(OVreturn_IS_OK(src_result = OVOneToOne_GetForward(I->id2offset, src_unique_id))) {
    int src_offset = src_result.word;
    SettingUniqueEntry *src_entry;
    while(ok && src_offset) {
      {
	src_entry = I->entry + src_offset;
	{
	  int setting_id = src_entry->setting_id;
	  int setting_type = SettingInfo[setting_id].type;
	  const char * setting_name = SettingInfo[setting_id].name;
	  switch (setting_type) {
	  case cSetting_int:
	  case cSetting_color:
	  case cSetting_boolean:
	    printf("%s:%d:%d:%d ", setting_name, setting_id, setting_type, src_entry->value.int_);
	    break;
	  case cSetting_float:
	    printf("%s:%d:%d:%f ", setting_name, setting_id, setting_type, src_entry->value.float_);
	    break;
	  case cSetting_float3:
	    printf("%s:%d:%d:%f,%f,%f ", setting_name, setting_id, setting_type, src_entry->value.float3_[0],
		   src_entry->value.float3_[1],
		   src_entry->value.float3_[2]);
	    break;
	  case cSetting_string:
	    printf("%s:%d:%d:s%d ", setting_name, setting_id, setting_type, src_entry->value.int_);
	    break;
	  }
	}
      }
      src_offset = I->entry[src_offset].next; /* src_entry invalid, since I->entry may have changed */
    }
  }
  printf("\n");
  return ok;
}

int SettingUniqueCopyAll(PyMOLGlobals * G, int src_unique_id, int dst_unique_id)
{
  int ok = true;
  CSettingUnique *I = G->SettingUnique;
  OVreturn_word dst_result;

  if(OVreturn_IS_OK((dst_result = OVOneToOne_GetForward(I->id2offset, dst_unique_id)))) {       /* setting list exists for atom */

    /* note, this code path not yet tested...doesn't occur */

    OVreturn_word src_result;
    if(OVreturn_IS_OK(src_result = OVOneToOne_GetForward(I->id2offset, src_unique_id))) {
      int src_offset = src_result.word;
      SettingUniqueEntry *src_entry;
      while(src_offset) {
        src_entry = I->entry + src_offset;

        {
          int setting_id = src_entry->setting_id;
          int setting_type = SettingInfo[setting_id].type;
          void *setting_value = &src_entry->value;
          int dst_offset = dst_result.word;
          int prev = 0;
          int found = false;
          while(dst_offset) {
            SettingUniqueEntry *dst_entry = I->entry + dst_offset;
            if(dst_entry->setting_id == setting_id) {
              found = true;     /* this setting is already defined */
	      SettingUniqueEntry_Set(dst_entry, setting_type, setting_value);
              break;
            }
            prev = dst_offset;
            dst_offset = dst_entry->next;
          }
          if(!found) {          /* setting not found in existing list, so append new value */
            if(!I->next_free)
              SettingUniqueExpand(G);
            if(I->next_free) {
              dst_offset = I->next_free;
              {
                SettingUniqueEntry *dst_entry = I->entry + dst_offset;
                I->next_free = dst_entry->next;
                dst_entry->next = 0;
                if(prev) {      /* append onto existing list */
                  I->entry[prev].next = dst_offset;
                  dst_entry->setting_id = setting_id;
                  SettingUniqueEntry_Set(dst_entry, setting_type, setting_value);
                } else
                  if(OVreturn_IS_OK
                     (OVOneToOne_Set(I->id2offset, dst_unique_id, dst_offset))) {
                  /* create new list */
                  dst_entry->setting_id = setting_id;
                  SettingUniqueEntry_Set(dst_entry, setting_type, setting_value);
                }
              }
            }
          }
        }
        src_offset = I->entry[src_offset].next; /* src_entry invalid, since I->entry may have changed */
      }
    }
  } else if(dst_result.status == OVstatus_NOT_FOUND) {  /* new setting list for atom */
    OVreturn_word src_result;
    if(OVreturn_IS_OK(src_result = OVOneToOne_GetForward(I->id2offset, src_unique_id))) {
      int src_offset = src_result.word;
      int prev = 0;
      SettingUniqueEntry *src_entry;
      while(ok && src_offset) {
        if(!I->next_free)
          SettingUniqueExpand(G);
        {
          src_entry = I->entry + src_offset;
          {
            int setting_id = src_entry->setting_id;
            int setting_type = SettingInfo[setting_id].type;
            void *setting_value = &src_entry->value;
            if(I->next_free) {
              int dst_offset = I->next_free;
              SettingUniqueEntry *dst_entry = I->entry + dst_offset;
              I->next_free = dst_entry->next;

              if(!prev) {
                if(!OVreturn_IS_OK
                   (OVOneToOne_Set(I->id2offset, dst_unique_id, dst_offset))) {
                  ok = false;
                }
              } else {
                I->entry[prev].next = dst_offset;
              }

              if(ok) {
                dst_entry->setting_id = setting_id;
                dst_entry->next = 0;
                SettingUniqueEntry_Set(dst_entry, setting_type, setting_value);
              }
              prev = dst_offset;
            }
          }
        }
        src_offset = I->entry[src_offset].next; /* src_entry invalid, since I->entry may have changed */
      }
    }
  } else {
    ok = false;
    /* unhandled error */
  }

  return ok;
}

static void SettingUniqueInit(PyMOLGlobals * G)
{
  CSettingUnique *I = G->SettingUnique;

  if((I = (G->SettingUnique = Calloc(CSettingUnique, 1)))) {
    I->id2offset = OVOneToOne_New(G->Context->heap);
    {
      int a;
      I->n_alloc = 10;
      I->entry = VLACalloc(SettingUniqueEntry, I->n_alloc);
      /* note: intentially skip index 0  */
      for(a = 2; a < 10; a++) {
        I->entry[a].next = a - 1;       /* 1-based linked list with 0 as sentinel */
      }
      I->next_free = I->n_alloc - 1;
    }
  }
}

static void SettingUniqueFree(PyMOLGlobals * G)
{
  CSettingUnique *I = G->SettingUnique;
  VLAFreeP(I->entry);
  OVOneToOne_Del(I->id2offset);
  FreeP(I);
}

/*
 * For unique_id remapping during partial session loading
 */
int SettingUniqueConvertOldSessionID(PyMOLGlobals * G, int old_unique_id)
{
  CSettingUnique *I = G->SettingUnique;
  int unique_id = old_unique_id;
  if(I->old2new) {
    OVreturn_word ret;
    if(OVreturn_IS_OK(ret = OVOneToOne_GetForward(I->old2new, old_unique_id))) {
      unique_id = ret.word;
    } else {
      unique_id = AtomInfoGetNewUniqueID(G);
      OVOneToOne_Set(I->old2new, old_unique_id, unique_id);
    }
  } else {
    AtomInfoReserveUniqueID(G, unique_id);
  }
  return unique_id;
}

/*
 * Return true if the given setting index should not be stored to PSE.
 *
 * Blacklisted are unused and system-dependent settings.
 */
static bool is_session_blacklisted(int index) {
  if (index >= cSetting_INIT ||
      SettingInfo[index].level == cSettingLevel_unused) {
    return true;
  }

  switch (index) {
  case cSetting_antialias_shader:
  case cSetting_ati_bugs:
  case cSetting_cache_max:
  case cSetting_cgo_shader_ub_color:
  case cSetting_cgo_shader_ub_flags:
  case cSetting_cgo_shader_ub_normal:
  case cSetting_cylinder_shader_ff_workaround:
  case cSetting_defer_updates:
  case cSetting_fast_idle:
  case cSetting_internal_feedback:
  case cSetting_internal_gui:
  case cSetting_internal_prompt:
  case cSetting_logging:
  case cSetting_max_threads:
  case cSetting_mouse_grid:
  case cSetting_mouse_scale:
  case cSetting_nb_spheres_use_shader:
  case cSetting_no_idle:
  case cSetting_nvidia_bugs:
  case cSetting_precomputed_lighting:
  case cSetting_render_as_cylinders:
  case cSetting_security:
  case cSetting_session_changed:
  case cSetting_session_file:
  case cSetting_session_migration:
  case cSetting_session_version_check:
  case cSetting_shaders_from_disk:
  case cSetting_show_progress:
  case cSetting_slow_idle:
  case cSetting_stereo:
  case cSetting_stereo_double_pump_mono:
  case cSetting_stereo_mode:
  case cSetting_suspend_deferred:
  case cSetting_suspend_undo:
  case cSetting_suspend_undo_atom_count:
  case cSetting_suspend_updates:
  case cSetting_text:
  case cSetting_trilines:
  case cSetting_use_geometry_shaders:
  case cSetting_use_shaders:
#ifdef _PYMOL_IOS
  case cSetting_cgo_sphere_quality:
  case cSetting_dynamic_measures:
  case cSetting_label_outline_color:
  case cSetting_mouse_selection_mode:
  case cSetting_sphere_mode:
  case cSetting_sphere_quality:
  case cSetting_stick_ball:
  case cSetting_virtual_trackball:
#elif defined(_PYMOL_ACTIVEX)
  case cSetting_async_builds:
#endif
    return true;
  }

  return false;
}

int SettingUniqueFromPyList(PyMOLGlobals * G, PyObject * list, int partial_restore)
{
  int ok = true;
  if(!partial_restore) {
    SettingUniqueResetAll(G);
  }
  if(list)
    if(PyList_Check(list)) {
      ov_size n_id = PyList_Size(list);
      ov_size a;
      for(a = 0; a < n_id; a++) {
        PyObject *id_list = PyList_GetItem(list, a);
        int unique_id;
        if(ok)
          ok = PyList_Check(id_list);
        if(ok)
          ok = (PyList_Size(id_list) > 1);
        if(ok)
          ok = PConvPyIntToInt(PyList_GetItem(id_list, 0), &unique_id);
        if(ok && partial_restore) {
          unique_id = SettingUniqueConvertOldSessionID(G, unique_id);
        }
        if(ok) {
          ov_size n_set = 0;

          PyObject *setting_list = PyList_GetItem(id_list, 1);
          if(ok)
            ok = PyList_Check(setting_list);
          if(ok)
            n_set = PyList_Size(setting_list);
          if(ok) {
            ov_size b;
            for(b = 0; b < n_set; b++) {
              PyObject *entry_list = PyList_GetItem(setting_list, b);
              if(ok)
                ok = PyList_Check(entry_list);
              if(ok)
                ok = (PyList_Size(entry_list) > 2);
              if(ok) {
                int setting_id;
                int setting_type;
                union {
                  int int_;
                  float float_;
                } value_store;
                if(ok)
                  ok = PConvPyIntToInt(PyList_GetItem(entry_list, 0), &setting_id);
                if(ok)
                  ok = PConvPyIntToInt(PyList_GetItem(entry_list, 1), &setting_type);
                if(ok)
                  switch (setting_type) {

                  case cSetting_int:
                  case cSetting_color:
                  case cSetting_boolean:
                    ok = PConvPyIntToInt(PyList_GetItem(entry_list, 2),
                                         &value_store.int_);
                    break;
                  case cSetting_float:
                    ok = PConvPyFloatToFloat(PyList_GetItem(entry_list, 2),
                                             &value_store.float_);
                    break;
                  }
                if(ok) {
                  SettingUniqueSetTypedValue(G, unique_id, setting_id,
                                             setting_type, &value_store.int_);
                }
              }
            }
          }
        }
      }
    }
  return ok;
}

PyObject *SettingUniqueAsPyList(PyMOLGlobals * G)
{
  PyObject *result = NULL;
  CSettingUnique *I = G->SettingUnique;
  {
    ov_word hidden = 0;
    OVreturn_word ret;
    int n_entry = 0;
    while(1) {
      ret = OVOneToOne_IterateForward(I->id2offset, &hidden);
      if(ret.status != OVstatus_YES)
        break;
      n_entry++;
    }
    result = PyList_New(n_entry);
    if(result) {
      hidden = 0;
      n_entry = 0;
      while(1) {
        PyObject *setting_list = NULL;
        int save_offset, unique_id;
        ret = OVOneToOne_IterateForward(I->id2offset, &hidden);

        if(ret.status != OVstatus_YES)
          break;
        unique_id = ret.word;
        if(OVreturn_IS_OK(ret = OVOneToOne_GetForward(I->id2offset, unique_id))) {
          int offset = ret.word;
          int n_set = 0;

          /* count number of settings for this unique_id */

          SettingUniqueEntry *entry;
          save_offset = offset;
          while(offset) {
            entry = I->entry + offset;
            n_set++;
            offset = entry->next;
          }

          /* create and insert list for each setting */

          setting_list = PyList_New(n_set);
          n_set = 0;
          offset = save_offset;
          while(offset) {
            PyObject *setting_entry = PyList_New(3);
            entry = I->entry + offset;
            int type = SettingInfo[entry->setting_id].type;
            PyList_SetItem(setting_entry, 0, PyInt_FromLong(entry->setting_id));
            PyList_SetItem(setting_entry, 1, PyInt_FromLong(type));
            switch (type) {
            case cSetting_int:
            case cSetting_color:
            case cSetting_boolean:
              PyList_SetItem(setting_entry, 2, PyInt_FromLong(entry->value.int_));
              break;
            case cSetting_float:
              PyList_SetItem(setting_entry, 2,
                             PyFloat_FromDouble(*(float *) &entry->value.float_));
              break;
	    case cSetting_float3:
	      PyList_SetItem(setting_entry, 2,
			     PConvFloatArrayToPyList((float *) &entry->value.float3_, 3));
	      break;
            }
            PyList_SetItem(setting_list, n_set, setting_entry);
            n_set++;
            offset = entry->next;
          }
        }

        /* add this unique_id set into the overall list */

        {
          PyObject *unique_list = PyList_New(2);
          PyList_SetItem(unique_list, 0, PyInt_FromLong(unique_id));
          PyList_SetItem(unique_list, 1, setting_list);
          PyList_SetItem(result, n_entry, unique_list);
        }
        n_entry++;
      }
    }
  }
  return (PConvAutoNone(result));
}

int SettingSetSmart_i(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index,
                      int value)
{
  int dummy;
  if(set1 && SettingGetIfDefined_i(G, set1, index, &dummy)) {
    return SettingSet_i(set1, index, value);
  }
  if(set2 && SettingGetIfDefined_i(G, set2, index, &dummy)) {
    return SettingSet_i(set2, index, value);
  }
  return SettingSetGlobal_i(G, index, value);
}

int SettingSetGlobalsFromPyList(PyMOLGlobals * G, PyObject * list)
{
  int ok = true;

  CSetting *I = G->Setting;

  if(list)
    if(PyList_Check(list))
      ok = SettingFromPyList(I, list);

  /* restore the following settings  */

  if(G->Option->presentation) {
    SettingSet_b(I, cSetting_presentation, 1);
  }
  if(G->Option->no_quit) {
    SettingSet_b(I, cSetting_presentation_auto_quit, 0);
  }

  ColorUpdateFrontFromSettings(G);
  return (ok);
}

PyObject *SettingGetGlobalsAsPyList(PyMOLGlobals * G)
{
  PyObject *result = NULL;
  CSetting *I = G->Setting;
  result = SettingAsPyList(I);
  return (PConvAutoNone(result));
}

static PyObject *get_list(CSetting * I, int index)
{
  PyObject *result = NULL, *value = NULL;
  int setting_type = SettingInfo[index].type;

  if (is_session_blacklisted(index)) {
    return NULL;
  }

  switch (setting_type) {

  case cSetting_boolean:
  case cSetting_int:
  case cSetting_color:
    value = PyInt_FromLong(I->info[index].int_);
    break;
  case cSetting_float:
    value = PyFloat_FromDouble(I->info[index].float_);
    break;
  case cSetting_float3:
    value = PConvFloatArrayToPyList(I->info[index].float3_, 3);
    break;
  case cSetting_string:
    value = PyString_FromString(get_s(I, index));
    break;
  }

  if (value) {
    result = PyList_New(3);
    PyList_SetItem(result, 0, PyInt_FromLong(index));
    PyList_SetItem(result, 1, PyInt_FromLong(setting_type));
    PyList_SetItem(result, 2, value);
  }

  return result;
}

PyObject *SettingAsPyList(CSetting * I)
{
  PyObject *result = NULL;
  int a;

  if(I) {
    std::vector<PyObject*> list;
    list.reserve(cSetting_INIT);

    for(a = 0; a < cSetting_INIT; a++) {
      if(I->info[a].defined) {
        PyObject * item = get_list(I, a);
        if (item != NULL) {
          list.push_back(item);
        }
      }
    }

    result = PConvToPyObject(list);
  }
  return (PConvAutoNone(result));
}

/*========================================================================*/
static int SettingCheckUseShaders(CSetting * I, int quiet)
{
  PyMOLGlobals * G = I->G;
    if (SettingGetGlobal_i(G, cSetting_use_shaders)){
      if (!CShaderMgr_ShadersPresent(G->ShaderMgr)){
	SettingSet_b(I, cSetting_use_shaders, 0);
	if (!quiet){
	  PRINTFB(G, FB_Setting, FB_Warnings)
	    "Setting-Error: use_shaders cannot be set when Shaders are not available, setting use_shaders back to false\n"
	    ENDFB(G);
	}
	return 1;
      }
    }
    return 0;
}

/*========================================================================*/
static int set_list(CSetting * I, PyObject * list)
{
  int index = -1;
  int setting_type = -1;

  union {
    int val_i;
    float val_f;
    float val_3f[3];
    char * val_s;
  };

  if (list == NULL || CPythonVal_IsNone(list))
    return true;

  ok_assert(1, PyList_Check(list));
  ok_assert(1, CPythonVal_PConvPyIntToInt_From_List(I->G, list, 0, &index));
  ok_assert(1, CPythonVal_PConvPyIntToInt_From_List(I->G, list, 1, &setting_type));

  if (is_session_blacklisted(index))
    return true;

  switch (setting_type) {
  case cSetting_boolean:
  case cSetting_int:
  case cSetting_color:
    ok_assert(1, CPythonVal_PConvPyIntToInt_From_List(I->G, list, 2, &val_i));
    if (setting_type == cSetting_color)
      val_i = ColorConvertOldSessionIndex(I->G, val_i);
    SettingSet_i(I, index, val_i);
    break;
  case cSetting_float:
    ok_assert(1, CPythonVal_PConvPyFloatToFloat_From_List(I->G, list, 2, &val_f));
    SettingSet_f(I, index, val_f);
    break;
  case cSetting_float3:
    ok_assert(1, CPythonVal_PConvPyListToFloatArrayInPlaceAutoZero_From_List(I->G, list, 2, val_3f, 3));
    SettingSet_3fv(I, index, val_3f);
    break;
  case cSetting_string:
    ok_assert(1, val_s = PyString_AsString(PyList_GetItem(list, 2)));
    SettingSet_s(I, index, val_s);
    break;
  default:
    ok_raise(1);
  }

  return true;
ok_except1:
  printf(" set_list-Error: i=%d, t=%d\n", index, setting_type);
  return false;
}

/*========================================================================*/
/*
 * Used to set object and object-state level settings from PSEs
 */
CSetting *SettingNewFromPyList(PyMOLGlobals * G, PyObject * list)
{
  int ok = true;
  ov_size size;
  ov_size a;
  CSetting *I = NULL;
  if(ok)
    ok = (list != NULL);
  if(ok)
    ok = PyList_Check(list);
  if(ok) {
    I = SettingNew(G);
    size = PyList_Size(list);
    for(a = 0; a < size; a++) {
      if(ok)
        ok = set_list(I, PyList_GetItem(list, a));
    }
  }
  return (I);
}

/*========================================================================*/
int SettingFromPyList(CSetting * I, PyObject * list)
{
  int ok = true;
  ov_size size;
  ov_size a;

  if(ok)
    ok = (I != NULL);
  if(ok)
    ok = PyList_Check(list);
  if(ok) {
    size = PyList_Size(list);
    for(a = 0; a < size; a++) {
      if(!set_list(I, PyList_GetItem(list, a)))
        ok = false;
    }
  }
  return (ok);
}

/*========================================================================*/
/*
 * Get the indices of all settings that have changed since last calling
 * this function. Resets the "changed" flag.
 *
 * NOTE: assumes blocked interpreter
 *
 * name: object name or NULL/"" for global settings
 * state: object state
 */
std::vector<int> SettingGetUpdateList(PyMOLGlobals * G, const char * name, int state)
{
  CSetting **handle, *I = G->Setting;
  int a;
  int n;
  std::vector<int> result;

  if (name && name[0]) {
    // object-state settings

    CObject *obj = ExecutiveFindObjectByName(G, name);

    if (!obj ||
        !(handle = obj->fGetSettingHandle(obj, state)) ||
        !(I = *handle))
      // not found -> empty list
      return result;
  }

  n = VLAGetSize(I->info);
  for(a = 0; a < n; a++) {
    if(I->info[a].changed) {
      I->info[a].changed = false;
      result.push_back(a);
    }
  }
  return (result);

}


/*========================================================================*/
void SettingCheckHandle(PyMOLGlobals * G, CSetting ** handle)
{
  if(!*handle)
    *handle = SettingNew(G);
}


/*========================================================================*/
int SettingGetTextValue(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index,
                        char *buffer)
{
  const char * sptr = SettingGetTextPtr(G, set1, set2, index, buffer);
  if(!sptr)
    return 0;

  if (sptr != buffer) {
    if(strlen(sptr) > OrthoLineLength) {
      PRINTFB(G, FB_Setting, FB_Warnings)
        "Setting-Warning: text longer than OrthoLineLength" ENDFB(G);
    }

    strncpy(buffer, sptr, OrthoLineLength);
  }

  return 1;
}

/*========================================================================*/
/*
 * Returns a pointer to the internal string representation if available,
 * or it formats the value into buffer and returns a pointer to buffer.
 */
const char * SettingGetTextPtr(PyMOLGlobals * G, CSetting * set1, CSetting * set2,
                               int index, char *buffer)
{
  int type;
  char *sptr = NULL;
  float *ptr;
  type = SettingGetType(G, index);
  switch (type) {
  case cSetting_boolean:
    sprintf(buffer, SettingGet_b(G, set1, set2, index) ? "on" : "off");
    break;
  case cSetting_int:
    sprintf(buffer, "%d", SettingGet_i(G, set1, set2, index));
    break;
  case cSetting_float:
    sprintf(buffer, "%1.5f", SettingGet_f(G, set1, set2, index));
    break;
  case cSetting_float3:
    ptr = SettingGet_3fv(G, set1, set2, index);
    sprintf(buffer, "[ %1.5f, %1.5f, %1.5f ]", ptr[0], ptr[1], ptr[2]);
    break;
  case cSetting_color:
    {
      int color = SettingGet_color(G, set1, set2, index);
      switch (color) {
        case cColorAtomic:
          strcpy(buffer, "atomic");
          break;
        case cColorObject:
          strcpy(buffer, "object");
          break;
        case cColorFront:
          strcpy(buffer, "front");
          break;
        case cColorBack:
          strcpy(buffer, "back");
          break;
        case -1:
          strcpy(buffer, "default");
          break;
        default:
          sptr = ColorGetName(G, color);
          if(sptr)
            return sptr;
              strcpy(buffer, "invalid");
      }
    }
    break;
  case cSetting_string:
    return SettingGet_s(G, set1, set2, index);
    break;
  default:
    return NULL;
  }
  return buffer;
}


#ifndef _PYMOL_NOPY
/*========================================================================*/
int SettingSetFromTuple(PyMOLGlobals * G, CSetting * I, int index, PyObject * tuple)
/* must have interpret locked to make this call */
{
  PyObject *value;
  int type;
  int ok = true;
  if(!I)
    I = G->Setting;             /* fall back on global settings */

  /* this data structure has been pre-checked at the python level... */

  type = PyInt_AsLong(PyTuple_GetItem(tuple, 0));
  value = PyTuple_GetItem(tuple, 1);
  switch (type) {
  case cSetting_boolean:
    SettingSet_b(I, index, PyInt_AsLong(PyTuple_GetItem(value, 0)));
    break;
  case cSetting_int:
    SettingSet_i(I, index, PyInt_AsLong(PyTuple_GetItem(value, 0)));
    break;
  case cSetting_float:
    SettingSet_f(I, index, (float) PyFloat_AsDouble(PyTuple_GetItem(value, 0)));
    break;
  case cSetting_float3:
    SettingSet_3f(I, index,
                  (float) PyFloat_AsDouble(PyTuple_GetItem(value, 0)),
                  (float) PyFloat_AsDouble(PyTuple_GetItem(value, 1)),
                  (float) PyFloat_AsDouble(PyTuple_GetItem(value, 2)));
    break;
  case cSetting_color:
    SettingSet_color(I, index, PyString_AsString(PyTuple_GetItem(value, 0)));
    break;
  case cSetting_string:
    SettingSet_s(I, index, PyString_AsString(PyTuple_GetItem(value, 0)));
    break;
  default:
    ok = false;
    break;
  }
  return (ok);
}
#endif

/*========================================================================*/
int SettingStringToTypedValue(PyMOLGlobals * G, int index, const char *st, int *type,
                              int *value)
{
  int ok = true;
  int newvalue ;
  float newfvalue;
  /* this data structure has been pre-checked at the python level... */

  *type = SettingGetType(G, index);

  switch (*type) {
  case cSetting_boolean:
    if((!*st) || (*st == '0') || (*st == 'F') || WordMatchExact(G, st, "on", true)
       || WordMatchExact(G, st, "false", true)){
          newvalue = 0;
      } else {
          newvalue = 1;
      }
      if (newvalue != *value){
          *value = newvalue;
      } else {
          ok = false;
      }
    break;
  case cSetting_int:
    if(sscanf(st, "%d", &newvalue) != 1){
        ok = false;
    } else if (newvalue!=*value){
        *value = newvalue;
    } else {
        ok = false;
    }
    break;
  case cSetting_float:
    if(sscanf(st, "%f", &newfvalue) != 1){
        ok = false;
    } else if (newfvalue != *((float *) value)){
        *(float*)value = newfvalue;
    } else {
        ok = false;
    }
    break;
  case cSetting_color:
    {
      int color_index = ColorGetIndex(G, st);
      if (*(value) != color_index){
          *(value) = color_index;
      } else {
          ok = false;
      }
    }
    break;
  default:
    ok = false;
    break;
  }
  return (ok);
}

int SettingSetFromString(PyMOLGlobals * G, CSetting * I, int index, const char *st)
{
  int type;
  int ok = true;
  if(!I)
    I = G->Setting;             /* fall back on global settings */

  /* this data structure has been pre-checked at the python level... */

  type = SettingGetType(G, index);

  switch (type) {
  case cSetting_boolean:
    if((!*st) || (*st == '0') || (*st == 'F') || WordMatchExact(G, st, "on", true)
       || WordMatchExact(G, st, "false", true))
      SettingSet_b(I, index, 0);
    else
      SettingSet_b(I, index, 1);
    break;
  case cSetting_int:
    {
      int tmp;
      if(sscanf(st, "%d", &tmp) == 1)
        SettingSet_i(I, index, tmp);
      else
        ok = false;
    }
    break;
  case cSetting_float:
    {
      float tmp;
      if(sscanf(st, "%f", &tmp) == 1)
        SettingSet_f(I, index, tmp);
      else
        ok = false;
    }
    break;
  case cSetting_float3:
    {
      float tmp1, tmp2, tmp3;
      if(sscanf(st, "%f%f%f", &tmp1, &tmp2, &tmp3) == 3)
        SettingSet_3f(I, index, tmp1, tmp2, tmp3);
      else
        ok = false;
    }
    break;
  case cSetting_color:
    SettingSet_color(I, index, st);
    break;
  case cSetting_string:
    SettingSet_s(I, index, st);
    break;
  default:
    ok = false;
    break;
  }
  return (ok);
}


/*========================================================================*/
#ifndef _PYMOL_NOPY
PyObject *SettingGetPyObject(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{                               /* assumes blocked python interpeter */
  PyObject *result = NULL;
  float *ptr;
  int type = SettingGetType(G, index);

  switch (type) {
  case cSetting_boolean:
    result = CPythonVal_New_Boolean(SettingGet_b(G, set1, set2, index));
    break;
  case cSetting_int:
    result = CPythonVal_New_Integer(SettingGet_i(G, set1, set2, index));
    break;
  case cSetting_float:
    result = CPythonVal_New_Float(SettingGet_f(G, set1, set2, index));
    break;
  case cSetting_float3:
    ptr = SettingGet_3fv(G, set1, set2, index);
    result = Py_BuildValue("(fff)", ptr[0], ptr[1], ptr[2]);
    break;
  case cSetting_color:
    {
      int retcol = SettingGet_color(G, set1, set2, index);
      if (retcol > 0){
	float *col;
	col = ColorGet(G, retcol);
	result = Py_BuildValue("(fff)", col[0], col[1], col[2]);
      }
    }
    break;
  case cSetting_string:
    result = PyString_FromString(SettingGet_s(G, set1, set2, index));
    break;
  }
  return result;
}

PyObject *SettingGetTuple(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{                               /* assumes blocked python interpeter */
  PyObject *result = NULL;
  float *ptr;
  int type = SettingGetType(G, index);

  switch (type) {
  case cSetting_boolean:
    result = Py_BuildValue("(i(i))", type, SettingGet_b(G, set1, set2, index));
    break;
  case cSetting_int:
    result = Py_BuildValue("(i(i))", type, SettingGet_i(G, set1, set2, index));
    break;
  case cSetting_float:
    result = Py_BuildValue("(i(f))", type, SettingGet_f(G, set1, set2, index));
    break;
  case cSetting_float3:
    ptr = SettingGet_3fv(G, set1, set2, index);
    result = Py_BuildValue("(i(fff))", type, ptr[0], ptr[1], ptr[2]);
    break;
  case cSetting_color:
    result = Py_BuildValue("(i(i))", type, SettingGet_color(G, set1, set2, index));
    break;
  case cSetting_string:
    result = Py_BuildValue("(i(s))", type, SettingGet_s(G, set1, set2, index));
    break;
  default:
    result = PConvAutoNone(Py_None);
    break;
  }
  return result;
}
#endif


/*========================================================================*/
CSetting *SettingNew(PyMOLGlobals * G)
{
  OOAlloc(G, CSetting);
  SettingInit(G, I);
  return (I);
}


/*========================================================================*/
void SettingPurge(CSetting * I)
{
  if(I) {
    // need to free strings
    for(int index = 0; index < cSetting_INIT; ++index) {
      if (SettingInfo[index].type == cSetting_string) {
        I->info[index].delete_s();
      }
    }

    VLAFreeP(I->info);
    I->size = 0;
  }
}


/*========================================================================*/
void SettingFreeP(CSetting * I)
{
  if(I)
    SettingPurge(I);
  OOFreeP(I);
}


/*========================================================================*/
void SettingInit(PyMOLGlobals * G, CSetting * I)
{
  I->G = G;
  I->size = sizeof(int);        /* insures offset is never zero, except when undef */
  I->info = (SettingRec*) VLAMalloc(cSetting_INIT, sizeof(SettingRec), 5, 1); /* auto-zero */
}


/*========================================================================*/
bool SettingIsDefaultZero(int index)
{
  switch (SettingInfo[index].type) {
    case cSetting_boolean:
    case cSetting_int:
    case cSetting_float:
      if (SettingInfo[index].value.i[0] == 0)
        return true;
  }

  return false;
}


/*========================================================================*/
/*
 * Restore the default value from `src` or `SettingInfo`
 */
void SettingRestoreDefault(CSetting * I, int index, const CSetting * src)
{
  // 1) from stored default if provided
  if (src) {
    UtilCopyMem(I->info + index, src->info + index, sizeof(SettingRec));

    // need to properly copy strings
    if (SettingInfo[index].type == cSetting_string && src->info[index].str_) {
      I->info[index].str_ = new std::string(*src->info[index].str_);
    }

    return;
  }

  // 2) from SettingInfo
  auto &rec = SettingInfo[index];

  switch (rec.type) {
    case cSetting_blank:
      break;
    case cSetting_boolean:
    case cSetting_int:
      I->info[index].set_i(rec.value.i[0]);
      break;
    case cSetting_float:
      I->info[index].set_f(rec.value.f[0]);
      break;
    case cSetting_float3:
      I->info[index].set_3f(rec.value.f);
      break;
    case cSetting_string:
      I->info[index].delete_s();
      break;
    case cSetting_color:
      SettingSet_color(I, index, rec.value.s);
      break;
    default:
      // coding error
      printf(" ERROR: unkown type\n");
  };

  I->info[index].defined = false;
}


/*========================================================================*/
int SettingUnset(CSetting * I, int index)
{
  if(I) {
    SettingRec *sr = I->info + index;
    if (!sr->defined && sr->changed){
      return false;
    }
    sr->defined = false;
    sr->changed = true;
  }
  return true;
}


/*========================================================================*/
int SettingGetType(PyMOLGlobals * G, int index)
{
  return (SettingInfo[index].type);
}


/*========================================================================*/
static int get_i(CSetting * I, int index)
{
  PyMOLGlobals *G = I->G;
  int result;
  switch (SettingInfo[index].type) {
  case cSetting_boolean:
  case cSetting_int:
  case cSetting_color:
    result = I->info[index].int_;
    break;
  case cSetting_float:
    result = (int) I->info[index].float_;
    break;
  default:
    PRINTFB(G, FB_Setting, FB_Errors)
      "Setting-Error: type read mismatch (int) %d\n", index ENDFB(G);
    result = 0;
    break;
  }
  return (result);
}


/*========================================================================*/
static int get_b(CSetting * I, int index)
{
  int result;
  PyMOLGlobals *G = I->G;
  switch (SettingInfo[index].type) {
  case cSetting_boolean:
  case cSetting_int:
  case cSetting_color:
    result = I->info[index].int_;
    break;
  case cSetting_float:
    result = (int) I->info[index].float_;
    break;
  default:
    PRINTFB(G, FB_Setting, FB_Errors)
      "Setting-Error: type read mismatch (boolean) %d\n", index ENDFB(G);
    result = 0;
  }
  return (result);
}


/*========================================================================*/
static int get_color(CSetting * I, int index)
{
  int result;
  PyMOLGlobals *G = I->G;
  switch (SettingInfo[index].type) {
  case cSetting_boolean:
  case cSetting_int:
  case cSetting_color:
    result = I->info[index].int_;
    break;
  case cSetting_float:
    result = (int) I->info[index].float_;
    break;
  default:
    PRINTFB(G, FB_Setting, FB_Errors)
      "Setting-Error: type read mismatch (color) %d\n", index ENDFB(G);
    result = 0;
  }
  return (result);
}


/*========================================================================*/
static float get_f(CSetting * I, int index)
{
  float result;
  PyMOLGlobals *G = I->G;
  switch (SettingInfo[index].type) {
  case cSetting_boolean:
  case cSetting_int:
  case cSetting_color:
    result = (float) I->info[index].int_;
    break;
  case cSetting_float:
    result = I->info[index].float_;
    break;
  default:
    PRINTFB(G, FB_Setting, FB_Errors)
      "Setting-Error: type read mismatch (float) %d\n", index ENDFB(G);
    result = 0.0F;
  }
  return (result);
}


/*========================================================================*/
// should be `const char *`
static char *get_s(CSetting * I, int index)
{
  const char *result;
  PyMOLGlobals *G = I->G;
  switch (SettingInfo[index].type) {
  case cSetting_string:
    if(I->info[index].str_) {
      result = I->info[index].str_->c_str();
    } else {
      result = SettingInfo[index].value.s;
    }
    break;
  default:
    PRINTFB(G, FB_Setting, FB_Errors)
      "Setting-Error: type read mismatch (string) %d\n", index ENDFB(G);
    result = NULL;
  }
  return (char*) result;
}


/*========================================================================*/
int SettingSet_i(CSetting * I, int index, int value)
{
  int ok = true;
  if(I) {
    PyMOLGlobals *G = I->G;
    {
      int setting_type = SettingInfo[index].type;
      switch (setting_type) {
      case cSetting_boolean:
      case cSetting_int:
      case cSetting_color:
        I->info[index].set_i(value);
	break;
      case cSetting_float:
        I->info[index].set_f((float) value);
	break;
      default:
	PRINTFB(G, FB_Setting, FB_Errors)
	  "Setting-Error: type set mismatch (integer) %d\n", index ENDFB(G);
	ok = false;
      }
    }
  } else {
    ok = false;
  }
  return (ok);
}


/*========================================================================*/
static int SettingSet_color_from_3f(CSetting * I, int index, const float * vector)
{
  int color_index;
  float vals[3];
  copy3f(vector, vals);
  clamp3f(vals);
  color_index = Color3fToInt(I->G, vals);
  return SettingSet_i(I, index, color_index);
}

int SettingSet_color(CSetting * I, int index, const char *value)
{
  int ok = true;
  int color_index;
  if(I) {
    PyMOLGlobals *G = I->G;
    color_index = ColorGetIndex(G, value);
    if((color_index == -1) && (strcmp(value, "-1") &&
                               strcmp(value, "-2") &&
                               strcmp(value, "-3") &&
                               strcmp(value, "-4") &&
                               strcmp(value, "-5") && strcmp(value, "default"))) {
      float vals[3];
      ok = ParseFloat3List(value, vals);
      if (ok){
	clamp3f(vals);
	color_index = cColor_TRGB_Bits | 
	  ((int) (255 * vals[0] + 0.49999F)) << 16 | 
	  ((int) (255 * vals[1] + 0.49999F)) << 8 | 
	  ((int) (255 * vals[2] + 0.49999F));
      } else {
	PRINTFB(G, FB_Setting, FB_Errors)
	  "Setting-Error: unknown color '%s'\n", value ENDFB(G);
      }
    }
    if (ok){
      SettingSet_i(I, index, color_index);
    }
  }
  return (ok);
}


/*========================================================================*/
int SettingSet_f(CSetting * I, int index, float value)
{
  int ok = true;
  if(I) {
    PyMOLGlobals *G = I->G;
    {
      int setting_type = SettingInfo[index].type;
      switch (setting_type) {
      case cSetting_boolean:
      case cSetting_int:
      case cSetting_color:
        I->info[index].set_i((int) value);
	break;
      case cSetting_float:
        I->info[index].set_f(value);
	break;
      default:
	PRINTFB(G, FB_Setting, FB_Errors)
	  "Setting-Error: type set mismatch (float) %d\n", index ENDFB(G);
	ok = false;
      }
    }
  } else {
    ok = false;
  }
  return (ok);
}


/*========================================================================*/
int SettingSet_s(CSetting * I, int index, const char *value)
{
  int ok = true;
  if(I) {
    PyMOLGlobals *G = I->G;
    {
      int setting_type = SettingInfo[index].type;
      switch (setting_type) {
      case cSetting_string:
        I->info[index].set_s(value);
	break;
      case cSetting_color:
        return SettingSet_color(I, index, value);
      default:
	PRINTFB(G, FB_Setting, FB_Errors)
	  "Setting-Error: type set mismatch (string) %d\n", index ENDFB(G);
	ok = false;
      }
    }
  } else {
    ok = false;
  }
  return (ok);
}


/*========================================================================*/
int SettingSet_3f(CSetting * I, int index, float value1, float value2, float value3)
{
  int ok = false;
  if(I) {
    PyMOLGlobals *G = I->G;
    {
      switch (SettingInfo[index].type) {
      case cSetting_float3:
        I->info[index].set_3f(value1, value2, value3);
	break;
      default:
	PRINTFB(G, FB_Setting, FB_Errors)
	  "Setting-Error: type set mismatch (float3) %d\n", index ENDFB(G);
	ok = false;
      }
    }
  } else {
    ok = false;
  }
  return (ok);
}


/*========================================================================*/
int SettingSet_3fv(CSetting * I, int index, const float *vector)
{
  switch (SettingInfo[index].type) {
  case cSetting_float3:
    I->info[index].set_3f(vector);
    return true;
  case cSetting_color:
    return SettingSet_color_from_3f(I, index, vector);
  default:
    PyMOLGlobals *G = I->G;
    PRINTFB(G, FB_Setting, FB_Errors)
      "Setting-Error: type set mismatch (float3) %d\n", index ENDFB(G);
    return false;
  }
}


/*========================================================================*/
int SettingGetGlobal_b(PyMOLGlobals * G, int index)
{
  CSetting *I = G->Setting;
  return (get_b(I, index));
}


/*========================================================================*/
int SettingGetGlobal_i(PyMOLGlobals * G, int index)
{
  CSetting *I = G->Setting;
  return (get_i(I, index));
}


/*========================================================================*/
float SettingGetGlobal_f(PyMOLGlobals * G, int index)
{
  CSetting *I = G->Setting;
  return (get_f(I, index));
}


/*========================================================================*/
// should be `const char *`
char *SettingGetGlobal_s(PyMOLGlobals * G, int index)
{
  CSetting *I = G->Setting;
  return (get_s(I, index));
}

int SettingGetGlobal_color(PyMOLGlobals * G, int index)
{
  CSetting *I = G->Setting;
  return (get_color(I, index));
}


/*========================================================================*/
void SettingGetGlobal_3f(PyMOLGlobals * G, int index, float *value)
{
  CSetting *I = G->Setting;
  float *ptr;
  ptr = I->info[index].float3_;
  copy3f(ptr, value);
}


/*========================================================================*/
float *SettingGetGlobal_3fv(PyMOLGlobals * G, int index)
{
  CSetting *I = G->Setting;
  return I->info[index].float3_;
}


/*========================================================================*/
int SettingGet_b(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{
  if(set1) {
    if(set1->info[index].defined) {
      return (get_b(set1, index));
    }
  }
  if(set2) {
    if(set2->info[index].defined) {
      return (get_b(set2, index));
    }
  }
  return (SettingGetGlobal_b(G, index));
}


/*========================================================================*/
int SettingGetIfDefined_b(PyMOLGlobals * G, CSetting * set1, int index, int *value)
{
  int result = false;
  if(set1) {
    if(set1->info[index].defined) {
      *value = get_b(set1, index);
      result = true;
    }
  }
  return (result);
}


/*========================================================================*/
int SettingGetIfDefined_i(PyMOLGlobals * G, CSetting * set1, int index, int *value)
{
  int result = false;
  if(set1) {
    if(set1->info[index].defined) {
      *value = get_i(set1, index);
      result = true;
    }
  }
  return (result);
}


/*========================================================================*/
int SettingGetIfDefined_color(PyMOLGlobals * G, CSetting * set1, int index, int *value)
{
  int result = false;
  if(set1) {
    if(set1->info[index].defined) {
      *value = get_color(set1, index);
      result = true;
    }
  }
  return (result);
}


/*========================================================================*/
int SettingGetIfDefined_f(PyMOLGlobals * G, CSetting * set1, int index, float *value)
{
  int result = false;
  if(set1) {
    if(set1->info[index].defined) {
      *value = get_f(set1, index);
      result = true;
    }
  }
  return (result);
}


/*========================================================================*/
int SettingGetIfDefined_3fv(PyMOLGlobals * G, CSetting * set1, int index, float **value)
{
  int result = false;
  if(set1) {
    if(set1->info[index].defined) {
      (*value) = set1->info[index].float3_;
      result = true;
    }
  }
  return (result);
}


/*========================================================================*/
// `value` should be `const char **`
int SettingGetIfDefined_s(PyMOLGlobals * G, CSetting * set1, int index, char **value)
{
  int result = false;
  if(set1) {
    if(set1->info[index].defined) {
      *value = get_s(set1, index);
      result = true;
    }
  }
  return (result);
}


/*========================================================================*/
int SettingGet_i(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{
  if(set1) {
    if(set1->info[index].defined) {
      return (get_i(set1, index));
    }
  }
  if(set2) {
    if(set2->info[index].defined) {
      return (get_i(set2, index));
    }
  }
  return (SettingGetGlobal_i(G, index));
}


/*========================================================================*/
int SettingGet_color(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{
  if(set1) {
    if(set1->info[index].defined) {
      return (get_color(set1, index));
    }
  }
  if(set2) {
    if(set2->info[index].defined) {
      return (get_color(set2, index));
    }
  }
  return (SettingGetGlobal_color(G, index));
}


/*========================================================================*/
float SettingGet_f(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{
  if(set1) {
    if(set1->info[index].defined) {
      return (get_f(set1, index));
    }
  }
  if(set2) {
    if(set2->info[index].defined) {
      return (get_f(set2, index));
    }
  }
  return (SettingGetGlobal_f(G, index));
}


/*========================================================================*/
// should be `const char *`
char *SettingGet_s(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{
  if(set1) {
    if(set1->info[index].defined) {
      return (get_s(set1, index));
    }
  }
  if(set2) {
    if(set2->info[index].defined) {
      return (get_s(set2, index));
    }
  }
  return (SettingGetGlobal_s(G, index));
}


/*========================================================================*/
void SettingGet_3f(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index,
                   float *value)
{
  float *ptr;
  if(set1) {
    if(set1->info[index].defined) {
      ptr = set1->info[index].float3_;
      copy3f(ptr, value);
      return;
    }
  }
  if(set2) {
    if(set2->info[index].defined) {
      ptr = set2->info[index].float3_;
      copy3f(ptr, value);
      return;
    }
  }
  SettingGetGlobal_3f(G, index, value);
}


/*========================================================================*/
float *SettingGet_3fv(PyMOLGlobals * G, CSetting * set1, CSetting * set2, int index)
{
  if(set1) {
    if(set1->info[index].defined) {
      return set1->info[index].float3_;
    }
  }
  if(set2) {
    if(set2->info[index].defined) {
      return set2->info[index].float3_;
    }
  }
  return (SettingGetGlobal_3fv(G, index));
}


/*========================================================================*/

/*========================================================================*/
int SettingGetIndex(PyMOLGlobals * G, const char *name)
{
  OVreturn_word result = get_setting_id(G->PyMOL, name);

  if (OVreturn_IS_OK(result))
    return result.word;

  return -1;
}


/*========================================================================*/
int SettingGetName(PyMOLGlobals * G, int index, SettingName name)
{
  UtilNCopy(name, SettingInfo[index].name, sizeof(SettingName));
  return (name[0] != 0);
}

/*========================================================================*/
const char * SettingGetName(int index)
{
  return SettingInfo[index].name;
}

/*========================================================================*/
void SettingGenerateSideEffects(PyMOLGlobals * G, int index, const char *sele, int state, int quiet)
{
  const char *inv_sele = (sele && sele[0]) ? sele : cKeywordAll;
  auto &rec = SettingInfo[index];

  if (rec.level == cSettingLevel_unused) {
    const char * name = rec.name;

    if (!quiet && name && name[0]){
      PRINTFB(G, FB_Setting, FB_Warnings)
        " Setting-Warning: '%s' is no longer used\n", name
        ENDFB(G);
    }

    return;
  }

  // range check for int (global only)
  if (rec.type == cSetting_int && rec.hasMinMax() && !(sele && sele[0])) {
    int value = SettingGetGlobal_i(G, index);
    bool clamped = true;

    if (value < rec.value.i[1]) {
      value = rec.value.i[1];
    } else if (value > rec.value.i[2]) {
      value = rec.value.i[2];
    } else {
      clamped = false;
    }

    if (clamped) {
      PRINTFB(G, FB_Setting, FB_Warnings)
        " Setting-Warning: %s range = [%d,%d]; setting to %d.\n",
        rec.name, rec.value.i[1], rec.value.i[2], value ENDFB(G);
      SettingSetGlobal_i(G, index, value);
    }
  }

  switch (index) {
  case cSetting_stereo:
    SceneUpdateStereo(G);
    CShaderMgr_Set_Reload_Bits(G, RELOAD_ALL_SHADERS);
    break;
  case cSetting_pickable:
    ExecutiveInvalidateRep(G, inv_sele, cRepAll, cRepInvAll);
    SceneChanged(G);
    break;
  case cSetting_grid_mode:
    if (!SettingGetGlobal_i(G, cSetting_grid_mode))
      ShaderMgrResetUniformSet(G);
  case cSetting_grid_slot:
    ExecutiveInvalidateGroups(G, false);
    SceneChanged(G);
    break;
  case cSetting_grid_max:
    SceneChanged(G);
    break;
  case cSetting_defer_builds_mode:
    ExecutiveRebuildAll(G);
    break;
  case cSetting_seq_view:
  case cSetting_seq_view_label_spacing:
  case cSetting_seq_view_label_mode:
  case cSetting_seq_view_label_start:
  case cSetting_seq_view_format:
  case cSetting_seq_view_color:
  case cSetting_seq_view_unaligned_mode:
    SeqChanged(G);
    break;
  case cSetting_seq_view_fill_color:
  case cSetting_seq_view_fill_char:
  case cSetting_seq_view_label_color:
    OrthoDirty(G);
    break;
  case cSetting_show_frame_rate:
    OrthoDirty(G);
    break;
  case cSetting_group_full_member_names:
  case cSetting_group_arrow_prefix:
    OrthoDirty(G);
    break;
  case cSetting_static_singletons:
    SeqChanged(G);
    break;
  case cSetting_seq_view_location:
    PParse(G, "cmd.viewport(-1,-1)");
    SeqChanged(G);
    break;
  case cSetting_seq_view_overlay:
    PParse(G, "cmd.viewport(-1,-1)");
    break;
  case cSetting_stereo_mode:
  case cSetting_anaglyph_mode:
    SceneUpdateStereoMode(G);
    OrthoInvalidateDoDraw(G);
    OrthoDirty(G);
    PyMOL_NeedRedisplay(G->PyMOL);
    break;
  case cSetting_light_count:
  case cSetting_spec_count:
    CShaderMgr_Set_Reload_Bits(G, RELOAD_SHADERS_FOR_LIGHTING);
  case cSetting_light:
  case cSetting_light2:
  case cSetting_light3:
  case cSetting_light4:
  case cSetting_light5:
  case cSetting_light6:
  case cSetting_light7:
  case cSetting_dot_lighting:
  case cSetting_mesh_lighting:
  case cSetting_fog:
  case cSetting_field_of_view:
  case cSetting_fog_start:
  case cSetting_two_sided_lighting:
  case cSetting_transparency_mode:
  case cSetting_transparency_global_sort:
  case cSetting_dot_normals:
  case cSetting_mesh_normals:
    SceneInvalidate(G);
    break;
  case cSetting_spec_power:
    if (!quiet){
      PRINTFB(G, FB_Setting, FB_Debugging)
	"Setting-Details: spec_power is depreciated in PyMOL 1.5.  This option will not work in future versions. Please set shininess to set the specular exponent for movable light sources.\n"
	ENDFB(G);
    }
    SceneInvalidate(G);
    break;
  case cSetting_shininess:
  case cSetting_spec_reflect:
  case cSetting_spec_direct:
  case cSetting_spec_direct_power:
#ifdef PURE_OPENGL_ES_2
#endif
    if (SettingGetGlobal_b(G, cSetting_use_shaders)) {
      SceneInvalidate(G);
    }
    break;
  case cSetting_use_display_lists:
  case cSetting_simplify_display_lists:
  case cSetting_excl_display_lists_shaders:
    if (!quiet){
      PRINTFB(G, FB_Setting, FB_Debugging)
	"Setting-Details: display lists were depreciated in PyMOL 1.7.x.  The settings use_display_lists, simplify_display_lists, and excl_display_lists_shaders no longer work.\n"
	ENDFB(G);
    }
  case cSetting_use_shaders:
    {
      short changed = 0;
      if (SettingGetGlobal_b(G, cSetting_use_shaders)){
	if (SettingCheckUseShaders(G->Setting, quiet)){
	  return;
	}
      }
      SceneInvalidate(G);
      if (SettingGetGlobal_b(G, cSetting_sphere_use_shader)){
	ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_ribbon_use_shader)){
	ExecutiveInvalidateRep(G, inv_sele, cRepRibbon, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_nonbonded_use_shader)){
	ExecutiveInvalidateRep(G, inv_sele, cRepNonbonded, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_i(G, cSetting_nb_spheres_use_shader)){
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_dash_use_shader)){
	ExecutiveInvalidateRep(G, inv_sele, cRepAngle, cRepInvRep);
	ExecutiveInvalidateRep(G, inv_sele, cRepDihedral, cRepInvRep);
	ExecutiveInvalidateRep(G, inv_sele, cRepDash, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_line_use_shader)){
	ExecutiveInvalidateRep(G, inv_sele, cRepLine, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_cartoon_use_shader)){
	ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_cgo_use_shader) ||
          SettingGetGlobal_b(G, cSetting_stick_as_cylinders)){
	ExecutiveInvalidateRep(G, inv_sele, cRepCGO, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_stick_use_shader) ||
          SettingGetGlobal_b(G, cSetting_stick_as_cylinders) ||
	  (SettingGetGlobal_b(G, cSetting_stick_ball) && 
	   SettingGetGlobal_b(G, cSetting_valence))){
	ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
	changed = 1;
      }
      if (SettingGetGlobal_b(G, cSetting_surface_use_shader) ||
	  SettingGetGlobal_b(G, cSetting_dot_use_shader) || 
	  SettingGetGlobal_b(G, cSetting_mesh_use_shader)){
	changed = 1;
      }
      if (changed){
	SceneChanged(G);
	SceneUpdateObjectMoleculesSingleThread(G);
      }
    }
    break;
  case cSetting_stereo_shift:
  case cSetting_stereo_angle:
  case cSetting_stereo_dynamic_strength:
    SceneInvalidate(G);
    break;
  case cSetting_scene_buttons:
  case cSetting_scene_buttons_mode:
    SceneInvalidate(G);
    OrthoInvalidateDoDraw(G);
    break;
  case cSetting_dash_round_ends:
  case cSetting_dash_color:
    if (SettingGetGlobal_b(G, cSetting_use_shaders) && SettingGetGlobal_b(G, cSetting_dash_use_shader)){
      ExecutiveInvalidateRep(G, "all", cRepDash, cRepInvRep);
    }
    SceneInvalidate(G);
    break;
  case cSetting_angle_color:
    if (SettingGetGlobal_b(G, cSetting_use_shaders) && SettingGetGlobal_b(G, cSetting_dash_use_shader)){
      ExecutiveInvalidateRep(G, "all", cRepAngle, cRepInvRep);
    }
    SceneInvalidate(G);
    break;
  case cSetting_dihedral_color:
    if (SettingGetGlobal_b(G, cSetting_use_shaders) && SettingGetGlobal_b(G, cSetting_dash_use_shader)){
      ExecutiveInvalidateRep(G, "all", cRepDihedral, cRepInvRep);
    }
    SceneInvalidate(G);
    break;
  case cSetting_mouse_selection_mode:
    OrthoDirty(G);
    break;
  case cSetting_internal_gui_control_size:
    WizardRefresh(G);
    OrthoDirty(G);
    break;
  case cSetting_hide_underscore_names:
    ExecutiveInvalidateGroups(G, false);
    OrthoDirty(G);
    break;
  case cSetting_gradient_spacing:
  case cSetting_gradient_max_length:
  case cSetting_gradient_min_length:
  case cSetting_gradient_normal_min_dot:
  case cSetting_gradient_step_size:
  case cSetting_gradient_min_slope:
  case cSetting_gradient_symmetry:
    ExecutiveInvalidateRep(G, inv_sele, cRepMesh, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_min_mesh_spacing:
  case cSetting_mesh_grid_max:
  case cSetting_mesh_cutoff:
  case cSetting_mesh_carve_state:
  case cSetting_mesh_carve_cutoff:
  case cSetting_mesh_carve_selection:
  case cSetting_mesh_clear_state:
  case cSetting_mesh_clear_cutoff:
  case cSetting_mesh_clear_selection:
  case cSetting_mesh_mode:
  case cSetting_mesh_type:
  case cSetting_mesh_solvent:
  case cSetting_mesh_quality:
  case cSetting_mesh_skip:
    ExecutiveInvalidateRep(G, inv_sele, cRepMesh, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_valence:
  case cSetting_valence_mode:
  case cSetting_valence_size:
  case cSetting_half_bonds:
  case cSetting_line_stick_helper:
  case cSetting_hide_long_bonds:
    ExecutiveInvalidateRep(G, inv_sele, cRepLine, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_stick_transparency:
  case cSetting_stick_debug:
  case cSetting_stick_round_nub:
  case cSetting_stick_as_cylinders:
  case cSetting_stick_good_geometry:
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_line_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      ExecutiveInvalidateRep(G, inv_sele, cRepRibbon, cRepInvRep);
      SceneChanged(G);
      if (SettingGetGlobal_b(G, cSetting_line_use_shader)){
	SceneUpdateObjectMoleculesSingleThread(G);
      }
    }
    break;
  case cSetting_ribbon_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      ExecutiveInvalidateRep(G, inv_sele, cRepRibbon, cRepInvRep);
      SceneChanged(G);
    }
    break;
  case cSetting_dot_as_spheres:
    SceneInvalidate(G);
    SceneChanged(G);
    break;
  case cSetting_dot_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      ExecutiveInvalidateRep(G, inv_sele, cRepDot, cRepInvRep);
      SceneChanged(G);
    }
    break;
  case cSetting_nonbonded_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      ExecutiveInvalidateRep(G, inv_sele, cRepNonbonded, cRepInvRep);
      SceneChanged(G);
    }
    break;
  case cSetting_nb_spheres_size:
    ExecutiveInvalidateRep(G, inv_sele, cRepNonbondedSphere, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_nb_spheres_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      SceneChanged(G);
    }
    break;
  case cSetting_render_as_cylinders:
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
  case cSetting_mesh_as_cylinders:
  case cSetting_line_as_cylinders:
  case cSetting_ribbon_as_cylinders:
  case cSetting_dash_as_cylinders:
    if (index == cSetting_dash_as_cylinders) {
      ExecutiveInvalidateRep(G, inv_sele, cRepAngle, cRepInvRep);
      ExecutiveInvalidateRep(G, inv_sele, cRepDihedral, cRepInvRep);
    }
  case cSetting_nonbonded_as_cylinders:
  case cSetting_alignment_as_cylinders:
  case cSetting_cartoon_nucleic_acid_as_cylinders:
    if (SettingGetGlobal_b(G, cSetting_render_as_cylinders)){
      if (!CShaderMgr_ShaderPrgExists(G->ShaderMgr, "cylinder")){
	SettingSet_b(G->Setting, cSetting_render_as_cylinders, 0);
	if (!quiet){
	  PRINTFB(G, FB_Setting, FB_Warnings)
	    "Setting-Error: render_as_cylinders cannot be set when the Cylinder Shader is not available, setting render_as_cylinder back to false\n"
	    ENDFB(G);
	}
	return;
      }
      switch (index){
      case cSetting_render_as_cylinders:
      case cSetting_cartoon_nucleic_acid_as_cylinders:
      case cSetting_use_shaders:
	if (SettingGetGlobal_b(G, cSetting_use_shaders) && SettingGetGlobal_b(G, cSetting_render_as_cylinders)){
	  ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
	}
      }
      SceneChanged(G);
    }
    break;
  case cSetting_mesh_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      SceneChanged(G);
      if (SettingGetGlobal_b(G, cSetting_mesh_use_shader)){
	SceneUpdateObjectMoleculesSingleThread(G);
      }
    }
    break;
  case cSetting_slice_height_scale:
  case cSetting_slice_height_map:
  case cSetting_slice_grid:
  case cSetting_slice_dynamic_grid:
  case cSetting_slice_dynamic_grid_resolution:
    ExecutiveInvalidateRep(G, inv_sele, cRepSlice, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_label_font_id:
  case cSetting_label_size:
    ExecutiveInvalidateRep(G, inv_sele, cRepLabel, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_retain_order:
  case cSetting_pdb_hetatm_sort:
  case cSetting_pdb_insertions_go_first:
    ExecutiveSort(G, inv_sele);
    break;
  case cSetting_roving_lines:
  case cSetting_roving_sticks:
  case cSetting_roving_spheres:
  case cSetting_roving_labels:
  case cSetting_roving_selection:
  case cSetting_roving_ribbon:
  case cSetting_roving_cartoon:
  case cSetting_roving_polar_contacts:
  case cSetting_roving_polar_cutoff:
  case cSetting_roving_nonbonded:
  case cSetting_roving_nb_spheres:
  case cSetting_roving_map1_level:
  case cSetting_roving_map2_level:
  case cSetting_roving_map3_level:
  case cSetting_roving_map1_name:
  case cSetting_roving_map2_name:
  case cSetting_roving_map3_name:
  case cSetting_roving_isosurface:
  case cSetting_roving_isomesh:
    SceneRovingChanged(G);
    break;
  case cSetting_roving_byres:
  case cSetting_roving_detail:
    SceneRovingDirty(G);
    break;
  case cSetting_dash_length:
  case cSetting_dash_gap:
  case cSetting_dash_radius:
  case cSetting_dash_width:
  case cSetting_angle_size:
  case cSetting_label_digits:
  case cSetting_label_distance_digits:
  case cSetting_label_angle_digits:
  case cSetting_label_dihedral_digits:
  case cSetting_angle_label_position:
  case cSetting_dihedral_size:
  case cSetting_dihedral_label_position:
    ExecutiveRebuildAllObjectDist(G);
    SceneChanged(G);
    break;
  case cSetting_button_mode:
    EditorMouseInvalid(G);
    OrthoDirty(G);
    break;
  case cSetting_stick_radius:
  case cSetting_stick_h_scale:
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);       /* base width */
    SceneChanged(G);
    break;
  case cSetting_nb_spheres_quality:
    {
      ExecutiveInvalidateRep(G, inv_sele, cRepNonbondedSphere, cRepInvRep);
      SceneChanged(G);
    }
  case cSetting_cgo_sphere_quality:
  case cSetting_cgo_debug:
    {
      ExecutiveInvalidateRep(G, inv_sele, cRepCGO, cRepInvRep);
      SceneChanged(G);
      break;
    }
  case cSetting_stick_quality:
  case cSetting_stick_ball:
  case cSetting_stick_nub:
  case cSetting_stick_ball_ratio:
  case cSetting_stick_ball_color:
  case cSetting_stick_fixed_radius:
  case cSetting_stick_valence_scale:
  case cSetting_stick_overlap:
  case cSetting_stick_color:
  case cSetting_stick_use_shader:
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    SceneChanged(G);
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      if (SettingGetGlobal_b(G, cSetting_stick_use_shader)){
	SceneUpdateObjectMoleculesSingleThread(G);
      }
    }
    break;
  case cSetting_clamp_colors:
  case cSetting_ramp_blend_nearby_colors:
    ExecutiveInvalidateRep(G, inv_sele, cRepAll, cRepInvColor);
    SceneChanged(G);
    break;
  case cSetting_label_color:
  case cSetting_label_outline_color:
  case cSetting_label_position:
    ExecutiveRebuildAllObjectDist(G);
    ExecutiveInvalidateRep(G, inv_sele, cRepLabel, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_cartoon_color:
    ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_ribbon_color:
    ExecutiveInvalidateRep(G, inv_sele, cRepRibbon, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_cgo_line_width:
  case cSetting_line_width:    
  case cSetting_line_color:
  case cSetting_line_radius:
    ExecutiveInvalidateRep(G, inv_sele, cRepLine, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepNonbonded, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_scenes_changed:
    {
      int scene_buttons = SettingGetGlobal_i(G, cSetting_scene_buttons);
      if (scene_buttons)
	OrthoInvalidateDoDraw(G);
    }
    break;
  case cSetting_button_mode_name:
    {
      int internal_gui_mode = SettingGetGlobal_i(G, cSetting_internal_gui_mode);
      if (internal_gui_mode){
	OrthoInvalidateDoDraw(G);
      }
    }
      break;
  case cSetting_scene_current_name:
    SceneRestartFrameTimer(G);
    break;
  case cSetting_mesh_width:
    ExecutiveInvalidateRep(G, inv_sele, cRepMesh, cRepInvColor);
    SceneChanged(G);
    break;
  case cSetting_ellipsoid_probability:
  case cSetting_ellipsoid_scale:
  case cSetting_ellipsoid_color:
  case cSetting_ellipsoid_transparency:
    ExecutiveInvalidateRep(G, inv_sele, cRepEllipsoid, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_ellipsoid_quality:
  case cSetting_cgo_ellipsoid_quality:
    ExecutiveInvalidateRep(G, inv_sele, cRepCGO, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepEllipsoid, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_mesh_color:
  case cSetting_mesh_negative_color:
    ExecutiveInvalidateRep(G, inv_sele, cRepMesh, cRepInvColor);
    SceneChanged(G);
    break;
  case cSetting_ray_color_ramps:
    ExecutiveInvalidateRep(G, inv_sele, cRepAll, cRepInvColor);
    SceneChanged(G);
    break;

  case cSetting_cull_spheres:
  case cSetting_sphere_scale:
  case cSetting_sphere_transparency:
  case cSetting_sphere_solvent:
  case cSetting_sphere_mode:
  case cSetting_sphere_point_max_size:
  case cSetting_sphere_point_size:
  case cSetting_sphere_use_shader:
    ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
    SceneInvalidate(G);
    break;
  case cSetting_sphere_quality:
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepNonbondedSphere, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_nonbonded_size:
  case cSetting_nonbonded_transparency:
    ExecutiveInvalidateRep(G, inv_sele, cRepNonbonded, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepNonbondedSphere, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_mesh_radius:
    ExecutiveInvalidateRep(G, inv_sele, cRepMesh, cRepInvColor);
    SceneChanged(G);
    break;
  case cSetting_ambient_occlusion_scale:
    SceneChanged(G);
    break;
  case cSetting_ambient_occlusion_mode:
    ExecutiveInvalidateRep(G, inv_sele, cRepSurface, cRepInvColor);
    switch (SettingGetGlobal_i(G, cSetting_ambient_occlusion_mode) % 4){
    case 1:
      SettingSetGlobal_i(G, cSetting_ambient_occlusion_smooth, 10);
      SettingSetGlobal_f(G, cSetting_ambient_occlusion_scale, 25.f);
      break;
    case 2:
      SettingSetGlobal_i(G, cSetting_ambient_occlusion_smooth, 5);
      SettingSetGlobal_f(G, cSetting_ambient_occlusion_scale, .9f);
      break;
    case 3:
      SettingSetGlobal_i(G, cSetting_ambient_occlusion_smooth, 5);
      SettingSetGlobal_f(G, cSetting_ambient_occlusion_scale, 1.1f);
      break;
    }
    SceneChanged(G);
    break;
  case cSetting_ambient_occlusion_smooth:
  case cSetting_surface_negative_color:
  case cSetting_surface_color:
  case cSetting_surface_ramp_above_mode:
  case cSetting_transparency:
    ExecutiveInvalidateRep(G, inv_sele, cRepSurface, cRepInvColor);
    SceneChanged(G);
    break;
  case cSetting_dot_color:
    ExecutiveInvalidateRep(G, inv_sele, cRepDot, cRepInvColor);
    SceneChanged(G);
    break;
  case cSetting_sphere_color:
    ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvColor);
    SceneChanged(G);
    break;

  case cSetting_surface_quality:
  case cSetting_surface_mode:
  case cSetting_surface_normal:
  case cSetting_surface_type:
  case cSetting_surface_carve_state:
  case cSetting_surface_carve_cutoff:
  case cSetting_surface_carve_selection:
  case cSetting_surface_carve_normal_cutoff:
  case cSetting_surface_clear_state:
  case cSetting_surface_clear_cutoff:
  case cSetting_surface_clear_selection:
  case cSetting_surface_trim_cutoff:
  case cSetting_surface_trim_factor:
  case cSetting_surface_circumscribe:
  case cSetting_surface_solvent:
  case cSetting_surface_proximity:
  case cSetting_surface_cavity_mode:   
  case cSetting_surface_cavity_radius:
  case cSetting_surface_cavity_cutoff:   
  case cSetting_cavity_cull:
    ExecutiveInvalidateRep(G, inv_sele, cRepSurface, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_surface_use_shader:
    SceneChanged(G);
    break;
  case cSetting_surface_negative_visible:
    ExecutiveInvalidateRep(G, inv_sele, cRepSurface, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_mesh_negative_visible:
    ExecutiveInvalidateRep(G, inv_sele, cRepMesh, cRepInvAll);
    SceneChanged(G);
    break;
  case cSetting_solvent_radius:
    ExecutiveInvalidateRep(G, inv_sele, cRepSurface, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepMesh, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepDot, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_trace_atoms_mode:
    ExecutiveInvalidateRep(G, inv_sele, cRepRibbon, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_ribbon_smooth:
  case cSetting_ribbon_power:
  case cSetting_ribbon_power_b:
  case cSetting_ribbon_sampling:
  case cSetting_ribbon_radius:
  case cSetting_ribbon_width:
  case cSetting_ribbon_throw:
  case cSetting_ribbon_trace_atoms:
  case cSetting_ribbon_transparency:
    ExecutiveInvalidateRep(G, inv_sele, cRepRibbon, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_draw_mode:
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
    break;
  case cSetting_cartoon_side_chain_helper:
  case cSetting_cartoon_nucleic_acid_mode:
    ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepLine, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_ribbon_side_chain_helper:
  case cSetting_ribbon_nucleic_acid_mode:
    ExecutiveInvalidateRep(G, inv_sele, cRepRibbon, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepLine, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepCyl, cRepInvRep);
    ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_cartoon_transparency:
  case cSetting_cartoon_ring_transparency:
  case cSetting_cartoon_trace_atoms:
  case cSetting_cartoon_refine:
  case cSetting_cartoon_nucleic_acid_color:
  case cSetting_cartoon_ring_mode:
  case cSetting_cartoon_ring_finder:
  case cSetting_cartoon_ring_width:
  case cSetting_cartoon_ring_color:
  case cSetting_cartoon_ladder_mode:
  case cSetting_cartoon_ladder_radius:
  case cSetting_cartoon_ladder_color:
  case cSetting_cartoon_sampling:
  case cSetting_cartoon_loop_quality:
  case cSetting_ray_trace_mode:        /* affects loop quality */
  case cSetting_cartoon_loop_radius:
  case cSetting_cartoon_loop_cap:
  case cSetting_cartoon_tube_quality:
  case cSetting_cartoon_tube_radius:
  case cSetting_cartoon_tube_cap:
  case cSetting_cartoon_putty_quality:
  case cSetting_cartoon_putty_radius:
  case cSetting_cartoon_putty_range:
  case cSetting_cartoon_putty_scale_min:
  case cSetting_cartoon_putty_scale_max:
  case cSetting_cartoon_putty_scale_power:
  case cSetting_cartoon_putty_transform:
  case cSetting_cartoon_power:
  case cSetting_cartoon_power_b:
  case cSetting_cartoon_ring_radius:
  case cSetting_cartoon_rect_length:
  case cSetting_cartoon_rect_width:
  case cSetting_cartoon_oval_length:
  case cSetting_cartoon_oval_width:
  case cSetting_cartoon_oval_quality:
  case cSetting_cartoon_round_helices:
  case cSetting_cartoon_flat_sheets:
  case cSetting_cartoon_refine_normals:
  case cSetting_cartoon_smooth_loops:
  case cSetting_cartoon_dumbbell_width:
  case cSetting_cartoon_dumbbell_length:
  case cSetting_cartoon_dumbbell_radius:
  case cSetting_cartoon_fancy_helices:
  case cSetting_cartoon_fancy_sheets:
  case cSetting_cartoon_cylindrical_helices:
  case cSetting_cartoon_refine_tips:
  case cSetting_cartoon_helix_radius:
  case cSetting_cartoon_throw:
  case cSetting_cartoon_debug:
  case cSetting_cartoon_highlight_color:
  case cSetting_cartoon_discrete_colors:
  case cSetting_cartoon_smooth_first:
  case cSetting_cartoon_smooth_last:
  case cSetting_cartoon_smooth_cycles:
  case cSetting_cartoon_flat_cycles:
  case cSetting_cartoon_gap_cutoff:
    ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_cartoon_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
      SceneChanged(G);
      if (SettingGetGlobal_b(G, cSetting_cartoon_use_shader)){
	SceneUpdateObjectMoleculesSingleThread(G);
      }
    }
    break;
  case cSetting_cgo_use_shader:
    if (SettingGetGlobal_b(G, cSetting_use_shaders)){
      ExecutiveInvalidateRep(G, inv_sele, cRepCGO, cRepInvRep);
      SceneChanged(G);
      if (SettingGetGlobal_b(G, cSetting_cgo_use_shader)){
	SceneUpdateObjectMoleculesSingleThread(G);
      }
    }
    break;
  case cSetting_cgo_shader_ub_flags:
    if (SettingGetGlobal_b(G, cSetting_use_shaders) && SettingGetGlobal_b(G, cSetting_cgo_use_shader)){
      ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
      SceneChanged(G);
      SceneUpdateObjectMoleculesSingleThread(G);
    }
    break;
  case cSetting_cgo_shader_ub_color:
    if (SettingGetGlobal_b(G, cSetting_use_shaders) && SettingGetGlobal_b(G, cSetting_cgo_use_shader)){
      ExecutiveInvalidateRep(G, inv_sele, cRepCGO, cRepInvRep);
      ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
      ExecutiveInvalidateRep(G, inv_sele, cRepSphere, cRepInvRep);
      SceneChanged(G);
      SceneUpdateObjectMoleculesSingleThread(G);
    }
    break;
  case cSetting_cgo_shader_ub_normal:
    if (SettingGetGlobal_b(G, cSetting_use_shaders) && SettingGetGlobal_b(G, cSetting_cgo_use_shader)){
      ExecutiveInvalidateRep(G, inv_sele, cRepCGO, cRepInvRep);
      ExecutiveInvalidateRep(G, inv_sele, cRepCartoon, cRepInvRep);
      SceneChanged(G);
      SceneUpdateObjectMoleculesSingleThread(G);
    }
    break;
  case cSetting_dot_width:
  case cSetting_dot_radius:
  case cSetting_dot_density:
  case cSetting_dot_mode:
  case cSetting_dot_solvent:
  case cSetting_dot_hydrogens:
  case cSetting_trim_dots:
    ExecutiveInvalidateRep(G, inv_sele, cRepDot, cRepInvRep);
    SceneChanged(G);
    break;
  case cSetting_bg_gradient:
    if (SettingGetGlobal_b(G, cSetting_bg_gradient)){
      ColorUpdateFrontFromSettings(G);
      ExecutiveInvalidateRep(G, inv_sele, cRepAll, cRepInvColor);
    }
    CShaderMgr_Set_Reload_Bits(G, RELOAD_SHADERS_UPDATE_FOR_BACKGROUND);
    SceneChanged(G);
    break;
  case cSetting_bg_rgb_top:
  case cSetting_bg_rgb_bottom:
    {
      /* clamp this value */
      if(
          SettingGetGlobal_b(G, cSetting_bg_gradient) && !OrthoBackgroundDataIsSet(G)) {
        ColorUpdateFrontFromSettings(G);
	ExecutiveInvalidateRep(G, inv_sele, cRepAll, cRepInvColor);
	OrthoBackgroundTextureNeedsUpdate(G);
      }
    }
    SceneChanged(G);
    break;
  case cSetting_bg_rgb:
    {
      /* clamp this value */
      float *v = ColorGet(G, SettingGet_color(G, NULL, NULL, cSetting_bg_rgb));
      {
	if(!OrthoBackgroundDataIsSet(G)) {
	  ColorUpdateFront(G, v);
	  ExecutiveInvalidateRep(G, inv_sele, cRepAll, cRepInvColor);
	}
      }
    }
    SceneChanged(G);
    break;
  case cSetting_selection_width:
  case cSetting_selection_width_scale:
  case cSetting_selection_width_max:
    ExecutiveInvalidateSelectionIndicatorsCGO(G);
  case cSetting_line_smooth:
  case cSetting_ortho:
    CShaderMgr_Set_Reload_Bits(G, RELOAD_ALL_SHADERS);
  case cSetting_reflect:
  case cSetting_direct:
  case cSetting_ambient:
  case cSetting_specular:
  case cSetting_specular_intensity:
    SceneInvalidate(G);
    break;
  case cSetting_depth_cue:
    SceneInvalidate(G);
    break;
  case cSetting_sculpting:
    OrthoDirty(G);
    break;
  case cSetting_auto_overlay:
    OrthoRemoveAutoOverlay(G); /* always start clean */
    break;
  case cSetting_overlay:
  case cSetting_overlay_lines:
  case cSetting_text:
    OrthoDirty(G);
    break;
  case cSetting_internal_gui_mode:
  case cSetting_internal_gui_width:
  case cSetting_internal_gui:
  case cSetting_internal_feedback:
  case cSetting_mouse_grid:
  case cSetting_movie_panel_row_height:
  case cSetting_movie_panel:
    if(!SettingGetGlobal_b(G, cSetting_suspend_updates)) {
      OrthoCommandIn(G, "viewport");
    }
    break;
  case cSetting_suspend_updates:
    if(!SettingGetGlobal_b(G, cSetting_suspend_updates)) {
      SceneChanged(G);          /* force big update upon resumption */
      OrthoDirty(G);
    }
    break;
  case cSetting_security:
    G->Security = SettingGetGlobal_i(G, cSetting_security);
    break;
  case cSetting_all_states:
  case cSetting_state:
  case cSetting_frame:
    ExecutiveInvalidateSelectionIndicatorsCGO(G);
    SceneChanged(G);
    break;
  case cSetting_dynamic_width:
  case cSetting_dynamic_width_factor:
  case cSetting_dynamic_width_min:
  case cSetting_dynamic_width_max:
    SceneChanged(G);
    break;
  case cSetting_rock:
  case cSetting_sweep_mode:
  case cSetting_sweep_phase:
  case cSetting_sweep_angle:
  case cSetting_sweep_speed:
    SceneRestartSweepTimer(G);
    break;
  case cSetting_motion_power:
  case cSetting_motion_bias:
  case cSetting_motion_simple:
  case cSetting_motion_linear:
  case cSetting_motion_hand:
  case cSetting_movie_loop:
    if(SettingGetGlobal_b(G, cSetting_movie_auto_interpolate)) {
      ExecutiveMotionReinterpolate(G);
    }
    break;
  case cSetting_volume_bit_depth:
    ExecutiveInvalidateRep(G, inv_sele, cRepVolume, cRepInvAll);
    SceneInvalidate(G);
    break;
  case cSetting_volume_layers:
    ExecutiveInvalidateRep(G, inv_sele, cRepVolume, cRepInvColor);
    SceneInvalidate(G);
    break;
  case cSetting_cgo_transparency:
    SceneInvalidate(G);
    SceneChanged(G);
    break;
  case cSetting_surface_color_smoothing:
  case cSetting_surface_color_smoothing_threshold:
    ExecutiveInvalidateRep(G, inv_sele, cRepSurface, cRepInvColor);    
    SceneChanged(G);
    break;
  case cSetting_smooth_half_bonds:
    SceneChanged(G);
    break;    
  case cSetting_selection_round_points:
    ExecutiveInvalidateSelectionIndicatorsCGO(G);
    break;
  case cSetting_antialias_shader:
  case cSetting_atom_type_format:
  case cSetting_bg_image_mode:
  case cSetting_bg_image_filename:
  case cSetting_bg_image_linear:
  case cSetting_bg_image_tilesize:
  case cSetting_chromadepth:
  case cSetting_dash_transparency:
  case cSetting_label_bg_color:
  case cSetting_label_bg_outline:
  case cSetting_label_bg_transparency:
  case cSetting_label_connector:
  case cSetting_label_connector_color:
  case cSetting_label_connector_ext_length:
  case cSetting_label_connector_mode:
  case cSetting_label_connector_width:
  case cSetting_label_multiline_justification:
  case cSetting_label_multiline_spacing:
  case cSetting_label_padding:
  case cSetting_label_placement_offset:
  case cSetting_label_relative_mode:
  case cSetting_label_screen_point:
  case cSetting_label_z_target:
  case cSetting_load_atom_props_default:
  case cSetting_load_object_props_default:
  case cSetting_pick_labels:
  case cSetting_precomputed_lighting:
  case cSetting_ray_label_connector_flat:
  case cSetting_session_embeds_data:
  case cSetting_shaders_from_disk:
  case cSetting_suspend_undo:
  case cSetting_use_geometry_shaders:
  case cSetting_volume_mode:
    PRINTFB(G, FB_Setting, FB_Warnings)
      " Setting-Warning: %s is not supported in Open-Source version of PyMOL\n",
      SettingInfo[index].name
      ENDFB(G);
  default:
    break;
  }
}


/*========================================================================*/
int SettingSetGlobal_b(PyMOLGlobals * G, int index, int value)
{
  return (SettingSet_b(G->Setting, index, value));
}


/*========================================================================*/
int SettingSetGlobal_i(PyMOLGlobals * G, int index, int value)
{
  return (SettingSet_i(G->Setting, index, value));
}

/*========================================================================*/
int SettingSetGlobal_s(PyMOLGlobals * G, int index, const char *value)
{
  return (SettingSet_s(G->Setting, index, value));
}

/*========================================================================*/
int SettingSetGlobal_f(PyMOLGlobals * G, int index, float value)
{
  return (SettingSet_f(G->Setting, index, value));
}


/*========================================================================*/
int SettingSetGlobal_3f(PyMOLGlobals * G, int index, float value1, float value2,
                        float value3)
{
  return (SettingSet_3f(G->Setting, index, value1, value2, value3));
}


/*========================================================================*/
void SettingFreeGlobal(PyMOLGlobals * G)
{
  CSetting *I = G->Setting;
  SettingUniqueFree(G);
  SettingPurge(I);
  if(G->Default) {
    SettingPurge(G->Default);
    FreeP(G->Default);
  }
  FreeP(G->Setting);
}


/*========================================================================*/
void SettingInitGlobal(PyMOLGlobals * G, int alloc, int reset_gui, int use_default)
{
  CSetting *I = G->Setting;

  /* use function pointers to prevent the compiler from inlining every
     call in this block (a waste of RAM and time) */

  int (*set_i) (CSetting * I, int index, int value) = SettingSet_i;
  int (*set_b) (CSetting * I, int index, int value) = SettingSet_b;

  if(alloc || !I) {
    I = (G->Setting = Calloc(CSetting, 1));
    SettingUniqueInit(G);
    SettingInit(G, I);
  }

  if(G->Default && use_default) {

    SettingCopyAll(G, G->Default, G->Setting);

  } else {

    // copy defaults from SettingInfo table
    for(int index = 0; index < cSetting_INIT; ++index) {
      if (!reset_gui) switch (index) {
        case cSetting_internal_gui_width:
        case cSetting_internal_gui:
          continue;
      }

      SettingRestoreDefault(I, index);
    }

    // open-source has no volume_mode=1
    set_i(I, cSetting_volume_mode, 0);

    // command line arguments overwrites
    set_b(I, cSetting_auto_show_lines, G->Option->sphere_mode < 0);
    set_i(I, cSetting_auto_zoom, G->Option->zoom_mode);
    set_b(I, cSetting_auto_show_nonbonded, G->Option->sphere_mode < 0);
    set_b(I, cSetting_presentation, G->Option->presentation);
    set_i(I, cSetting_defer_builds_mode, G->Option->defer_builds_mode);
    set_b(I, cSetting_presentation_auto_quit, !G->Option->no_quit);
    set_b(I, cSetting_auto_show_spheres, G->Option->sphere_mode >= 0);
    set_i(I, cSetting_internal_feedback, G->Option->internal_feedback);

    if(G->Option->stereo_mode) {
      set_i(I, cSetting_stereo_mode, G->Option->stereo_mode);
    } else if(G->StereoCapable || G->Option->blue_line) {
      set_i(I, cSetting_stereo_mode, 1);      /* quadbuffer if we can */
    }

    /* In order to get electrostatic potentials in kT from the Coulomb equation... 

       PyMOL charges: Q, in e
       PyMOL distances: r, in Angstrom
       Coulomb Constant: K = 8.987552e9 ((J*m)/(C^2))
       Angstrom Convertor: 1 A = 1e-10 m
       Coulomb Convertor: 1 e = 1.60217733e-19 C
       Angstrom Convertor: 1 A = 10e-10 m
       Dielectric Constant: D (unitless)

       ePot = (KQ)/(Dr) = 

       8.987552e9 J*m     1.6021773e-19 C   1.6021773e-19 C      1 A        Q  
       ---------------- * --------------- * --------------- * ---------- * --- =
       C^2              1 e               1 e            1e-10 m     Dr

       2.3070795237e-18 J*A      Q
       = ---------------------- * ---
       e^2               Dr

       Boltzmann Constant: k = 1.380658e-23 (J/K)
       Temperature: 300 Kelvin

       kT = 1.380658e-23 * 300 = 4.141974e-21 J

       2.3070795237e-18 J*A         1 kT             Q
       ePot = --------------------- * ------------------ * ---
       e^2              4.141974e-21 J       Dr

       557.00000 kT*A    Q
       ePot = -------------- * --- which will give kT/e units when applied
       e^2           Dr
     */

#ifdef WIN32
    /* BEGIN PROPRIETARY CODE SEGMENT (see disclaimer in "os_proprietary.h") */
    set_b(I, cSetting_cache_display, 0);

#ifndef _PYMOL_ACTIVEX
    {
      SYSTEM_INFO SysInfo;
      GetSystemInfo(&SysInfo);
      {
        DWORD count = SysInfo.dwNumberOfProcessors;
        if(count > 1) {
          set_i(I, cSetting_max_threads, count);
        }
      }
    }
    /* END PROPRIETARY CODE SEGMENT */
#endif
#endif

#ifndef _PYMOL_FREETYPE
    set_i(I, cSetting_label_font_id, 0);
#endif

#ifdef _PYMOL_IOS
#endif
  }
  CShaderMgr_Set_Reload_Bits(G, RELOAD_ALL_SHADERS);
}

/*
 * State index iterator constructor, see Setting.h for documentation.
 */
StateIterator::StateIterator(PyMOLGlobals * G, CSetting * set, int state_, int nstate) {
  if(state_ == -2) {
    // current state
    state = SettingGet_i(G, set, NULL, cSetting_state) - 1;
    end = state + 1;
  } else if(state_ == -1) {
    // all states
    state = 0;
    end = nstate;
  } else {
    // given state or static singleton
    state = (state_ > 0 && nstate == 1
        && SettingGet_b(G, set, NULL, cSetting_static_singletons)) ? 0 : state_;
    end = state + 1;
  }

  if (state < 0)
    state = 0;

  if (end > nstate)
    end = nstate;

  state--;
}

/*
 * Helper function to init CPyMOL.Setting, a (name: index) dictionary.
 * Called in PyMOL_InitAPI
 */
bool CPyMOLInitSetting(OVLexicon * Lex, OVOneToOne * Setting) {
  for(int index = 0; index < cSetting_INIT; ++index) {
    auto &rec = SettingInfo[index];

    if (rec.level == cSettingLevel_unused)
      continue;

    OVreturn_word result = OVLexicon_GetFromCString(Lex, rec.name);

    if( !OVreturn_IS_OK(result) ||
        !OVreturn_IS_OK(OVOneToOne_Set(Setting, result.word, index)))
      return false;
  }

  return true;
}

#ifndef _PYMOL_NOPY
/*
 * Export the settings names to Python a as (name: index) dictionary.
 * Replacement for pymol.settings.SettingIndex
 */
PyObject * SettingGetSettingIndices() {
  PyObject * val;
  PyObject * dict = PyDict_New();

  for(int index = 0; index < cSetting_INIT; ++index) {
    auto &rec = SettingInfo[index];

    if (rec.level == cSettingLevel_unused)
      continue;

    if ((val = PyInt_FromLong(index))) {
      PyDict_SetItemString(dict, rec.name, val);
      Py_DECREF(val);
    }
  }

  return dict;
}

/*
 * Return a list of all setting indices for the given unique id
 */
PyObject * SettingUniqueGetIndicesAsPyList(PyMOLGlobals * G, int unique_id)
{
  CSettingUnique *I = G->SettingUnique;
  PyObject * list = PyList_New(0);
  OVreturn_word result;

  if(unique_id && OVreturn_IS_OK(result = OVOneToOne_GetForward(I->id2offset, unique_id))) {
    SettingUniqueEntry *entry;
    for (int offset = result.word; offset; offset = entry->next) {
      entry = I->entry + offset;
      PyObject *item = PyInt_FromLong(entry->setting_id);
      PyList_Append(list, item);
      Py_DECREF(item);
    }
  }

  return list;
}
#endif
