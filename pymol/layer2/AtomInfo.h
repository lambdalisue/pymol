
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
#ifndef _H_AtomInfo
#define _H_AtomInfo

#include"Rep.h"
#include"Setting.h"
#include"Version.h"

#if _PyMOL_VERSION_int < 1770
#define AtomInfoVERSION  176
#define BondInfoVERSION  176
#elif _PyMOL_VERSION_int < 1810
#define AtomInfoVERSION  177
#define BondInfoVERSION  177
#else
#define AtomInfoVERSION  181
#define BondInfoVERSION  177
#endif

/* FLAGS 0-3 have the following conventional usage for molecular modeling */


/* FLAG 0 - Atoms of interest - i.e. a ligand in an active site */
#define cAtomFlag_focus         0x00000001


/* FLAG 1 - Free atoms - those which can move subject to a force-field */
#define cAtomFlag_free          0x00000002


/* FLAG 2 - Restrained atoms - atoms subject to a harmonic restraint */
#define cAtomFlag_restrain      0x00000004


/* FLAG 3 - Fixed atoms - no movement allowed */
#define cAtomFlag_fix           0x00000008


/* FLAG 4 - Exclude these atoms when performing simulation, minimization */
#define cAtomFlag_exclude       0x00000010


/* FLAG 5 - Study atoms  */
#define cAtomFlag_study         0x00000020


/* FLAGS 4-7 are reserved for additional molecular modeling tasks */


/* FLAGS 8-15 are free for end users to manipulate */


/* FLAGS 16-21 are reserved for external GUIs and linked applications */


/* FLAGS 22-23 are for temporary use only (inside of self-contained loops) */


/* FLAGS 24-31 are reserved for PyMOL's internal use */


/* FLAG 24 - don't surface these atoms (waters, ligands, etc.) */
#define cAtomFlag_exfoliate     0x01000000


/* FLAG 25 - ignore atoms altogether when surfacing */
#define cAtomFlag_ignore        0x02000000


/* FLAG 26 - disable cartoon smoothing for these atoms */
#define cAtomFlag_no_smooth     0x04000000


/* FLAG 27 - polymer */
#define cAtomFlag_polymer       0x08000000

/* FLAG 28 - waters */
#define cAtomFlag_solvent       0x10000000

/* FLAG 29 - organics */
#define cAtomFlag_organic       0x20000000

/* FLAG 30 - inorganics */
#define cAtomFlag_inorganic     0x40000000


/* FLAG 31 - guide atom: e.g. CA in proteins */
#define cAtomFlag_guide         0x80000000

#define cAtomFlag_class         0xF8000000
#define cAtomFlag_class_mask    0x07FFFFFF

#define cResnLen 5
#define cResiLen 5
#define cAtomNameLen 4
#define cElemNameLen 4
#define cSegiLen 4
#define cTextTypeLen 20
#define cLabelTypeLen 20

#define cAtomInfoTetrahedral 4
#define cAtomInfoPlanar 3
#define cAtomInfoLinear 2
#define cAtomInfoSingle 1
#define cAtomInfoNone 5

#define cAN_LP  0
#define cAN_H   1
#define cAN_He  2
#define cAN_Li  3
#define cAN_Be  4
#define cAN_B   5
#define cAN_C   6
#define cAN_N   7
#define cAN_O   8
#define cAN_F   9
#define cAN_Ne 10
#define cAN_Na 11
#define cAN_Mg 12
#define cAN_Al 13
#define cAN_Si 14
#define cAN_P  15
#define cAN_S  16
#define cAN_Cl 17
#define cAN_Ar 18
#define cAN_K  19
#define cAN_Ca 20

#define cAN_Ti 22
#define cAN_V  23

#define cAN_Cr 24
#define cAN_Mn 25
#define cAN_Fe 26
#define cAN_Co 27
#define cAN_Ni 28
#define cAN_Cu 29
#define cAN_Zn 30
#define cAN_Ga 31
#define cAN_Ge 32
#define cAN_As 33
#define cAN_Se 34
#define cAN_Br 35
#define cAN_Kr 36

#define cAN_Rb 37
#define cAN_Sr 38
#define cAN_Y  39

#define cAN_Pd 46
#define cAN_Ag 47
#define cAN_Cd 48
#define cAN_In 49
#define cAN_Sn 50
#define cAN_Sb 51
#define cAN_Te 52
#define cAN_I  53
#define cAN_Xe 54
#define cAN_Cs 55
#define cAN_Ba 56

#define cAN_Ce 58

#define cAN_Pt 78
#define cAN_Au 79
#define cAN_Hg 80
#define cAN_Tl 81
#define cAN_Pb 82

#define cAN_U  92

typedef char Chain[2];
typedef char SSType[2];
typedef char SegIdent[cSegiLen + 1];
typedef char ResIdent[cResiLen + 1];
typedef char ResName[cResnLen + 1];
typedef char AtomName[cAtomNameLen + 1];

typedef char ElemName[cElemNameLen + 1];

// for customType (not geom)
#define cAtomInfoNoType -9999

inline char makeInscode(char c) {
  return (c <= ' ') ? '\0' : c;
}

typedef struct BondType {
  int index[2];
  int id;
  int unique_id;
#ifdef _PYMOL_IP_EXTRAS
  int oldid;
#endif
  signed char order;    // 0-4
  signed char temp1;    // bool? where used?
  signed char stereo;   // 0-6 Only for SDF (MOL) format in/out
  bool has_setting;     /* setting based on unique_id */
} BondType;

typedef struct AtomInfoType {
  union {
    float * anisou;               // only allocate with get_anisou
    int64_t dummyanisou;
  };

  lexidx_t segi;
  lexidx_t chain;
  lexidx_t resn;
  lexidx_t name;
  lexidx_t textType;
  lexidx_t custom;
  lexidx_t label;

  int resv;
  int customType;
  int priority;
  float b, q, vdw, partialCharge;
  int selEntry;
  int color;
  int id;                       // PDB ID
  unsigned int flags;
  int temp1;                    /* kludge fields - to remove */
  int unique_id;                /* introduced in version 0.77 */
  int discrete_state;           /* state+1 for atoms in discrete objects */
  float elec_radius;            /* radius for PB calculations */
  int rank;
  int visRep;                   /* bitmask for all reps */
#ifdef _PYMOL_IP_EXTRAS
  int oldid;                    // for undo
  int prop_id;
#endif

  // boolean flags
  bool hetatm : 1;
  bool bonded : 1;
  bool deleteFlag : 1;
  bool masked : 1;
  bool hb_donor : 1;
  bool hb_acceptor : 1;
  bool has_setting : 1;      /* setting based on unique_id */

  /* be careful not to write at these as (int*) */

  signed char formalCharge;     // values typically in range -2..+2
  signed char mmstereo;           /* from MMStereo */
  signed char cartoon;          /* 0 = default which is auto (use ssType) */
  signed char geom;             // cAtomInfo*
  signed char valence;          // 0-4
  signed char protons;          /* atomic number */

  char inscode;

  ElemName elem;               // redundant with "protons" ?
  SSType ssType;               /* blank or 'L' = turn/loop, 'H' = helix, 'S' = beta-strand/sheet */
  Chain alt;

  // small value optimized bitfields
  unsigned char stereo : 2;     // 0-3 Only for SDF (MOL) format in/out
  unsigned char chemFlag : 2;   // 0,1,2
  unsigned char protekted : 2;  // 0,1,2

  // methods
  bool isHydrogen() {
    return protons == cAN_H;
  }

  char getInscode(bool space=false) const {
    if (space && !inscode)
      return ' ';
    return inscode;
  }

  void setInscode(char c) {
    inscode = makeInscode(c);
  }

  void setResi(const char * resi) {
    if (sscanf(resi, "%d%c", &resv, &inscode) == 1 || inscode <= ' ')
      inscode = '\0';
  }

  // for AtomInfoHistory
  void setResi(int resv_, char inscode_) {
    resv = resv_;
    setInscode(inscode_);
  }

  /*
   * Return true if any representation, which is displayable by this
   * atom, is shown
   */
  bool isVisible() {
    if(visRep & (
          // point reps
          cRepSphereBit | cRepEllipsoidBit | cRepLabelBit |
          // surface reps
          cRepSurfaceBit | cRepDotBit | cRepMeshBit |
          // polymer reps (actually only shown for guide atoms or if
          // *_trace_atoms=1 or cartoon_ring_finder=4)
          cRepCartoonBit | cRepRibbonBit)) {
      return true;
    } else if(bonded) {
      // bond reps
      if (visRep & (cRepCylBit | cRepLineBit))
        return true;
    } else {
      // nonbonded reps
      if (visRep & (cRepNonbondedSphereBit | cRepNonbondedBit))
        return true;
    }
    return false;
  }

  // get the anisou array, allocate if null
  float * get_anisou() { return (anisou ? anisou : (anisou = new float[6])); }

  // read-only anisou access, no allocation
  const float * get_anisou() const { return anisou; }
  bool has_anisou() const { return anisou; }
} AtomInfoType;

void AtomInfoFree(PyMOLGlobals * G);
int AtomInfoInit(PyMOLGlobals * G);
void BondTypeInit(BondType *bt);
void BondTypeInit2(BondType *bt, int i1, int i2, int order);
void AtomInfoPurge(PyMOLGlobals * G, AtomInfoType * ai);
void AtomInfoCopy(PyMOLGlobals * G, const AtomInfoType * src, AtomInfoType * dst, int copy_properties=true);
int AtomInfoReserveUniqueID(PyMOLGlobals * G, int unique_id);
int AtomInfoIsUniqueIDActive(PyMOLGlobals * G, int unique_id);
int AtomInfoGetNewUniqueID(PyMOLGlobals * G);
void AtomInfoCleanAtomName(char *name);

#ifndef _PYMOL_NOPY
int AtomInfoSetSettingFromPyObject(PyMOLGlobals * G, AtomInfoType *ai, int setting_id, PyObject *val);
#endif
int AtomInfoCheckSetting(PyMOLGlobals * G, AtomInfoType * ai, int setting_id);
PyObject *SettingGetIfDefinedPyObject(PyMOLGlobals * G, AtomInfoType * ai, int setting_id);
int AtomInfoGetSetting_b(PyMOLGlobals * G, AtomInfoType * ai, int setting_id, int current,
                         int *effective);
int AtomInfoGetSetting_i(PyMOLGlobals * G, AtomInfoType * ai, int setting_id, int current,
                         int *effective);
int AtomInfoGetSetting_f(PyMOLGlobals * G, AtomInfoType * ai, int setting_id,
                         float current, float *effective);
int AtomInfoGetSetting_color(PyMOLGlobals * G, AtomInfoType * ai, int setting_id,
                             int current, int *effective);

void AtomInfoBondCopy(PyMOLGlobals * G, const BondType * src, BondType * dst);

int AtomInfoCheckBondSetting(PyMOLGlobals * G, BondType * bi, int setting_id);
int AtomInfoGetBondSetting_b(PyMOLGlobals * G, BondType * ai, int setting_id, int current,
                             int *effective);
int AtomInfoGetBondSetting_i(PyMOLGlobals * G, BondType * ai, int setting_id, int current,
                             int *effective);
int AtomInfoGetBondSetting_f(PyMOLGlobals * G, BondType * ai, int setting_id,
                             float current, float *effective);
int AtomInfoGetBondSetting_color(PyMOLGlobals * G, BondType * ai, int setting_id,
                                 int current, int *effective);

int AtomInfoCheckUniqueID(PyMOLGlobals * G, AtomInfoType * ai);
void AtomInfoAssignParameters(PyMOLGlobals * G, AtomInfoType * I);
void AtomInfoFreeSortedIndexes(PyMOLGlobals * G, int **index, int **outdex);
void AtomInfoPrimeColors(PyMOLGlobals * G);
void AtomInfoAssignColors(PyMOLGlobals * G, AtomInfoType * at1);
int AtomInfoGetColor(PyMOLGlobals * G, AtomInfoType * at1);
int AtomInfoGetExpectedValence(PyMOLGlobals * G, AtomInfoType * I);
int AtomInfoIsFreeCation(PyMOLGlobals * G, AtomInfoType * I);
PyObject *AtomInfoAsPyList(PyMOLGlobals * G, AtomInfoType * at);
int AtomInfoFromPyList(PyMOLGlobals * G, AtomInfoType * at, PyObject * list);
int AtomInfoMatch(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoCompareAll(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoCompare(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoCompareIgnoreRank(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoCompareIgnoreHet(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoCompareIgnoreRankHet(PyMOLGlobals * G, AtomInfoType * at1,
                                 AtomInfoType * at2);
float AtomInfoGetBondLength(PyMOLGlobals * G, AtomInfoType * ai1, AtomInfoType * ai2);
int AtomInfoSameResidue(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoSameResidueP(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoSameChainP(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoSameSegmentP(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoSequential(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2,
                       int mode);

int AtomInfoCheckUniqueBondID(PyMOLGlobals * G, BondType * bi);
void AtomInfoPurgeBond(PyMOLGlobals * G, BondType * bi);

void AtomInfoBracketResidue(PyMOLGlobals * G, AtomInfoType * ai0, int n0,
                            AtomInfoType * ai, int *st, int *nd);
void AtomInfoBracketResidueFast(PyMOLGlobals * G, AtomInfoType * ai0, int n0, int cur,
                                int *st, int *nd);

int AtomInfoUniquefyNames(PyMOLGlobals * G, AtomInfoType * atInfo0, int n0,
                          AtomInfoType * atInfo1, int *flag1, int n1);
int AtomInfoGetCarbColor(PyMOLGlobals * G);
bool AtomResiFromResv(char *resi, size_t size, int resv, char inscode);
inline bool AtomResiFromResv(char *resi, size_t size, AtomInfoType * ai) {
  return AtomResiFromResv(resi, size, ai->resv, ai->inscode);
}

int AtomInfoKnownWaterResName(PyMOLGlobals * G, const char *resn);
int AtomInfoKnownPolymerResName(const char *resn);
void AtomInfoGetPDB3LetHydroName(PyMOLGlobals * G, const char *resn, const char *iname, char *oname);

#define cAIC_ct        0x0001
#define cAIC_fc        0x0002
#define cAIC_pc        0x0004
#define cAIC_b         0x0008
#define cAIC_q         0x0010
#define cAIC_id        0x0020
#define cAIC_flags     0x0080
#define cAIC_tt        0x0100
#define cAIC_state     0x0200
#define cAIC_rank      0x0400
#define cAIC_custom    0x0800

#define cAIC_IDMask (cAIC_id|cAIC_rank)
#define cAIC_PDBMask (cAIC_b|cAIC_q|cAIC_id|cAIC_rank)
#define cAIC_MMDMask (cAIC_pc|cAIC_ct|cAIC_id|cAIC_rank)
#define cAIC_MOLMask (cAIC_fc|cAIC_id|cAIC_rank)
#define cAIC_AllMask 0xFFFF

void AtomInfoCombine(PyMOLGlobals * G, AtomInfoType * dst, AtomInfoType * src, int mask);
int AtomInfoNameOrder(PyMOLGlobals * G, AtomInfoType * at1, AtomInfoType * at2);
int AtomInfoUpdateAutoColor(PyMOLGlobals * G);

typedef struct {
  int resv1, resv2;
  char inscode1, inscode2;
  unsigned char chain1, chain2;
  unsigned char type;
  int next;
} SSEntry;

int BondTypeCompare(PyMOLGlobals * G, BondType * bt1, BondType * bt2);

void atomicnumber2elem(char * dst, int protons);

// atom-level setting
template <typename V> void SettingSet(PyMOLGlobals * G, int index, V value, AtomInfoType * ai) {
  AtomInfoCheckUniqueID(G, ai);
  ai->has_setting = true;
  SettingUniqueSet(G, ai->unique_id, index, value);
}

// bond-level setting
template <typename V> void SettingSet(PyMOLGlobals * G, int index, V value, BondType * b) {
  AtomInfoCheckUniqueBondID(G, b);
  b->has_setting = true;
  SettingUniqueSet(G, b->unique_id, index, value);
}

#endif
