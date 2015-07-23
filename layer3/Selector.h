
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
#ifndef _H_Selector
#define _H_Selector

#include"os_python.h"

#include"ObjectMolecule.h"
#include"CoordSet.h"
#include"DistSet.h"
#include"ObjectMap.h"
#include"OVOneToAny.h"
#include"Match.h"

#include "AtomIterators.h"

#define cSelectionAll 0
#define cSelectionNone 1

int SelectorInit(PyMOLGlobals * G);
int SelectorInitImpl(PyMOLGlobals * G, CSelector **I, short init2);
int SelectorCreate(PyMOLGlobals * G, const char *name, const char *sele, ObjectMolecule * obj,
                   int quiet, Multipick * mp);
int SelectorCreateWithStateDomain(PyMOLGlobals * G, const char *name, const char *sele,
                                  ObjectMolecule * obj, int quiet, Multipick * mp,
                                  int state, const char *domain);
int SelectorCreateSimple(PyMOLGlobals * G, const char *name, const char *sele);
int SelectorCreateFromObjectIndices(PyMOLGlobals * G, const char *sname, ObjectMolecule * obj,
                                    int *idx, int n_idx);
int SelectorCreateOrderedFromObjectIndices(PyMOLGlobals * G, const char *sname,
                                           ObjectMolecule * obj, int *idx, int n_idx);
int SelectorCreateOrderedFromMultiObjectIdxTag(PyMOLGlobals * G, const char *sname,
                                               ObjectMolecule ** obj, int **pri_idx,
                                               int *n_idx, int n_obj);

int SelectorCreateFromTagDict(PyMOLGlobals * G, const char *sname, OVOneToAny * id2tag,
                              int exec_managed);


/* if n_idx is negative, then looks for negative *idx as the sentinel */
int SelectorMoveMember(PyMOLGlobals * G, int s, int sele_old, int sele_new);
int SelectorCreateEmpty(PyMOLGlobals * G, const char *name, int exec_managed);
void SelectorToggle(PyMOLGlobals * G, int rep, const char *name);
void SelectorCylinder(PyMOLGlobals * G, const char *sele, const char *onoff);

int SelectorUpdateTable(PyMOLGlobals * G, int req_state, int domain);
int SelectorUpdateTableImpl(PyMOLGlobals * G, CSelector *I, int req_state, int domain);

#define cSelectorUpdateTableAllStates -1
#define cSelectorUpdateTableCurrentState -2
#define cSelectorUpdateTableEffectiveStates -3

int SelectorIndexByName(PyMOLGlobals * G, const char *sele);
char *SelectorGetNameFromIndex(PyMOLGlobals * G, int index);
void SelectorFree(PyMOLGlobals * G);
void SelectorFreeImpl(PyMOLGlobals * G, CSelector *I, short init2);
void SelectorDelete(PyMOLGlobals * G, const char *sele);
void SelectorFreeTmp(PyMOLGlobals * G, const char *name);
int SelectorGetTmp2(PyMOLGlobals * G, const char *input, char *store, bool quiet=false);
int SelectorGetTmp(PyMOLGlobals * G, const char *input, char *store, bool quiet=false);
int SelectorCheckTmp(PyMOLGlobals * G, const char *name);
int SelectorGetPDB(PyMOLGlobals * G, char **charVLA, int cLen, int sele, int state,
                   int conectFlag, PDBInfoRec * pdb_info, int *counter, double *ref,
                   ObjectMolecule * single_object);
int SelectorLoadCoords(PyMOLGlobals * G, PyObject * coords, int sele, int state);
PyObject *SelectorGetCoordsAsNumPy(PyMOLGlobals * G, int sele, int state);
PyObject *SelectorGetChemPyModel(PyMOLGlobals * G, int sele, int state, double *ref);
float SelectorSumVDWOverlap(PyMOLGlobals * G, int sele1, int state1,
                            int sele2, int state2, float adjust);
int SelectorVdwFit(PyMOLGlobals * G, int sele1, int state1, int sele2, int state2,
                   float buffer, int quiet);

DistSet *SelectorGetDistSet(PyMOLGlobals * G, DistSet * ds,
                            int sele1, int state1, int sele2,
                            int state2, int mode, float cutoff, float *result);
DistSet *SelectorGetAngleSet(PyMOLGlobals * G, DistSet * ds,
                             int sele1, int state1,
                             int sele2, int state2,
                             int sele3, int state3,
                             int mode, float *angle_sum, int *angle_cnt);
DistSet *SelectorGetDihedralSet(PyMOLGlobals * G, DistSet * ds,
                                int sele1, int state1,
                                int sele2, int state2,
                                int sele3, int state3,
                                int sele4, int state4,
                                int mode, float *angle_sum, int *angle_cnt);
int SelectorGetSeleNCSet(PyMOLGlobals * G, int sele);
int SelectorCreateObjectMolecule(PyMOLGlobals * G, int sele, const char *name,
                                 int target_state, int state, int discrete,
                                 int zoom, int quiet, int singletons);
int SelectorSubdivide(PyMOLGlobals * G, const char *pref, int sele1, int sele2,
                      int sele3, int sele4,
                      const char *fragPref, const char *compName, int *bondMode);
ObjectMolecule *SelectorGetSingleObjectMolecule(PyMOLGlobals * G, int sele);
ObjectMolecule *SelectorGetFirstObjectMolecule(PyMOLGlobals * G, int sele);
int SelectorRenameObjectAtoms(PyMOLGlobals * G, ObjectMolecule * obj, int sele, int force,
                              int update_table);
void SelectorUpdateObjectSele(PyMOLGlobals * G, ObjectMolecule * obj);
void SelectorDeletePrefixSet(PyMOLGlobals * G, const char *pref);
void SelectorUpdateCmd(PyMOLGlobals * G, int sele0, int sele1, int sta0, int sta1,
                       int method, int quiet);
int SelectorGetSingleAtomVertex(PyMOLGlobals * G, int sele, int state, float *v);
int SelectorGetSingleAtomObjectIndex(PyMOLGlobals * G, int sele, ObjectMolecule ** in_obj,
                                     int *index);
int *SelectorGetResidueVLA(PyMOLGlobals * G, int sele0, int ca_only,
                           ObjectMolecule * exclude);
int SelectorCreateAlignments(PyMOLGlobals * G, int *pair, int sele1, int *vla1, int sele2,
                             int *vla2, const char *name1, const char *name2, int identical,
                             int atomic_input);
int SelectorGetPairIndices(PyMOLGlobals * G, int sele1, int state1, int sele2, int state2,
                           int mode, float cutoff, float h_angle, int **indexVLA,
                           ObjectMolecule *** objVLA);

int SelectorCountAtoms(PyMOLGlobals * G, int sele, int state);
void SelectorSetDeleteFlagOnSelectionInObject(PyMOLGlobals * G, int sele, ObjectMolecule *obj, signed char val);
int SelectorCheckIntersection(PyMOLGlobals * G, int sele1, int sele2);
int SelectorCountStates(PyMOLGlobals * G, int sele);
int SelectorClassifyAtoms(PyMOLGlobals * G, int sele, int preserve,
                          ObjectMolecule * only_object);

void SelectorLogSele(PyMOLGlobals * G, const char *name);
int SelectorMapMaskVDW(PyMOLGlobals * G, int sele1, ObjectMapState * oMap, float buffer,
                       int state);

int SelectorMapCoulomb(PyMOLGlobals * G, int sele1, ObjectMapState * oMap, float cutoff,
                       int state, int neutral, int shift, float shift_power);

int SelectorMapGaussian(PyMOLGlobals * G, int sele1, ObjectMapState * oMap,
                        float buffer, int state, int normalize, int use_max, int quiet,
                        float resolution);

PyObject *SelectorAsPyList(PyMOLGlobals * G, int sele1);
int SelectorFromPyList(PyMOLGlobals * G, const char *name, PyObject * list);
ObjectMolecule **SelectorGetObjectMoleculeVLA(PyMOLGlobals * G, int sele);

PyObject *SelectorColorectionGet(PyMOLGlobals * G, const char *prefix);
int SelectorColorectionApply(PyMOLGlobals * G, PyObject * list, const char *prefix);
int SelectorColorectionSetName(PyMOLGlobals * G, PyObject * list, const char *prefix,
                               char *new_prefix);
int SelectorColorectionFree(PyMOLGlobals * G, PyObject * list, const char *prefix);
void SelectorReinit(PyMOLGlobals * G);
PyObject *SelectorSecretsAsPyList(PyMOLGlobals * G);
int SelectorSecretsFromPyList(PyMOLGlobals * G, PyObject * list);
void SelectorMemoryDump(PyMOLGlobals * G);
int SelectorAssignSS(PyMOLGlobals * G, int target, int present, int state_value,
                     int preserve, ObjectMolecule * single_object, int quiet);

int SelectorPurgeObjectMembers(PyMOLGlobals * G, ObjectMolecule * obj);
void SelectorDefragment(PyMOLGlobals * G);
void SelectorSelectByID(PyMOLGlobals * G, const char *name, ObjectMolecule * obj, int *id,
                        int n_id);
void SelectorGetUniqueTmpName(PyMOLGlobals * G, char *name_buffer);
int SelectorIsAtomBondedToSele(PyMOLGlobals * G, ObjectMolecule * obj, int sele1atom,
                               int sele2);
void SelectorComputeFragPos(PyMOLGlobals * G, ObjectMolecule * obj, int state, int n_frag,
                            char *prefix, float **vla);

int SelectorSetName(PyMOLGlobals * G, const char *new_name, const char *old_name);

ObjectMolecule *SelectorGetCachedSingleAtom(PyMOLGlobals * G, int sele, int *theAtom);

ObjectMolecule *SelectorGetFastSingleAtomObjectIndex(PyMOLGlobals * G, int sele,
                                                     int *index);
ObjectMolecule *SelectorGetFastSingleObjectMolecule(PyMOLGlobals * G, int sele);
MapType *SelectorGetSpacialMapFromSeleCoord(PyMOLGlobals * G, int sele, int state,
                                            float cutoff, float **coord_vla);
int SelectorNameIsKeyword(PyMOLGlobals * G, const char *name);

int SelectorResidueVLAsTo3DMatchScores(PyMOLGlobals * G, CMatch * match,
                                       int *vla1, int n1, int state1,
                                       int *vla2, int n2, int state2,
                                       float seq_wt,
                                       float radius, float scale,
                                       float base, float coord_wt, float rms_exp);

int SelectorAssignAtomTypes(PyMOLGlobals * G, int sele, int state, int quiet, int format);


/* reserve special meaning for tags 1-15 and note that 0 is disallowed */

#define SELECTOR_BASE_TAG 0x10

typedef struct {
  int selection;
  int tag;                      /* must not be zero since it is also used as a boolean test for membership */
  int next;
} MemberType;

int SelectorIsMember(PyMOLGlobals * G, int start, int sele);

#endif
