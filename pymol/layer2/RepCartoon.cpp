

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
-* Cameron Mura
-* 
-*
Z* -------------------------------------------------------------------
*/

#include"os_predef.h"
#include"os_std.h"
#include"os_gl.h"

#include"Base.h"
#include"OOMac.h"
#include"RepCartoon.h"
#include"Color.h"
#include"Setting.h"
#include"Word.h"
#include"Feedback.h"
#include"CGO.h"
#include"Extrude.h"
#include"ShaderMgr.h"
#include "Lex.h"

#include "AtomIterators.h"

class CCInOut {
  // skip c++11 initialization, this is Calloc'd
  signed char cc_in; // { 0 };
  signed char cc_out; // { 0 };

public:
  signed char getCCIn() const { return cc_in; }
  signed char getCCOut() const { return cc_out ? cc_out : getCCIn(); }

  void setCCOut(int c) { cc_out = c; }

  // legacy
  void operator= (int c) { cc_in = c; }
  operator int() const { return cc_in; }
  bool operator== (const CCInOut &other) const { return cc_in == other.cc_in; }
};

typedef struct RepCartoon {
  Rep R;                        /* must be first! */
  CGO *ray, *std, *preshader, *pickingCGO;
  char *LastVisib;
  unsigned char renderWithShaders, hasTransparency;
} RepCartoon;

#include"ObjectMolecule.h"

#define ESCAPE_MAX 500

void RepCartoonFree(RepCartoon * I);

void RepCartoonFree(RepCartoon * I)
{
  if (I->ray != I->preshader)
    CGOFree(I->preshader);
  CGOFree(I->ray);
  if(I->pickingCGO && (I->pickingCGO != I->std))
    CGOFree(I->pickingCGO);
  CGOFree(I->std);
  FreeP(I->LastVisib);
  RepPurge(&I->R);
  OOFreeP(I);
}

static void RepCartoonRender(RepCartoon * I, RenderInfo * info)
{
  CRay *ray = info->ray;
  Picking **pick = info->pick;
  PyMOLGlobals *G = I->R.G;
  int ok = true;

  if (!ray && I->preshader){
    int use_shaders, cartoon_use_shader, has_cylinders_to_optimize;
    use_shaders = SettingGetGlobal_b(G, cSetting_use_shaders);
    cartoon_use_shader = SettingGetGlobal_b(G, cSetting_cartoon_use_shader);
    has_cylinders_to_optimize = CShaderPrg_Get_CylinderShader_NoSet(G) && SettingGetGlobal_i(G, cSetting_cartoon_nucleic_acid_as_cylinders) && SettingGetGlobal_b(G, cSetting_render_as_cylinders) ;
    if (use_shaders && cartoon_use_shader){
      CGO *convertcgo = NULL, *tmpCGO = NULL;
      if (has_cylinders_to_optimize){
	CGO *leftOverCGO = CGONew(G), *leftOverCGOSimplified = NULL, *sphereVBOs = NULL, *leftOverAfterSpheresCGO = NULL;
	CHECKOK(ok, leftOverCGO);
	/* Optimize Cylinders into Shader operation */
	if (CShaderPrg_Get_CylinderShader_NoSet(G)){
	  convertcgo = CGOOptimizeGLSLCylindersToVBOIndexedWithLeftOver(I->preshader, 0, leftOverCGO);
	}
	if (!convertcgo){
	  convertcgo = CGONew(G);
	  CHECKOK(ok, convertcgo);
	  leftOverCGO = I->preshader;
	  I->preshader = NULL;
	} else if (ok) {
	  ok &= CGOStop(leftOverCGO); 
	}
	/* Optimize spheres and putting them into convertcgo as a shader CGO operation */
	if (ok)
	  leftOverAfterSpheresCGO = CGONew(G);
	CHECKOK(ok, leftOverAfterSpheresCGO);
	if (ok)
	  sphereVBOs = CGOOptimizeSpheresToVBONonIndexedImpl(leftOverCGO, 0, leftOverAfterSpheresCGO);
	if (ok && sphereVBOs){
	  ok &= CGOStop(leftOverAfterSpheresCGO);
	  if (leftOverCGO!=I->ray){
	    CGOFree(leftOverCGO);
	    leftOverCGO = NULL;
	  }
	  if (ok && sphereVBOs)
	    ok &= CGOAppend(convertcgo, sphereVBOs);
	  CGOFreeWithoutVBOs(sphereVBOs);
	  sphereVBOs = NULL;
	} else {
	  if (leftOverAfterSpheresCGO)
	    CGOFree(leftOverAfterSpheresCGO);
	  leftOverAfterSpheresCGO = leftOverCGO;
	}
	/* For the rest of the primitives that exist, simplify them into Geometry
	 * (should probably be no more, but do this anyway) */
	if (ok)
	  leftOverCGOSimplified = CGOSimplify(leftOverAfterSpheresCGO, 0);
	CHECKOK(ok, leftOverCGOSimplified);
	if (leftOverAfterSpheresCGO!=I->ray){
	  CGOFree(leftOverAfterSpheresCGO);
	  leftOverAfterSpheresCGO = NULL;
	}
	/* Convert all DrawArrays and Geometry to VBOs */
	if (ok)
	  tmpCGO = CGOOptimizeToVBONotIndexed(leftOverCGOSimplified, 0);
	CHECKOK(ok, tmpCGO);
	CGOFree(leftOverCGOSimplified);
	leftOverCGOSimplified = NULL;
	if (ok)
	  ok &= CGOAppend(convertcgo, tmpCGO);
	CGOFreeWithoutVBOs(tmpCGO);
	tmpCGO = NULL;
	I->std = convertcgo;
      } else if (ok){
	convertcgo = CGOSimplify(I->preshader, 0);
	CHECKOK(ok, convertcgo);
	if (ok)
	  tmpCGO = CGOOptimizeToVBONotIndexed(convertcgo, 0);
	CHECKOK(ok, tmpCGO);
	CGOFree(convertcgo);
	convertcgo = NULL;
	I->std = tmpCGO;
      }
    } else if (ok){
      I->std = CGOSimplify(I->preshader, 0);
      CHECKOK(ok, I->std);
    }
    if(I->preshader && (I->ray!=I->preshader)){
      CGOFree(I->preshader);
    }
    I->preshader = NULL;
  }

  if(ray) {
#ifndef _PYMOL_NO_RAY
    int try_std = false;
    PRINTFD(G, FB_RepCartoon)
      " RepCartoonRender: rendering raytracable...\n" ENDFD;

    if(I->ray){
      int rok = CGORenderRay(I->ray, ray, NULL, I->R.cs->Setting, I->R.obj->Setting);
      if (!rok){
        if (I->ray == I->preshader)
          I->preshader = NULL;
	CGOFree(I->ray);
	I->ray = NULL;
	try_std = true;
      }
    } else {
      try_std = true;
    }
    if(try_std && I->std){
      ok &= CGORenderRay(I->std, ray, NULL, I->R.cs->Setting, I->R.obj->Setting);
      if (!ok){
	CGOFree(I->std);
	I->std = NULL;
      }
    }
#endif
  } else if(G->HaveGUI && G->ValidContext) {
    int use_shader;
    use_shader = SettingGetGlobal_b(G, cSetting_cartoon_use_shader) &&
		  SettingGetGlobal_b(G, cSetting_use_shaders)
      && !pick;  /* should not use shaders for picking (for now) */
    
    if(pick) {
      if(I->pickingCGO) {
	I->pickingCGO->use_shader = use_shader;
        CGORenderGLPicking(I->pickingCGO, pick, &I->R.context,
                           I->R.cs->Setting, I->R.obj->Setting);
      }
    } else {
        PRINTFD(G, FB_RepCartoon)
          " RepCartoonRender: rendering GL...\n" ENDFD;

        if(ok && I->std){
	  I->std->use_shader = use_shader;

	  I->std->enable_shaders = true;
          CGORenderGL(I->std, NULL, I->R.cs->Setting, I->R.obj->Setting, info, &I->R);
	}
    }
  }
  if (!ok || !CGOHasOperationsOfType(I->ray, 0)){
    if (I->ray == I->preshader)
      I->preshader = NULL;
    CGOFree(I->ray);
    CGOFree(I->std);
    I->R.fInvalidate(&I->R, I->R.cs, cRepInvPurge);
    I->R.cs->Active[cRepCartoon] = false;
  }
}

#define NUCLEIC_NORMAL0 "C2"
#define NUCLEIC_NORMAL1 "C3*"
#define NUCLEIC_NORMAL2 "C3'"

#define MAX_RING_ATOM 10

typedef struct nuc_acid_data {
  int na_mode;
  int *nuc_flag;   // whether atom is part of nucleotide
  int a2;          // defaults to -1
  int nSeg;        // defaults to 0
  float *v_o_last; // defaults to NULL
  int *sptr;
  int *iptr;
  CCInOut * cc;
  int nAt;
  int *ss;
  int putty_flag;
  int *fp;
  float *vptr, *voptr;
  int *ring_anchor;
  int ring_mode, ring_finder, ring_finder_eff;
  int n_ring;
} nuc_acid_data;

/* atix must contain n_atom + 1 elements, with the first atom repeated at the end */

static void do_ring(PyMOLGlobals * G, nuc_acid_data *ndata, bool is_picking, int n_atom,
                    int *atix, ObjectMolecule * obj,
                    CoordSet * cs, float ring_width, CGO * cgo, int ring_color,
                    float ladder_radius, int ladder_color, int ladder_mode,
                    int sc_helper,
                    float ring_alpha, float alpha, int *marked, float *moved,
                    float ring_radius)
{
  float *v_i[MAX_RING_ATOM];
  float *col[MAX_RING_ATOM];
  float n_up[MAX_RING_ATOM][3];
  float n_dn[MAX_RING_ATOM][3];
  AtomInfoType *ai_i[MAX_RING_ATOM];
  int have_all = true;
  int all_marked = true;
  AtomInfoType *ai;
  int have_C4 = -1;
  int have_C4_prime = -1;
  int have_C_number = -1;
  int nf = false;
  int ring_mode = ndata->ring_mode;
  int finder = ndata->ring_finder;
  const auto& nuc_flag = ndata->nuc_flag;

  /* first, make sure all atoms have known coordinates */
  {
    int a, i;
    for(i = 0; i <= n_atom; i++) {
      int a1 = atix[i];
      int have_atom = false;
      if(nuc_flag[a1])
        nf = true;
      a = cs->atmToIdx(a1);
      if(a >= 0) {
        ai = obj->AtomInfo + a1;
        if(ai->visRep & cRepCartoonBit) {
          ai_i[i] = ai;

          {
            int atom_ring_mode = ring_mode;
            int atom_ring_color = ladder_color;
            float atom_ring_radius = ring_radius;
            float atom_ring_width = ring_width;
            int atom_ladder_mode = ladder_mode;
            int atom_ladder_color = ladder_color;
            float atom_ladder_radius = ladder_radius;

            if(AtomInfoGetSetting_i
               (G, ai, cSetting_cartoon_ring_mode, ndata->ring_mode, &atom_ring_mode)) {
              ring_mode = atom_ring_mode;
            }
            if(AtomInfoGetSetting_color
               (G, ai, cSetting_cartoon_ring_color, ring_color, &atom_ring_color)) {
              ring_color = atom_ring_color;
            }
            if(AtomInfoGetSetting_f
               (G, ai, cSetting_cartoon_ring_radius, ring_radius, &atom_ring_radius)) {
              ring_radius = atom_ring_radius;
            }
            if(AtomInfoGetSetting_f
               (G, ai, cSetting_cartoon_ring_width, ring_width, &atom_ring_width)) {
              ring_width = atom_ring_width;
            }
            if(AtomInfoGetSetting_i
               (G, ai, cSetting_cartoon_ladder_mode, ladder_mode, &atom_ladder_mode)) {
              ladder_mode = atom_ladder_mode;
            }
            if(AtomInfoGetSetting_color
               (G, ai, cSetting_cartoon_ladder_color, ladder_color, &atom_ladder_color)) {
              ladder_color = atom_ladder_color;
            }
            if(AtomInfoGetSetting_f
               (G, ai, cSetting_cartoon_ladder_radius, ladder_radius,
                &atom_ladder_radius)) {
              ladder_radius = atom_ladder_radius;
            }
          }

          col[i] = ColorGet(G, ai->color);
          v_i[i] = cs->Coord + 3 * a;
          have_atom = true;
          const char * ai_name = LexStr(G, ai->name);
          if(WordMatchExact(G, "C4", ai_name, 1))
            have_C4 = a1;
          else if(
              WordMatchExact(G, "C4'", ai_name, 1) ||
              WordMatchExact(G, "C4*", ai_name, 1))
            have_C4_prime = a1;
          if(((ai_name[0] == 'C') || (ai_name[0] == 'c')) &&
              isdigit(ai_name[1]))
            have_C_number = a1;
        }
        if(!marked[a1])
          all_marked = false;
      }
      if(!have_atom) {
        have_all = false;
        break;
      }
    }
  }

  if(!ring_mode)
    finder = 0;

  ring_width *= 0.5F;

  if(n_atom && have_all && (!all_marked)) {
    if(ladder_mode) {
      int i;
      int a1;
      AtomInfoType *ai2;
      AtomInfoType *atomInfo = obj->AtomInfo;
      int mem0, mem1, mem2, mem3, mem4, mem5, mem6, mem7;
      int *neighbor = obj->Neighbor;
      int nbr[7];
      int sugar_at = -1, base_at = -1;
      int phos3_at = -1, phos5_at = -1;
      int o3_at = -1, o5_at = -1, c1_at = -1;
      int purine_flag = false;
      int c1 = -1, c1_linked = -1;
      int c2 = -1, c2_linked = -1;
      int c3 = -1, c3_linked = -1;
      int c4 = -1, c4_linked = -1;
      int c5 = -1, c5_linked = -1;

      if(n_atom == 5) {         /* going from the sugar to the base */
        for(i = 0; i < n_atom; i++) {
          a1 = atix[i];
          if(!nf)
            nf = nuc_flag[a1];
          ai = atomInfo + a1;
          if((ai->protons == cAN_C) && (!marked[a1]) &&
             (WordMatchExact(G, "C3*", LexStr(G, ai->name), 1) ||
              WordMatchExact(G, "C3'", LexStr(G, ai->name), 1))) {
            sugar_at = a1;
            mem0 = a1;
            nbr[0] = neighbor[mem0] + 1;
            while((mem1 = neighbor[nbr[0]]) >= 0) {
              if((atomInfo[mem1].protons == cAN_O) && (!marked[mem1])) {
                ai = atomInfo + mem1;
                if(WordMatchExact(G, "O3*", LexStr(G, ai->name), 1) ||
                   WordMatchExact(G, "O3'", LexStr(G, ai->name), 1))
                  o3_at = mem1;
                nbr[1] = neighbor[mem1] + 1;
                while((mem2 = neighbor[nbr[1]]) >= 0) {
                  if((mem2 != mem0) && (!marked[mem2]) &&
                     (atomInfo[mem2].protons == cAN_P)) {
                    phos3_at = mem2;
                  }
                  nbr[1] += 2;
                }
              }

              if((atomInfo[mem1].protons == cAN_C) && (!marked[mem1])) {
                ai2 = atomInfo + mem1;
                if(WordMatchExact(G, NUCLEIC_NORMAL1, LexStr(G, ai2->name), 1) ||
                   WordMatchExact(G, NUCLEIC_NORMAL2, LexStr(G, ai2->name), 1))
                  sugar_at = mem1;

                nbr[1] = neighbor[mem1] + 1;
                while((mem2 = neighbor[nbr[1]]) >= 0) {
                  if((mem2 != mem0) && (!marked[mem2]) &&
                     (atomInfo[mem2].protons == cAN_C)) {
                    ai = atomInfo + mem2;
                    if(WordMatchExact(G, "C1*", LexStr(G, ai->name), 1) ||
                       WordMatchExact(G, "C1'", LexStr(G, ai->name), 1))
                      c1_at = mem2;
                    nbr[2] = neighbor[mem2] + 1;
                    while((mem3 = neighbor[nbr[2]]) >= 0) {
                      if((mem3 != mem1) && (mem3 != mem0)) {
                        if((atomInfo[mem3].protons == cAN_O) && (!marked[mem3])) {
                          ai = atomInfo + mem3;
                          if(WordMatchExact(G, "O5*", LexStr(G, ai->name), 1) ||
                             WordMatchExact(G, "O5'", LexStr(G, ai->name), 1))
                            o5_at = mem3;

                          nbr[3] = neighbor[mem3] + 1;
                          while((mem4 = neighbor[nbr[3]]) >= 0) {
                            if((mem4 != mem2) && (!marked[mem4]) &&
                               (atomInfo[mem4].protons == cAN_P)) {
                              phos5_at = mem4;
                            }
                            nbr[3] += 2;
                          }
                        }
                        if(atomInfo[mem3].protons == cAN_N) {

                          if(ring_mode) {
                            ai2 = atomInfo + mem3;
                            if((!marked[mem3])
                               && (WordMatchExact(G, "N1", LexStr(G, ai2->name), 1)
                                   || WordMatchExact(G, "N9", LexStr(G, ai2->name), 1))) {
                              base_at = mem3;
                              if(ring_mode != 3) {
                                ladder_radius = ring_width * 1.5;
                              } else {
                                ladder_radius = ring_width * 3;
                              }
                            }
                          } else {
                            nbr[3] = neighbor[mem3] + 1;
                            while((mem4 = neighbor[nbr[3]]) >= 0) {
                              if((mem4 != mem2) && (mem4 != mem1) && (mem4 != mem0) &&
                                 (atomInfo[mem4].protons == cAN_C)) {

                                nbr[4] = neighbor[mem4] + 1;
                                while((mem5 = neighbor[nbr[4]]) >= 0) {
                                  if((mem5 != mem3) && (mem5 != mem2) && (mem5 != mem1)
                                     && (mem5 != mem0)
                                     && (atomInfo[mem5].protons == cAN_N)
                                     && (marked[mem5])) {
                                    /* must be in a mapped ring */
                                    /* clear flag here */
                                    purine_flag = false;

                                    nbr[5] = neighbor[mem5] + 1;
                                    while((mem6 = neighbor[nbr[5]]) >= 0) {
                                      if((mem6 != mem4) && (mem6 != mem3)
                                         && (mem6 != mem2) && (mem6 != mem1)
                                         && (mem6 != mem0)
                                         && (atomInfo[mem6].protons == cAN_C)
                                         && (marked[mem6])) {
                                        nbr[6] = neighbor[mem6] + 1;
                                        while((mem7 = neighbor[nbr[6]]) >= 0) {
                                          ai2 = atomInfo + mem7;
                                          if((mem7 != mem5) && (mem7 != mem4)
                                             && (mem7 != mem3) && (mem7 != mem2)
                                             && (mem7 != mem2) && (mem7 != mem1)
                                             && (mem7 != mem0) && (ai2->protons == cAN_N)
                                             && (marked[mem7])) {
                                            if(WordMatchExact(G, "N1", LexStr(G, ai2->name), 1)) {
                                              /* and set flag */
                                              base_at = mem7;
                                              purine_flag = true;
                                            }
                                          }
                                          nbr[6] += 2;
                                        }
                                      }
                                      nbr[5] += 2;
                                    }

                                    if(!purine_flag) {
                                      ai2 = atomInfo + mem5;
                                      if(marked[mem5]
                                         && WordMatchExact(G, "N3", LexStr(G, ai2->name), 1)) {
                                        base_at = mem5;
                                      }
                                    }
                                  }
                                  nbr[4] += 2;
                                }
                              }
                              nbr[3] += 2;
                            }
                          }
                        }
                      }
                      nbr[2] += 2;
                    }
                  }
                  nbr[1] += 2;
                }
              }
              nbr[0] += 2;
            }
          }
        }
      } else if((!ring_mode) && (n_atom == 6)) {        /* going from the base to the sugar */

        for(i = 0; i < n_atom; i++) {
          a1 = atix[i];
          if(!nf)
            nf = nuc_flag[a1];
          ai = atomInfo + a1;

          /* base-hunting */

          if((ai->protons == cAN_N) && (!marked[a1]) &&
             (WordMatchExact(G, "N1", LexStr(G, ai->name), 1) ||
              WordMatchExact(G, "N3", LexStr(G, ai->name), 1))) {
            mem0 = a1;
            nbr[0] = neighbor[mem0] + 1;
            while((mem1 = neighbor[nbr[0]]) >= 0) {
              if((atomInfo[mem1].protons == cAN_C) && (!marked[mem1])) {
                nbr[1] = neighbor[mem1] + 1;
                while((mem2 = neighbor[nbr[1]]) >= 0) {
                  if((mem2 != mem0) && (!marked[mem2]) &&
                     (atomInfo[mem2].protons == cAN_N)) {
                    nbr[2] = neighbor[mem2] + 1;
                    while((mem3 = neighbor[nbr[2]]) >= 0) {
                      if((mem3 != mem1) && (mem3 != mem0) &&
                         (atomInfo[mem3].protons == cAN_C)) {
                        nbr[3] = neighbor[mem3] + 1;
                        while((mem4 = neighbor[nbr[3]]) >= 0) {
                          if((mem4 != mem2) && (mem4 != mem1) && (mem4 != mem0)) {
                            if((atomInfo[mem4].protons == cAN_N) ||
                               WordMatchExact(G, "C5", LexStr(G, atomInfo[mem4].name), 1)) {     /* purine case */
                              nbr[4] = neighbor[mem4] + 1;
                              while((mem5 = neighbor[nbr[4]]) >= 0) {
                                if((mem5 != mem3) && (mem5 != mem2) && (mem5 != mem1)
                                   && (mem5 != mem0) && (marked[mem5])
                                   && (atomInfo[mem5].protons == cAN_C)) {
                                  nbr[5] = neighbor[mem5] + 1;
                                  while((mem6 = neighbor[nbr[5]]) >= 0) {
                                    if((mem6 != mem4) && (mem6 != mem3) && (mem6 != mem2)
                                       && (mem6 != mem1) && (mem6 != mem0)
                                       && ((atomInfo[mem6].protons == cAN_C)
                                           || (atomInfo[mem6].protons == cAN_O))
                                       && (marked[mem6])) {
                                      nbr[6] = neighbor[mem6] + 1;
                                      while((mem7 = neighbor[nbr[6]]) >= 0) {
                                        ai2 = atomInfo + mem7;
                                        if((mem7 != mem5) && (mem7 != mem4)
                                           && (mem7 != mem3) && (mem7 != mem2)
                                           && (mem7 != mem2) && (mem7 != mem1)
                                           && (mem7 != mem0) && (ai2->protons == cAN_C)
                                           && (marked[mem7])) {
                                          if(WordMatchExact
                                             (G, NUCLEIC_NORMAL1, LexStr(G, ai2->name), 1)
                                             || WordMatchExact(G, NUCLEIC_NORMAL2,
                                                               LexStr(G, ai2->name), 1)) {
                                            base_at = a1;
                                            sugar_at = mem7;
                                          }
                                        }
                                        nbr[6] += 2;
                                      }
                                    }
                                    nbr[5] += 2;
                                  }
                                }
                                nbr[4] += 2;
                              }
                            } else if(((atomInfo[mem4].protons == cAN_C)
                                       || (atomInfo[mem4].protons == cAN_O))
                                      && (marked[mem4])) {
                              /* pyrimidine case */

                              nbr[4] = neighbor[mem4] + 1;
                              while((mem5 = neighbor[nbr[4]]) >= 0) {
                                ai2 = atomInfo + mem5;
                                if((mem5 != mem3) && (mem5 != mem2) && (mem5 != mem1)
                                   && (mem5 != mem0) && (ai2->protons == cAN_C)
                                   && (marked[mem5])) {
                                  if(WordMatchExact(G, NUCLEIC_NORMAL1, LexStr(G, ai2->name), 1)
                                     || WordMatchExact(G, NUCLEIC_NORMAL2, LexStr(G, ai2->name), 1)) {
                                    base_at = a1;
                                    sugar_at = mem5;
                                  }
                                }
                                nbr[4] += 2;
                              }
                            }
                          }
                          nbr[3] += 2;
                        }
                      }
                      nbr[2] += 2;
                    }
                  }
                  nbr[1] += 2;
                }
              }
              nbr[0] += 2;
            }
          }
        }
      }
      if(n_atom == 6) {

        for(i = 0; i < n_atom; i++) {
          a1 = atix[i];
          ai = atomInfo + a1;

          /* glycosylation hunting */

          if((ai->protons == cAN_C) && (!marked[a1])) { /* unmarked C in ring */
            mem0 = a1;
            nbr[0] = neighbor[mem0] + 1;
            while((mem1 = neighbor[nbr[0]]) >= 0) {
              if((atomInfo[mem1].protons == cAN_C) &&   /* exocyclic C */
                 (!marked[mem1])) {
                nbr[1] = neighbor[mem1] + 1;
                while((mem2 = neighbor[nbr[1]]) >= 0) {
                  if((mem2 != mem0) && (!marked[mem2])
                     && (atomInfo[mem2].protons == cAN_O)) {
                    /* exocyclic O */
                    nbr[2] = neighbor[mem2] + 1;
                    while((mem3 = neighbor[nbr[2]]) >= 0) {
                      if((mem3 != mem1) && (mem3 != mem0) && (marked[mem3])
                         && (atomInfo[mem3].protons == cAN_C)) {
                        /* cyclic C */
                        if(WordMatchExact(G, "C5", LexStr(G, ai->name), 1) &&
                           WordMatchExact(G, "C6", LexStr(G, atomInfo[mem1].name), 1)) {
                          c5_linked = mem3;
                          c5 = mem0;
                        }
                      }
                      nbr[2] += 2;
                    }
                  }
                  nbr[1] += 2;
                }
              } else if((atomInfo[mem1].protons == cAN_O) &&    /* exocyclic O */
                        (!marked[mem1])) {
                nbr[1] = neighbor[mem1] + 1;
                while((mem2 = neighbor[nbr[1]]) >= 0) {
                  if((mem2 != mem0) && (marked[mem2])
                     && (atomInfo[mem2].protons == cAN_C)) {
                    /* cyclic C */
                    const char * ai_name = LexStr(G, ai->name);
                    if(WordMatchExact(G, "C1", ai_name, 1)) {
                      c1_linked = mem2;
                      c1 = mem0;
                    } else if(WordMatchExact(G, "C2", ai_name, 1)) {
                      c2_linked = mem2;
                      c2 = mem0;
                    } else if(WordMatchExact(G, "C3", ai_name, 1)) {
                      c3_linked = mem2;
                      c3 = mem0;
                    } else if(WordMatchExact(G, "C4", ai_name, 1)) {
                      c4_linked = mem2;
                      c4 = mem0;
                    }
                  } else if((mem2 != mem0) && (!marked[mem2])
                            && (atomInfo[mem2].protons == cAN_C)) {
                    /* exocyclic C */
                    nbr[2] = neighbor[mem2] + 1;
                    while((mem3 = neighbor[nbr[2]]) >= 0) {
                      if((mem3 != mem1) && (mem3 != mem0) && (marked[mem3])
                         && (atomInfo[mem3].protons == cAN_C)) {
                        /* cyclic C */
                        if(WordMatchExact(G, "C5", LexStr(G, atomInfo[mem3].name), 1) &&
                           WordMatchExact(G, "C6", LexStr(G, atomInfo[mem2].name), 1)) {
                          c5 = mem0;
                          c5_linked = mem3;
                        }
                      } else if((mem3 != mem1) && (mem3 != mem0) && (!marked[mem3])
                                && (atomInfo[mem3].protons == cAN_C)) {
                        /* exocyclic */
                        if(WordMatchExact(G, "C1", LexStr(G, ai->name), 1) &&
                           WordMatchExact(G, "CA", LexStr(G, atomInfo[mem3].name), 1)) {
                          c1 = mem0;
                          c1_linked = mem3;
                        }
                      }
                      nbr[2] += 2;
                    }
                  }
                  nbr[1] += 2;
                }
              } else if((atomInfo[mem1].protons == cAN_N) &&    /* exocyclic N */
                        (!marked[mem1])) {
                nbr[1] = neighbor[mem1] + 1;
                while((mem2 = neighbor[nbr[1]]) >= 0) {
                  if((mem2 != mem0) && (!marked[mem2])
                     && (atomInfo[mem2].protons == cAN_C)) {
                    /* exocyclic C */
                    nbr[2] = neighbor[mem2] + 1;
                    while((mem3 = neighbor[nbr[2]]) >= 0) {
                      if((mem3 != mem1) && (mem3 != mem0) && (!marked[mem3])
                         && (atomInfo[mem3].protons == cAN_C)) {
                        /* exocyclic */
                        nbr[3] = neighbor[mem3] + 1;
                        while((mem4 = neighbor[nbr[3]]) >= 0) {
                          if((mem4 != mem2) && (mem3 != mem1) && (!marked[mem4])
                             && (atomInfo[mem4].protons == cAN_C)) {
                            /* exocyclic */
                            if(WordMatchExact(G, "C1", LexStr(G, ai->name), 1) &&
                               WordMatchExact(G, "CA", LexStr(G, atomInfo[mem4].name), 1)) {
                              c1 = mem0;
                              c1_linked = mem4;
                            }
                          }
                          nbr[3] += 2;
                        }
                      }
                      nbr[2] += 2;
                    }
                  }
                  nbr[1] += 2;
                }
              }
              nbr[0] += 2;
            }
          }
        }
      }

      if(sugar_at >= 0) {       /* still need to find the phosphates... */
        int c3_index = -1;
        ai = atomInfo + sugar_at;
        if(WordMatchExact(G, "C3*", LexStr(G, ai->name), 1) || WordMatchExact(G, "C3'", LexStr(G, ai->name), 1)) {
          c3_index = sugar_at;
        } else {
          mem0 = sugar_at;
          nbr[0] = neighbor[mem0] + 1;
          while((mem1 = neighbor[nbr[0]]) >= 0) {
            if((atomInfo[mem1].protons == cAN_C) && (!marked[mem1])) {
              ai = atomInfo + mem1;
              if(!(WordMatchExact(G, "C3*", LexStr(G, ai->name), 1) ||
                   WordMatchExact(G, "C3'", LexStr(G, ai->name), 1))) {
                c3_index = mem1;
              }
            }
            nbr[0] += 2;
          }
        }

        if(c3_index >= 0) {     /* now we know where we are... */

          mem0 = c3_index;
          nbr[0] = neighbor[mem0] + 1;
          while((mem1 = neighbor[nbr[0]]) >= 0) {
            if((atomInfo[mem1].protons == cAN_O) && (!marked[mem1])) {
              ai = atomInfo + mem1;
              if(WordMatchExact(G, "O3*", LexStr(G, ai->name), 1) ||
                 WordMatchExact(G, "O3'", LexStr(G, ai->name), 1))
                o3_at = mem1;
              nbr[1] = neighbor[mem1] + 1;
              while((mem2 = neighbor[nbr[1]]) >= 0) {
                if((mem2 != mem0) && (!marked[mem2]) && (atomInfo[mem2].protons == cAN_P)) {
                  phos3_at = mem2;
                }
                nbr[1] += 2;
              }
            }
            if((atomInfo[mem1].protons == cAN_C) && (marked[mem1])) {

              nbr[1] = neighbor[mem1] + 1;
              while((mem2 = neighbor[nbr[1]]) >= 0) {

                if((mem2 != mem0) && (!marked[mem2]) && (atomInfo[mem2].protons == cAN_C)) {
                  ai = atomInfo + mem2;
                  if(WordMatchExact(G, "C1*", LexStr(G, ai->name), 1) ||
                     WordMatchExact(G, "C1'", LexStr(G, ai->name), 1))
                    c1_at = mem2;

                  nbr[2] = neighbor[mem2] + 1;
                  while((mem3 = neighbor[nbr[2]]) >= 0) {
                    if((mem3 != mem1) && (mem3 != mem0) &&
                       (atomInfo[mem3].protons == cAN_O) && (!marked[mem3])) {
                      ai = atomInfo + mem3;
                      if(WordMatchExact(G, "O5*", LexStr(G, ai->name), 1) ||
                         WordMatchExact(G, "O5'", LexStr(G, ai->name), 1))
                        o5_at = mem3;
                      nbr[3] = neighbor[mem3] + 1;
                      while((mem4 = neighbor[nbr[3]]) >= 0) {
                        if((mem4 != mem2) && (!marked[mem4]) &&
                           (atomInfo[mem4].protons == cAN_P)) {
                          phos5_at = mem4;
                        }
                        nbr[3] += 2;
                      }
                    }
                    nbr[2] += 2;
                  }
                }
                nbr[1] += 2;
              }
            }
            nbr[0] += 2;
          }
        }
      }
      /* glycosylation connectors */
      if(1) {
        if((ring_mode) && (finder >= 3)) {
          if(finder >= 3) {
            int b;
            float glyco_radius;
            switch (ring_mode) {
            case 3:
            case 4:
              glyco_radius = ring_width * 3;
              break;
            case 5:
              glyco_radius = ladder_radius;
              break;
            default:
              glyco_radius = ring_width * 1.5;
              break;
            }

            if((alpha != 1.0F) || (ring_alpha != alpha))
              CGOAlpha(cgo, alpha);

            for(b = 0; b < 5; b++) {
              int g1 = -1, g2 = -1;
              switch (b) {
              case 0:
                g1 = c1;
                g2 = c1_linked;
                break;
              case 1:
                g1 = c2;
                g2 = c2_linked;
                break;
              case 2:
                g1 = c3;
                g2 = c3_linked;
                break;
              case 3:
                g1 = c4;
                g2 = c4_linked;
                break;
              case 4:
                g1 = c5;
                g2 = c5_linked;
                break;
              }

              if((g1 >= 0) && (g2 >= 0)) {
                AtomInfoType *g1_ai = atomInfo + g1;
                AtomInfoType *g2_ai = atomInfo + g2;
                int sc_helper1, sc_helper2;
                AtomInfoGetSetting_b(G, g1_ai, cSetting_cartoon_side_chain_helper, sc_helper, &sc_helper1);
                AtomInfoGetSetting_b(G, g2_ai, cSetting_cartoon_side_chain_helper, sc_helper, &sc_helper2);
                if((g1_ai->visRep & g2_ai->visRep & cRepCartoonBit) &&
                    (!(sc_helper1 || sc_helper2) ||
                     !(g1_ai->visRep & (cRepLineBit | cRepCylBit | cRepSphereBit)))) {

                  float *g1p, *g2p;
                  float avg[3];

                  {
                    int g1_x, g2_x;

                    if(obj->DiscreteFlag) {
                      if(cs == obj->DiscreteCSet[g1] && cs == obj->DiscreteCSet[g2]) {
                        g1_x = obj->DiscreteAtmToIdx[g1];
                        g2_x = obj->DiscreteAtmToIdx[g2];
                      } else {
                        g1_x = -1;
                        g2_x = -1;
                      }
                    } else {
                      g1_x = cs->AtmToIdx[g1];
                      g2_x = cs->AtmToIdx[g2];
                    }
                    g1p = cs->Coord + 3 * g1_x;
                    g2p = cs->Coord + 3 * g2_x;
                  }

                  if(!((!((ring_mode == 0) || (ring_mode == 4) || (ring_mode == 5))) ||
                       (!marked[g2]))) {
                    g2p = moved + 3 * g2;
                  }
                  if((ring_mode == 0) || (ring_mode == 4) || (ring_mode == 5)) {
                    /* ring center */
                    int i;
                    /* compute average coordinate and mark atoms so that ring is only drawn once */
                    zero3f(avg);
                    for(i = 0; i < n_atom; i++) {
                      add3f(avg, v_i[i], avg);
                    }
                    scale3f(avg, 1.0F / n_atom, avg);
                    g1p = avg;
                  }

		  {
		    float *color1, *color2;
		    if (ladder_color >= 0) {
		      color1 = color2 = ColorGet(G, ladder_color);
		    } else {
		      color1 = ColorGet(G, g1_ai->color);
		      color2 = ColorGet(G, g2_ai->color);
		    }
		    if (is_picking){ /* If picking, need to split cylinder in two to pick atoms */
		      float midv[3], midc[3];
		      average3f(g1p, g2p, midv);
		      average3f(color1, color2, midc);
		      if(!g1_ai->masked)
			CGOPickColor(cgo, g1, cPickableAtom);
		      CGOCustomCylinderv(cgo, g1p, midv, glyco_radius, 
					 color1, midc, 2.0F, 0.0F);
		      if(!g2_ai->masked)
			CGOPickColor(cgo, g2, cPickableAtom);
		      CGOCustomCylinderv(cgo, midv, g2p, glyco_radius, 
					 midc, color2, 0.0F, 2.0F);
		    } else {
		      CGOCustomCylinderv(cgo, g1p, g2p, glyco_radius, 
					 color1, color2, 2.0F, 2.0F);
		    }
		  }
                }
              }
            }
          }
        }
      }

      /* see if any of the neighbors are confirmed nucleic acids... */
      if(sugar_at >= 0) {
        if(!nf) {
          mem0 = sugar_at;
          nbr[0] = neighbor[mem0] + 1;
          while((!nf) && (mem1 = neighbor[nbr[0]]) >= 0) {
            if(!nf)
              nf = nuc_flag[mem1];
            nbr[1] = neighbor[mem1] + 1;
            while((!nf) && (mem2 = neighbor[nbr[1]]) >= 0) {
              if(mem2 != mem0) {
                if(!nf)
                  nf = nuc_flag[mem2];
                nbr[2] = neighbor[mem2] + 1;
                while((!nf) && (mem3 = neighbor[nbr[2]]) >= 0) {
                  if((mem3 != mem1) && (mem3 != mem0)) {
                    if(!nf)
                      nf = nuc_flag[mem3];
                    nbr[3] = neighbor[mem3] + 1;
                    while((mem4 = neighbor[nbr[3]]) >= 0) {
                      if(mem4 != mem2) {
                        if(!nf)
                          nf = nuc_flag[mem4];
                        nbr[4] = neighbor[mem4] + 1;
                        while((mem5 = neighbor[nbr[4]]) >= 0) {
                          if(mem5 != mem3) {
                            if(!nf)
                              nf = nuc_flag[mem5];
                          }
                          nbr[4] += 2;
                        }
                      }
                      nbr[3] += 2;
                    }
                  }
                  nbr[2] += 2;
                }
              }
              nbr[1] += 2;
            }
            nbr[0] += 2;
          }
        }

        if(nf) {
          if((ring_mode) && ((finder == 1) || (finder >= 3))) {
            if((c1_at >= 0) && (base_at >= 0)) {
              int save_at = sugar_at;
              sugar_at = c1_at;
              {
                AtomInfoType *sug_ai = atomInfo + sugar_at;
                AtomInfoType *bas_ai = atomInfo + base_at;
                int sc_helper1, sc_helper2;
                AtomInfoGetSetting_b(G, sug_ai, cSetting_cartoon_side_chain_helper, sc_helper, &sc_helper1);
                AtomInfoGetSetting_b(G, bas_ai, cSetting_cartoon_side_chain_helper, sc_helper, &sc_helper2);
                if((sug_ai->visRep & bas_ai->visRep & cRepCartoonBit) &&
                    (!(sc_helper1 || sc_helper2) ||
                     !(bas_ai->visRep & (cRepLineBit | cRepCylBit | cRepSphereBit)))) {

                  int sug, bas;
                  if(obj->DiscreteFlag) {
                    if(cs == obj->DiscreteCSet[sugar_at] &&
                       cs == obj->DiscreteCSet[base_at]) {
                      sug = obj->DiscreteAtmToIdx[sugar_at];
                      bas = obj->DiscreteAtmToIdx[base_at];
                    } else {
                      sug = -1;
                      bas = -1;
                    }
                  } else {
                    sug = cs->AtmToIdx[sugar_at];
                    bas = cs->AtmToIdx[base_at];
                  }

                  if((sug >= 0) && (bas >= 0)) {
		    {
		      float *color1, *color2;
		      if (ladder_color >= 0) {
			color1 = color2 = ColorGet(G, ladder_color);
		      } else {
			color1 = ColorGet(G, sug_ai->color);
			color2 = ColorGet(G, bas_ai->color);
		      }
		      if (is_picking){
			float midv[3], midc[3];
			average3f(cs->Coord + 3 * sug, cs->Coord + 3 * bas, midv);
			average3f(color1, color2, midc);
			if(!sug_ai->masked)
			  CGOPickColor(cgo, sugar_at, cPickableAtom);
			CGOCustomCylinderv(cgo, cs->Coord + 3 * sug, midv, ladder_radius, 
					   color1, midc, 2.0F, 0.0F);
			if(!bas_ai->masked)
			  CGOPickColor(cgo, base_at, cPickableAtom);
			CGOCustomCylinderv(cgo, midv, cs->Coord + 3 * bas, ladder_radius, 
					   midc, color2, 0.0F, 2.0F);
		      } else {
			CGOCustomCylinderv(cgo, cs->Coord + 3 * sug,
					   cs->Coord + 3 * bas,
					   ladder_radius,
					   color1, color2, 2.0F, 2.0F);
		      }
		    }
                  }
                }
              }
              base_at = save_at;
              sugar_at = save_at;
            }
          }
          if((base_at >= 0) && (sugar_at >= 0)) {
            AtomInfoType *sug_ai = atomInfo + sugar_at;
            AtomInfoType *bas_ai = atomInfo + base_at;
            int sc_helper1, sc_helper2;
            AtomInfoGetSetting_b(G, sug_ai, cSetting_cartoon_side_chain_helper, sc_helper, &sc_helper1);
            AtomInfoGetSetting_b(G, bas_ai, cSetting_cartoon_side_chain_helper, sc_helper, &sc_helper2);
            if((sug_ai->visRep & bas_ai->visRep & cRepCartoonBit) &&
                (!(sc_helper1 || sc_helper2) ||
                 !(bas_ai->visRep & (cRepLineBit | cRepCylBit | cRepSphereBit)))) {

              int sug, bas;
              float *v_outer, tmp[3], outer[3];
              if(obj->DiscreteFlag) {
                if(cs == obj->DiscreteCSet[sugar_at] && cs == obj->DiscreteCSet[base_at]) {
                  sug = obj->DiscreteAtmToIdx[sugar_at];
                  bas = obj->DiscreteAtmToIdx[base_at];
                } else {
                  sug = -1;
                  bas = -1;
                }
              } else {
                sug = cs->AtmToIdx[sugar_at];
                bas = cs->AtmToIdx[base_at];
              }

              if((sug >= 0) && (bas >= 0)) {
                int p3, p5;
                v_outer = cs->Coord + 3 * sug;

                if((o3_at >= 0) && (phos3_at < 0))
                  phos3_at = o3_at;
                if((o5_at >= 0) && (phos5_at < 0))
                  phos5_at = o5_at;
                if((ndata->na_mode != 1) && (phos3_at >= 0) && (phos5_at >= 0)) {

                  if(obj->DiscreteFlag) {
                    if(cs == obj->DiscreteCSet[phos3_at] &&
                       cs == obj->DiscreteCSet[phos5_at]) {
                      p3 = obj->DiscreteAtmToIdx[phos3_at];
                      p5 = obj->DiscreteAtmToIdx[phos5_at];
                    } else {
                      p3 = -1;
                      p5 = -1;
                    }
                  } else {
                    p3 = cs->AtmToIdx[phos3_at];
                    p5 = cs->AtmToIdx[phos5_at];
                  }
                  if((p3 >= 0) && (p5 >= 0)) {
                    if(ring_mode) {
                      scale3f(cs->Coord + 3 * p5, 0.333333F, outer);
                      scale3f(cs->Coord + 3 * p3, 0.666667F, tmp);
                    } else {
                      scale3f(cs->Coord + 3 * p3, 0.5F, outer);
                      scale3f(cs->Coord + 3 * p5, 0.5F, tmp);
                    }
                    add3f(tmp, outer, outer);
                    v_outer = outer;
                  }
                }
		{
		  float *color1, *color2;
		  if (ladder_color >= 0) {
		    color1 = color2 = ColorGet(G, ladder_color);
		  } else {
		    color1 = ColorGet(G, sug_ai->color);
		    color2 = ColorGet(G, bas_ai->color);
		  }
		  if (is_picking){
		    float midv[3], midc[3];
		    average3f(v_outer, cs->Coord + 3 * bas, midv);
		    average3f(color1, color2, midc);
		    if(!sug_ai->masked)
		      CGOPickColor(cgo, sugar_at, cPickableAtom);
		    CGOCustomCylinderv(cgo, v_outer, midv, ladder_radius, 
				       color1, midc, 2.0F, 0.0F);
		    if(!bas_ai->masked)
		      CGOPickColor(cgo, base_at, cPickableAtom);
		    CGOCustomCylinderv(cgo, midv, cs->Coord + 3 * bas, ladder_radius, 
				       midc, color2, 0.0F, 2.0F);
		  } else {
		    CGOCustomCylinderv(cgo, v_outer,
				       cs->Coord + 3 * bas,
				       ladder_radius,
				       color1, color2, 2.0F, 2.0F);
		  }
		}
              }
            }
          }
        }
      }
    }
    if((!ring_mode) || (finder == 2)) {
      if(ladder_mode) {         /* mark sure all rings traversed are mark */
        int i;
        for(i = 0; i <= n_atom; i++) {
          marked[atix[i]] = true;
        }
      }
    }
    if((!nf) && ((have_C4_prime >= 0) || (have_C4 >= 0))) {
      int nbr[9];
      int *neighbor = obj->Neighbor;
      int mem0, mem1, mem2, mem3, mem4, mem5, mem6, mem7, mem8, mem9;
      /* see if any of the neighbors are confirmed nucleic acids... */
      if(have_C4_prime >= 0)
        mem0 = have_C4_prime;
      else if(have_C4 >= 0)
        mem0 = have_C4;
      else
        mem0 = -1;
      if(mem0 >= 1) {
        nbr[0] = neighbor[mem0] + 1;
        while((!nf) && (mem1 = neighbor[nbr[0]]) >= 0) {
          if(!nf)
            nf = nuc_flag[mem1];
          nbr[1] = neighbor[mem1] + 1;
          while((!nf) && (mem2 = neighbor[nbr[1]]) >= 0) {
            if(mem2 != mem0) {
              if(!nf)
                nf = nuc_flag[mem2];
              nbr[2] = neighbor[mem2] + 1;
              while((!nf) && (mem3 = neighbor[nbr[2]]) >= 0) {
                if((mem3 != mem1) && (mem3 != mem0)) {
                  if(!nf)
                    nf = nuc_flag[mem3];
                  nbr[3] = neighbor[mem3] + 1;
                  while((mem4 = neighbor[nbr[3]]) >= 0) {
                    if(mem4 != mem2) {
                      if(!nf)
                        nf = nuc_flag[mem4];
                      nbr[4] = neighbor[mem4] + 1;
                      while((mem5 = neighbor[nbr[4]]) >= 0) {
                        if(mem5 != mem3) {
                          if(!nf)
                            nf = nuc_flag[mem5];
                          nbr[5] = neighbor[mem5] + 1;
                          while((mem6 = neighbor[nbr[5]]) >= 0) {
                            if(mem6 != mem4) {
                              if(!nf)
                                nf = nuc_flag[mem6];
                              nbr[6] = neighbor[mem6] + 1;
                              while((mem7 = neighbor[nbr[6]]) >= 0) {
                                if(mem7 != mem5) {
                                  if(!nf)
                                    nf = nuc_flag[mem7];
                                  nbr[7] = neighbor[mem7] + 1;
                                  while((mem8 = neighbor[nbr[7]]) >= 0) {
                                    if(mem8 != mem6) {
                                      if(!nf)
                                        nf = nuc_flag[mem8];
                                      nbr[8] = neighbor[mem8] + 1;
                                      while((mem9 = neighbor[nbr[8]]) >= 0) {
                                        if(mem9 != mem7) {
                                          if(!nf)
                                            nf = nuc_flag[mem9];
                                        }
                                        nbr[8] += 2;
                                      }
                                    }
                                    nbr[7] += 2;
                                  }
                                }
                                nbr[6] += 2;
                              }
                            }
                            nbr[5] += 2;
                          }
                        }
                        nbr[4] += 2;
                      }
                    }
                    nbr[3] += 2;
                  }
                }
                nbr[2] += 2;
              }
            }
            nbr[1] += 2;
          }
          nbr[0] += 2;
        }
      }
    }
    if(n_atom) {                /* store center of ring */

      float avg[3];
      float avg_col[3];
      int i;
      float up[3], upi[3];
      float vc0[3], vc1[3];
      float *color = NULL;
      /* compute average coordinate and mark atoms so that ring is only drawn once */
      zero3f(avg);
      zero3f(avg_col);

      for(i = 0; i < n_atom; i++) {
        add3f(avg, v_i[i], avg);
        add3f(avg_col, col[i], avg_col);
        marked[atix[i]] = true;
      }

      scale3f(avg, 1.0F / n_atom, avg);
      scale3f(avg_col, 1.0F / n_atom, avg_col);

      for(i = 0; i < n_atom; i++) {
        float *v_moved = moved + atix[i] * 3;
        copy3f(avg, v_moved);   /* store ring center for later use */
      }

      if((nf || (!ladder_mode) || (finder >= 3)) &&
         ring_mode &&
         (((finder == 1) && ((have_C4 >= 0) || (have_C4_prime >= 0))) ||
          ((finder == 2) && ((have_C4 >= 0))) ||
          ((finder == 3) && ((have_C_number >= 0))) || ((finder == 4)))) {

        if((alpha != 1.0F) || (ring_alpha != alpha))
          CGOAlpha(cgo, ring_alpha);

        if(ring_color >= 0) {
          color = ColorGet(G, ring_color);
        } else {
          color = avg_col;
        }

        CGOColorv(cgo, color);

        if((ring_mode == 4) || (ring_mode == 5)) {      /* spherical ring  */
          float radius = ring_radius;
          if(radius < 0.0F) {
            radius = 0.0F;
            if(ring_mode == 4) {
              for(i = 0; i < n_atom; i++) {
                float dist = diff3f(avg, v_i[i]);
                if(radius < dist)
                  radius = dist;
              }
            } else {
              radius = ladder_radius;
            }
          }

          if(n_atom) {
	    CGOColorv(cgo, avg_col);
            CGOPickColor(cgo, atix[0], ai_i[0]->masked ? cPickableNoPick : cPickableAtom);
            CGOSphere(cgo, avg, radius);
          }

        } else {

          /* clear the normals */

          for(i = 0; i <= n_atom; i++) {
            zero3f(n_up[i]);
            zero3f(n_dn[i]);
          }

          /* compute average normals */

          {
            float acc[3];
            int ii;

            zero3f(acc);
            for(i = 0; i < n_atom; i++) {
              ii = i + 1;
              subtract3f(v_i[i], avg, vc0);
              subtract3f(v_i[ii], avg, vc1);
              cross_product3f(vc0, vc1, up);
              add3f(up, n_up[i], n_up[i]);
              add3f(up, n_up[ii], n_dn[ii]);
              if(!i) {
                add3f(up, n_up[n_atom], n_up[n_atom]);
              } else if(ii == n_atom) {
                add3f(up, n_up[0], n_up[0]);
              }
              add3f(up, acc, acc);
            }
            normalize3f(up);
            scale3f(up, -1.0F, upi);
          }

          for(i = 0; i <= n_atom; i++) {
            normalize3f(n_up[i]);
            scale3f(n_up[i], -1.0F, n_dn[i]);
          }

          {
            int ii;
            float mid[3];
            float up_add[3];
            float ct[3], cb[3];
            float v0t[3], v0b[3];
            float v1t[3], v1b[3];
            float out[3];

            CGOBegin(cgo, GL_TRIANGLES);
            for(i = 0; i < n_atom; i++) {
              ii = i + 1;
              average3f(v_i[ii], v_i[i], mid);

              subtract3f(mid, avg, out);        /* compute outward-facing normal */
              normalize3f(out);

              scale3f(up, ring_width, up_add);

              add3f(avg, up_add, ct);
              subtract3f(avg, up_add, cb);

              add3f(v_i[i], up_add, v0t);
              subtract3f(v_i[i], up_add, v0b);

              add3f(v_i[ii], up_add, v1t);
              subtract3f(v_i[ii], up_add, v1b);

              CGONormalv(cgo, up);
              if(ring_color < 0)
                CGOColorv(cgo, color);
              CGOPickColor(cgo, atix[i], ai_i[i]->masked ? cPickableNoPick : cPickableAtom);
              CGOVertexv(cgo, ct);
              CGONormalv(cgo, n_up[i]);
              if(ring_color < 0)
                CGOColorv(cgo, col[i]);
	      //              CGOPickColor(cgo, atix[i], cPickableAtom);
              CGOVertexv(cgo, v0t);
              CGONormalv(cgo, n_up[ii]);
              if(ring_color < 0)
                CGOColorv(cgo, col[ii]);
              CGOPickColor(cgo, atix[ii], ai_i[ii]->masked ? cPickableNoPick : cPickableAtom);
	      //              CGOPickColor(cgo, atix[ii], cPickableAtom);
              CGOVertexv(cgo, v1t);

              if(ring_mode > 1) {
                CGONormalv(cgo, out);

                if(ring_color < 0)
                  CGOColorv(cgo, col[i]);
		//                CGOPickColor(cgo, atix[i], cPickableAtom);
                CGOVertexv(cgo, v0t);
                CGOVertexv(cgo, v0b);
                if(ring_color < 0)
                  CGOColorv(cgo, col[ii]);
                CGOVertexv(cgo, v1t);
                CGOVertexv(cgo, v1t);
                if(ring_color < 0)
                  CGOColorv(cgo, col[i]);
                CGOVertexv(cgo, v0b);
                if(ring_color < 0)
                  CGOColorv(cgo, col[ii]);
                CGOVertexv(cgo, v1b);
              }

              CGONormalv(cgo, upi);
              if(ring_color < 0)
                CGOColorv(cgo, color);
              CGOVertexv(cgo, cb);
              CGONormalv(cgo, n_dn[ii]);
              if(ring_color < 0)
                CGOColorv(cgo, col[ii]);
              CGOVertexv(cgo, v1b);
              CGONormalv(cgo, n_dn[i]);
              if(ring_color < 0)
                CGOColorv(cgo, col[i]);
              CGOVertexv(cgo, v0b);

            }
            CGOEnd(cgo);

            if((alpha != 1.0F) || (ring_alpha != alpha))
              CGOAlpha(cgo, alpha);

	    {
	      float ring_width_for_mode = ring_width;
	      if (ring_mode == 3){
		ring_width_for_mode = 3.f * ring_width;
	      }
              for(i = 0; i < n_atom; i++) {
                ii = i + 1;
		{
		  float *color1, *color2;
		  if (ring_color < 0) {
		    color1 = col[i];
		    color2 = col[ii];
		  } else {
		    color1 = color2 = color;
		  }
		  if (is_picking){
		    float midv[3], midc[3];
		    average3f(v_i[i], v_i[ii], midv);
		    average3f(color1, color2, midc);
		    CGOPickColor(cgo, atix[i], cPickableAtom);		    
		    CGOCustomCylinderv(cgo, v_i[i], midv, ring_width_for_mode, 
				       color1, midc, 2.0F, 0.0F);
		    CGOPickColor(cgo, atix[ii], cPickableAtom);		    
		    CGOCustomCylinderv(cgo, midv, v_i[ii], ring_width_for_mode, 
				       midc, color2, 0.0F, 2.0F);
		  } else {
		    CGOSausage(cgo, v_i[i], v_i[ii], ring_width_for_mode, color1, color2);
		  }
		}
              }
            }
          }
        }
      }
    }
  }
}

/*
 * for nucleic acid polymers, fill "ndata" with:
 * - cartoon type and secondary structure
 * - check if single nucleotide or actual polymer
 */
static void nuc_acid(PyMOLGlobals * G, nuc_acid_data *ndata, int a, int a1, AtomInfoType * ai, CoordSet * cs,
                     ObjectMolecule * obj, int set_flags)
{
  int a3, a4, st, nd;
  float *v_o, *v_c;
  float *v1;
  int cur_car;
  const auto& nuc_flag = ndata->nuc_flag;

  if(ndata->a2 < 0) {
    ndata->nSeg++;
    ndata->v_o_last = NULL;
  }
  *(ndata->sptr++) = ndata->nSeg;
  *(ndata->iptr++) = a;
  cur_car = ai->cartoon;
  if(cur_car == cCartoon_auto)
    cur_car = cCartoon_tube;
  (*ndata->ss) = 3;                      /* DNA/RNA */

  if(cur_car == cCartoon_putty)
    ndata->putty_flag = true;

  *(ndata->cc++) = cur_car;
  v1 = cs->Coord + 3 * a;
  copy3f(v1, ndata->vptr);
  ndata->vptr += 3;

  if(ndata->a2 >= 0) {
    if(set_flags) {
      if((obj->AtomInfo[ndata->a2].protons == cAN_P) && (!nuc_flag[ndata->a2])) {
        int *nf = NULL;
        AtomInfoBracketResidueFast(G, obj->AtomInfo, obj->NAtom, ndata->a2, &st, &nd);

        nf = nuc_flag + st;
        for(a3 = st; a3 <= nd; a3++) {
          *(nf++) = true;
        }
      }
    } else if((ndata->na_mode >= 2) && (!nuc_flag[ndata->a2])) {      /* just a single nucleotide -- skip */
      cur_car = cCartoon_skip;
      *(ndata->cc - 2) = cCartoon_skip;
      *(ndata->cc - 1) = cCartoon_skip;
    }
  }

  ndata->a2 = a1;

  ndata->ss++;

  v_c = NULL;
  v_o = NULL;

  AtomInfoBracketResidueFast(G, obj->AtomInfo, obj->NAtom, a1, &st, &nd);

  {
    int *nf = NULL;
    if(set_flags && ndata->v_o_last)
      nf = nuc_flag + st;
    for(a3 = st; a3 <= nd; a3++) {
      if(nf)
        *(nf++) = true;         /* mark this residue as being part of a nucleic acid chain */
      a4 = cs->atmToIdx(a3);
      if(a4 >= 0) {
        if(ndata->na_mode == 1) {
          if(WordMatchExact(G, NUCLEIC_NORMAL1, LexStr(G, obj->AtomInfo[a3].name), 1) ||
             WordMatchExact(G, NUCLEIC_NORMAL2, LexStr(G, obj->AtomInfo[a3].name), 1)) {
            v_c = cs->Coord + 3 * a4;
          }
        } else if(a3 == a1) {
          v_c = cs->Coord + 3 * a4;
        }
        if(WordMatchExact(G, NUCLEIC_NORMAL0, LexStr(G, obj->AtomInfo[a3].name), 1)) {
          v_o = cs->Coord + 3 * a4;
        }
      }
    }
  }
  if(!(v_c && v_o)) {
    zero3f(ndata->voptr);
    ndata->v_o_last = NULL;
  } else {
    if(ndata->v_o_last) {
      float t0[3];
      add3f(v_o, ndata->v_o_last, t0);
      add3f(ndata->v_o_last, t0, t0);
      scale3f(t0, 0.333333F, t0);
      subtract3f(v_c, t0, ndata->voptr);
    } else {
      subtract3f(v_c, v_o, ndata->voptr);
    }
    ndata->v_o_last = v_o;
    normalize3f(ndata->voptr);
  }
  ndata->voptr += 3;
  ndata->nAt++;
  return;
}

static
void GenerateRepCartoonDrawDebugLineAlongPath(CGO *cgo, int nAt, float *pv){
  float *v, *v1, *v2, *v3, *v4;
  float t0[3], t1[3];
  int a;
  CGOColor(cgo, 1.0, 1.0, 1.0);
#ifndef PURE_OPENGL_ES_2
  CGODisable(cgo, GL_LIGHTING);
#endif
  v1 = NULL;
  v2 = NULL;
  v3 = NULL;
  v4 = NULL;
  v = pv;
  if(nAt > 1) {
    CGOBegin(cgo, GL_LINE_STRIP);
    for(a = 0; a < nAt; a++) {
      v4 = v3;
      v3 = v2;
      v2 = v1;
      v1 = v;
      if(v1 && v2 && v3 && v4) {
        add3f(v1, v4, t0);
        add3f(v2, v3, t1);
        /*            scale3f(t0,0.2024,t0);
                      scale3f(t1,0.2727,t1); */
        scale3f(t0, 0.2130F, t0);
        scale3f(t1, 0.2870F, t1);
        
        add3f(t0, t1, t0);
        CGOVertexv(cgo, t0);
      }
      v += 3;
    }
    CGOEnd(cgo);
  }
}

static
int GenerateRepCartoonDrawDebugNormals(CGO *cgo, CExtrude *ex, int n_p){
  int b, ok;
  float *v, *vn;
  float t0[3];
  ok = CGOColor(cgo, 0.0, 1.0, 0.0);
  v = ex->p;
  vn = ex->n + 3;
#ifndef PURE_OPENGL_ES_2
  if (ok)
    ok &= CGODisable(cgo, GL_LIGHTING);
#endif
  if (ok)
    ok &= CGOBegin(cgo, GL_LINES);
  for(b = 0; ok && b < n_p; b++) {
    ok &= CGOVertexv(cgo, v);
    add3f(v, vn, t0);
    if (ok)
      ok &= CGOVertexv(cgo, t0);
    v += 3;
    vn += 9;
  }
  if (ok)
    ok &= CGOEnd(cgo);
#ifndef PURE_OPENGL_ES_2
  if (ok)
    ok &= CGOEnable(cgo, GL_LIGHTING);
#endif
  return ok;
}

static
int GenerateRepCartoonDrawDebugOrient(CGO *cgo, int nAt, float *pv, float *pvo, float *tv){
  int ok, a;
  float *v1, *v2, *v3;
  float t0[3];
  ok = CGOColor(cgo, 1.0, 1.0, 1.0);
#ifndef PURE_OPENGL_ES_2
  ok &= CGODisable(cgo, GL_LIGHTING);
#endif
  if (ok)
    ok &= CGOBegin(cgo, GL_LINES);
  v1 = pv;
  v2 = pvo;
  v3 = tv;
  for(a = 0; ok && a < nAt; a++) {
    ok &= CGOVertexv(cgo, v1);
    if (ok){
      add3f(v1, v2, t0);
      add3f(v2, t0, t0);
      ok &= CGOVertexv(cgo, t0);
    }
    if (ok){
      subtract3f(v1, v3, t0);
      ok &= CGOVertexv(cgo, t0);
    }
    if (ok){
      add3f(v1, v3, t0);
      ok &= CGOVertexv(cgo, t0);
    }
    v1 += 3;
    v2 += 3;
    v3 += 3;
  }
  if (ok)
    ok &= CGOEnd(cgo);
#ifndef PURE_OPENGL_ES_2
  if (ok)
    ok &= CGOEnable(cgo, GL_LIGHTING);
#endif
  return ok;
}

static
int GenerateRepCartoonProcessCylindricalHelices(PyMOLGlobals * G, ObjectMolecule * obj, CoordSet *cs, CGO *cgo, CExtrude *ex, int nAt, int *seg, float *pv, float *tv, float *pvo,
    const CCInOut *cc,
    int *at, float *dl, int cartoon_color, int discrete_colors, float loop_radius){
  int ok = true;
  int n_p, n_pm1, n_pm2;
  float *v, *v0, *v1, *v2, *vo, *d;
  float *vc = NULL;
  int atom_index1, atom_index2, *s,
      *atp, a, cur_car;
  int *vi;
  int last_color, uniform_color;
  int contFlag, extrudeFlag;
  int contigFlag;
  int b, c1, c2;
  float *h_start = NULL, *h_end = NULL;
  float t0[3], t1[3], t2[3], t3[3];
  float t4[3];
  float helix_radius;
  CGOPickColor(cgo, 0, cPickableNoPick);
  helix_radius =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_helix_radius);

    /* this is confusing because we're borrowing Extrude's arrays 
     * for convenient storage, but not actually calling Extrude */
  n_p = 0;
  v = ex->p;
  vc = ex->c;
  vi = ex->i;
  
  last_color = -1;
  uniform_color = true;
  
  v1 = pv;                    /* points */
  v2 = tv;                    /* tangents */
  vo = pvo;
  d = dl;
  s = seg;
  atp = at;                   /* cs index pointer */
  a = 0;
  contFlag = true;
  cur_car = cCartoon_skip;
  extrudeFlag = false;
  contigFlag = false;
  
  while(contFlag) {
    if((*cc) != cur_car) {    /* new cartoon type */
      if(n_p) {               /* any cartoon points? */
        extrudeFlag = true;
      } else {
        cur_car = *(cc);      /* now: go ahead and switch cartoons */
        n_p = 0;
        v = ex->p;
        vc = ex->c;
        vi = ex->i;
        last_color = -1;
        uniform_color = true;
      }
    }
    if(a && !extrudeFlag) {
      if((*s) != *(s - 1)) {  /* new segment */
        contigFlag = false;
        if(n_p) {             /* any cartoon points? */
          extrudeFlag = true;
        } else {
          n_p = 0;
          v = ex->p;
          vc = ex->c;
          vi = ex->i;
          last_color = -1;
          uniform_color = true;
        }
      }
    }
    if(!extrudeFlag) {
      if((a < (nAt - 1)) && (*s == *(s + 1))) {       /* working in the same segment... */
        AtomInfoType *ai1, *ai2;
        atom_index1 = cs->IdxToAtm[*atp];
        atom_index2 = cs->IdxToAtm[*(atp + 1)];
        ai1 = obj->AtomInfo + atom_index1;
        ai2 = obj->AtomInfo + atom_index2;

        c1 = ai1->color;
        c2 = ai2->color;
        
        if(cartoon_color >= 0) {
          c1 = (c2 = cartoon_color);
        }
        AtomInfoGetSetting_color(G, ai1, cSetting_cartoon_color,
                                 c1, &c1);
        AtomInfoGetSetting_color(G, ai2, cSetting_cartoon_color,
                                 c2, &c2);
        
        if((*(cc) == *(cc + 1)) && (c1 != c2))
          uniform_color = false;
        if(last_color >= 0) {
          if(c1 != last_color)
            uniform_color = false;
        }
        last_color = c1;
        
        v0 = ColorGet(G, c1);
        *(vc++) = *(v0++);
        *(vc++) = *(v0++);
        *(vc++) = *(v0++);
        *(vi++) = ai1->masked ? -1 : atom_index1;
        
        v0 = ColorGet(G, c2); /* kludge */
        *(vc) = *(v0++);
        *(vc + 1) = *(v0++);
        *(vc + 2) = *(v0++);
        *(vi) = ai2->masked ? -1 : atom_index2;
      } else {
        vc += 3;              /* part of kludge */
        vi++;
      }
      if(cur_car == cCartoon_skip_helix) {
        if(!n_p) {
          h_start = v1;
          h_end = v1;
        } else {
          h_end = v1;
        }
        copy3f(v1, v);        /* just store coordinates until we have a complete cylinder */
        v += 3;
        n_p++;
      }
      v1 += 3;
      v2 += 3;
      vo += 3;
      d++;
      atp++;
      s++;
      cc++;
    }
    a++;
    if(a == nAt) {
      contFlag = false;
      if(n_p)
        extrudeFlag = true;
    }
    if(extrudeFlag) {         /* generate cylinder */
      contigFlag = true;
      if((a < nAt) && extrudeFlag) {
        if(*(s - 1) != *(s))
          contigFlag = false;
      }
      
      if(n_p > 1) {
        atom_index1 = cs->IdxToAtm[*(atp - 1)];
        c1 = (obj->AtomInfo + atom_index1)->color;
        if(cartoon_color >= 0) {
          c1 = cartoon_color;
        }
        AtomInfoGetSetting_color(G, obj->AtomInfo + atom_index1, cSetting_cartoon_color,
                                 c1, &c1);
        if(n_p < 5) {
          copy3f(ex->p, t3);
          copy3f(v - 3, t4);
        } else {
          add3f(ex->p, ex->p + 9, t0);
          add3f(ex->p + 3, ex->p + 6, t1);
          scale3f(t0, 0.2130F, t0);
          scale3f(t1, 0.2870F, t1);
          add3f(t0, t1, t3);
          add3f(v - 3, v - 12, t0);
          add3f(v - 6, v - 9, t1);
          scale3f(t0, 0.2130F, t0);
          scale3f(t1, 0.2870F, t1);
          add3f(t0, t1, t4);
          
          /* extend helix to line up with CA */
          subtract3f(t4, t3, t0);
          normalize3f(t0);
          subtract3f(v - 3, t3, t1);
          project3f(t1, t0, t4);
          add3f(t3, t4, t4);
          invert3f(t0);
          subtract3f(ex->p, t4, t1);
          project3f(t1, t0, t3);
          add3f(t3, t4, t3);
          
          /* relocate terminal CA to touch helix, if necessary */
          if(h_start && h_end) {
            float f0;
            subtract3f(h_start, t3, t0);
            f0 = helix_radius - loop_radius * 2;
            if(length3f(t0) > f0) {
              normalize3f(t0);
              scale3f(t0, f0, t1);
              add3f(t1, t3, h_start);
            }
            subtract3f(h_end, t4, t0);
            if(length3f(t0) > f0) {
              normalize3f(t0);
              scale3f(t0, f0, t1);
              add3f(t1, t4, h_end);
            }
          }
        }
        /* push helix out a tad to consume loop */
        subtract3f(t4, t3, t0);
        normalize3f(t0);
        scale3f(t0, loop_radius * 2, t0);
        add3f(t0, t4, t4);
        invert3f(t0);
        add3f(t0, t3, t3);
        
        if(uniform_color) {
	  CGOCylinderv(cgo, t3, t4, helix_radius, ex->c, ex->c);
        } else {
          subtract3f(t4, t3, t0);
          n_pm1 = n_p - 1;
          n_pm2 = n_p - 2;
          for(b = 0; ok && b < n_pm1; b++) {
            if(!b) {
              scale3f(t0, ((float) b - 0.005F) / n_pm1, t1);  /* add small overlap */
            } else {
              scale3f(t0, ((float) b) / n_pm1, t1);
            }
            if(b < n_pm2) {
              scale3f(t0, ((float) b + 1.005F) / n_pm1, t2);
            } else {
              scale3f(t0, ((float) b + 1) / n_pm1, t2);
            }
            add3f(t3, t1, t1);
            add3f(t3, t2, t2);
            CGOCustomCylinderv(cgo, t1, t2, helix_radius, ex->c + (b * 3),
                                     ex->c + (b + 1) * 3, (float) (b ? 0 : cCylCapFlat),
                                     (float) (b == n_pm2 ? cCylCapFlat : 0));
          }
        }
      }
      a--;                    /* undo above... */
      extrudeFlag = false;
      n_p = 0;
      v = ex->p;
      vc = ex->c;
      vi = ex->i;
      uniform_color = true;
      last_color = -1;
    }
  }
  return ok;
}

static
int GenerateRepCartoonDrawRings(PyMOLGlobals * G, nuc_acid_data *ndata, ObjectMolecule * obj,
                                bool is_picking,
                                CoordSet * cs, CGO *cgo, 
                                float ring_width, int cartoon_color, float alpha){
  int ring_i;
  int mem[8];
  int nbr[7];
  int *neighbor;
  int *marked = Calloc(int, obj->NAtom);
  float *moved = Calloc(float, obj->NAtom * 3);
  int ring_color;
  int ok = true;
  int escape_count;
  int *atmToIdx = NULL;
  int ladder_mode, ladder_color;
  float ladder_radius, ring_radius;
  int cartoon_side_chain_helper;
  float ring_alpha;
  ring_alpha =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ring_transparency);
  if(ring_alpha < 0.0F)
    ring_alpha = alpha;
  else
    ring_alpha = 1.0F - ring_alpha;
  cartoon_side_chain_helper =
    SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_side_chain_helper);
  ladder_mode =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ladder_mode);
  ladder_radius =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ladder_radius);
  ladder_color =
    SettingGet_color(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ladder_color);
  ring_radius =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ring_radius);
  if(ladder_color == -1)
    ladder_color = cartoon_color;
  ring_color =
    SettingGet_color(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ring_color);
  if(ring_color == -1)
    ring_color = cartoon_color;

  if(!obj->DiscreteFlag)
    atmToIdx = cs->AtmToIdx;
  
  ok &= ObjectMoleculeUpdateNeighbors(obj);
  neighbor = obj->Neighbor;
  
  escape_count = ESCAPE_MAX;  /* don't get bogged down with structures 
                                 that have unreasonable connectivity */
  for(ring_i = 0; ok && ring_i < ndata->n_ring; ring_i++) {
    mem[0] = ndata->ring_anchor[ring_i];
    nbr[0] = neighbor[mem[0]] + 1;
    while(((mem[1] = neighbor[nbr[0]]) >= 0) &&
          ((!atmToIdx) || (atmToIdx[mem[0]] >= 0))) {
      nbr[1] = neighbor[mem[1]] + 1;
      while(((mem[2] = neighbor[nbr[1]]) >= 0) &&
            ((!atmToIdx) || (atmToIdx[mem[1]] >= 0))) {
        if(mem[2] != mem[0]) {
          nbr[2] = neighbor[mem[2]] + 1;
          while(((mem[3] = neighbor[nbr[2]]) >= 0) &&
                ((!atmToIdx) || (atmToIdx[mem[2]] >= 0))) {
            if(mem[3] != mem[1]) {
              nbr[3] = neighbor[mem[3]] + 1;
              while(((mem[4] = neighbor[nbr[3]]) >= 0) &&
                    ((!atmToIdx) || (atmToIdx[mem[3]] >= 0))) {
                if((mem[4] != mem[2]) && (mem[4] != mem[1]) && (mem[4] != mem[0])) {
                  nbr[4] = neighbor[mem[4]] + 1;
                  while(((mem[5] = neighbor[nbr[4]]) >= 0) &&
                        ((!atmToIdx) || (atmToIdx[mem[4]] >= 0))) {
                    if(!(escape_count--))
                      goto escape;
                    if((mem[5] != mem[3]) && (mem[5] != mem[2]) && (mem[5] != mem[1])) {
                      if(mem[5] == mem[0]) {  /* five-cycle */
                        /*    printf(" 5: %s(%d) %s(%d) %s(%d) %s(%d) %s(%d)\n",
                              obj->AtomInfo[mem[0]].name,mem[0],
                              obj->AtomInfo[mem[1]].name,mem[1],
                              obj->AtomInfo[mem[2]].name,mem[2],
                              obj->AtomInfo[mem[3]].name,mem[3],
                              obj->AtomInfo[mem[4]].name,mem[4]); */
                        do_ring(G, ndata, is_picking, 5, mem, obj, cs, ring_width, cgo, ring_color,
                                ladder_radius, ladder_color, ladder_mode,
                                cartoon_side_chain_helper,
                                ring_alpha, alpha, marked, moved, ring_radius);
                      }
                      nbr[5] = neighbor[mem[5]] + 1;
                      while(((mem[6] = neighbor[nbr[5]]) >= 0) &&
                            ((!atmToIdx) || (atmToIdx[mem[5]] >= 0))) {
                        if((mem[6] != mem[4]) && (mem[6] != mem[3])
                           && (mem[6] != mem[2]) && (mem[6] != mem[1])) {
                          if(mem[6] == mem[0]) {      /* six-cycle */
                            /* printf(" 6: %s %s %s %s %s %s\n",
                               obj->AtomInfo[mem[0]].name,
                               obj->AtomInfo[mem[1]].name,
                               obj->AtomInfo[mem[2]].name,
                               obj->AtomInfo[mem[3]].name,
                               obj->AtomInfo[mem[4]].name,
                               obj->AtomInfo[mem[5]].name
                               ); */
                            do_ring(G, ndata, is_picking, 6, mem, obj, cs, ring_width, cgo, ring_color,
                                    ladder_radius, ladder_color, ladder_mode,
                                    cartoon_side_chain_helper,
                                    ring_alpha, alpha, marked, moved,
                                    ring_radius);
                          }
                          nbr[6] = neighbor[mem[6]] + 1;
                          while(((mem[7] = neighbor[nbr[6]]) >= 0) &&
                                ((!atmToIdx) || (atmToIdx[mem[6]] >= 0))) {
                            if((mem[7] != mem[5]) && (mem[7] != mem[4])
                               && (mem[7] != mem[3]) && (mem[7] != mem[2])
                               && (mem[7] != mem[1])) {
                              if(mem[7] == mem[0]) {
                                do_ring(G, ndata, is_picking, 7, mem, obj, cs, ring_width, cgo,
                                        ring_color, ladder_radius,
                                        ladder_color, ladder_mode,
                                        cartoon_side_chain_helper,
                                        ring_alpha, alpha, marked, moved, ring_radius);
                              }
                            }
                            nbr[6] += 2;
                          }
                        }
                        nbr[5] += 2;
                      }
                    }
                    nbr[4] += 2;
                  }
                }
                nbr[3] += 2;
              }
            }
            nbr[2] += 2;
          }
        }
        nbr[1] += 2;
      }
      nbr[0] += 2;
    }
  escape:
    escape_count = ESCAPE_MAX;        /* don't get bogged down with structures 
                                         that have unreasonable connectivity */
  }
  FreeP(marked);
  FreeP(moved);
  return ok;
}

#define EXTRUDE_TRUNCATE() { ExtrudeTruncate(ex, 0); n_p = 0; v = ex->p;        \
                             vc = ex->c; vn = ex->n; vi = ex->i; }

/*
 * skip > dash > loop
 */
inline int prioritize(int a, int b) {
  if (a == b)
    return a;
  if (a == cCartoon_skip || b == cCartoon_skip)
    return cCartoon_skip;
  if (a == cCartoon_dash || b == cCartoon_dash)
    return cCartoon_dash;
  return cCartoon_loop;
}

/* CheckExtrudeContigFlags: compares current cartoon type and next to figure out whether to
      extrude, truncate, and/or whether its contiguous */
static
int CheckExtrudeContigFlags(int nAt, int n_p, int a,
    int *cur_car, // in-out
    const CCInOut *cc,
    const int *segptr,
    int * /* contigFlag */,
    int *extrudeFlag)
{
  int truncate = false;
  int next_car = cCartoon_skip;

  if (a < (nAt - 1) && (*segptr) == *(segptr + 1)) {
    next_car = prioritize(cc[0].getCCOut(), cc[1].getCCIn());
  }

  if (*cur_car != next_car) {   /* new cartoon type */
    if(n_p) {               /* any cartoon points? then extrude, otherwise truncate */
      *extrudeFlag = true;
    } else {
      *cur_car = next_car;      /* no: go ahead and switch cartoons */
      truncate = true;
    }
  }

  return truncate;
}

static
void ComputeCartoonAtomColors(PyMOLGlobals *G, ObjectMolecule *obj, CoordSet *cs, int *nuc_flag, int atom_index1, int atom_index2, int *c1a, int *c2a, int *atp,
    const CCInOut *cc,
    int cur_car, int cartoon_color, int nucleic_color, int discrete_colors, int n_p, int contigFlag){

  int c1, c2;

  if (nucleic_color >= 0 && (nuc_flag[*atp] || nuc_flag[*(atp + 1)])) {
    c1 = c2 = nucleic_color;
  } else {
    c1 = c2 = cartoon_color;
  }

  auto ai1 = obj->AtomInfo + atom_index1;
  auto ai2 = obj->AtomInfo + atom_index2;

  AtomInfoGetSetting_color(G, ai1, cSetting_cartoon_color, c1, &c1);
  AtomInfoGetSetting_color(G, ai2, cSetting_cartoon_color, c2, &c2);

  if (c1 < 0) c1 = ai1->color;
  if (c2 < 0) c2 = ai2->color;

  if(discrete_colors) {
    int next_car = *(cc + 1);
    // end of loop or dash segment
    if (cur_car != next_car) {
      if (cur_car == cCartoon_dash) {
        c2 = c1;
      } else if (next_car == cCartoon_dash) {
        c1 = c2;
      } else if (cur_car == cCartoon_loop) {
        c2 = c1;
      } else if (next_car == cCartoon_loop) {
        c1 = c2;
      }
    } else if (n_p == 0 && contigFlag &&
        (cur_car == cCartoon_dash || cur_car == cCartoon_loop)) {
      // beginning of loop or dash segment
      c1 = c2;
    }
  }
  *c1a = c1;
  *c2a = c2;
}

static
void CartoonGenerateSample(PyMOLGlobals *G, int sampling, int *n_p, float dev, float *vo,
                           float *v1,
                           float *v2, int c1, int c2, int atom_index1, int atom_index2,
                           float power_a, float power_b, float **vc_p,
                           int **vi_p,
                           float **v_p, float **vn_p){
  int b, i0;
  float f0, f1, f2, f3, f4;
  float *v0;
  int *vi = *vi_p;
  float *vc = *vc_p, *v = *v_p, *vn = *vn_p;
  for(b = 0; b < sampling; b++) {       /* needs optimization */
    if(*n_p == 0) {
      /* provide starting point on first point in segment only... */
      f0 = ((float) b) / sampling;      /* fraction of completion */
      if(f0 <= 0.5) {
        v0 = ColorGet(G, c1);
        i0 = atom_index1;
      } else {
        v0 = ColorGet(G, c2);
        i0 = atom_index2;
      }
      f0 = smooth(f0, power_a); /* bias sampling towards the center of the curve */
      /* store colors */
      *(vc++) = *(v0++);
      *(vc++) = *(v0++);
      *(vc++) = *(v0++);
      *(vi++) = i0;
      /* start of line/cylinder */
      f1 = 1.0F - f0;
      f2 = smooth(f0, power_b);
      f3 = smooth(f1, power_b);
      f4 = dev * f2 * f3;       /* displacement magnitude */
      
      *(v++) = f1 * v1[0] + f0 * v1[3] + f4 * (f3 * v2[0] - f2 * v2[3]);
      *(v++) = f1 * v1[1] + f0 * v1[4] + f4 * (f3 * v2[1] - f2 * v2[4]);
      *(v++) = f1 * v1[2] + f0 * v1[5] + f4 * (f3 * v2[2] - f2 * v2[5]);
      vn += 9;
      copy3f(vo, vn - 6);       /* starter... */
      (*n_p)++;
    }
    f0 = ((float) b + 1) / sampling;
    if(f0 <= 0.5) {
      v0 = ColorGet(G, c1);
      i0 = atom_index1;
    } else {
      v0 = ColorGet(G, c2);
      i0 = atom_index2;
    }
    f0 = smooth(f0, power_a);   /* bias sampling towards the center of the curve */
    
    /* store colors */
    *(vc++) = *(v0++);
    *(vc++) = *(v0++);
    *(vc++) = *(v0++);
    *(vi++) = i0;
    
    /* end of line/cylinder */
    f1 = 1.0F - f0;
    f2 = smooth(f0, power_b);
    f3 = smooth(f1, power_b);
    f4 = dev * f2 * f3; /* displacement magnitude */
    
    *(v++) = f1 * v1[0] + f0 * v1[3] + f4 * (f3 * v2[0] - f2 * v2[3]);
    *(v++) = f1 * v1[1] + f0 * v1[4] + f4 * (f3 * v2[1] - f2 * v2[4]);
    *(v++) = f1 * v1[2] + f0 * v1[5] + f4 * (f3 * v2[2] - f2 * v2[5]);
    /*                remove_component3f(vo,v2,o0);
                      remove_component3f(vo+3,v2,o0+3); */
    vn += 3;
    *(vn++) = f1 * (vo[0] * f2) + f0 * (vo[3] * f3);
    *(vn++) = f1 * (vo[1] * f2) + f0 * (vo[4] * f3);
    *(vn++) = f1 * (vo[2] * f2) + f0 * (vo[5] * f3);
    vn += 3;
    
    if(b == sampling - 1)
      copy3f(vo + 3, vn - 6);   /* starter... */
    (*n_p)++;
  }
  (*vc_p) = vc;
  (*vi_p) = vi;
  (*v_p) = v;
  (*vn_p) = vn;
}

static
void CartoonGenerateRefine(int refine, int sampling, float *v, float *vn, float *vo, float *sampling_tmp){
  int b, c;
  float t0[3], t1[3];
  float *p0, *p1, *p2, *p3;
  float f0, f1, f2, f3;
  c = refine;
  cross_product3f(vn + 3 - (sampling * 9), vn + 3 - 9, t0);  // why is this here?
  cross_product3f(vo, vo + 3, t0);
  if((sampling > 1) && length3f(t0) > R_SMALL4) {
    normalize3f(t0);
    while(c--) {
      p0 = v - (sampling * 3) - 3;
      p1 = v - (sampling * 3);
      p2 = v - (sampling * 3) + 3;
      for(b = 0; b < (sampling - 1); b++) {
        f0 = dot_product3f(t0, p0);
        f1 = dot_product3f(t0, p1);
        f2 = dot_product3f(t0, p2);
        
        f3 = (f2 + f0) / 2.0F;
        scale3f(t0, f3 - f1, t1);
        p3 = sampling_tmp + b * 3;
        add3f(t1, p1, p3);
        
        p0 = p1;
        p1 = p2;
        p2 += 3;
      }
      p1 = v - (sampling * 3);
      for(b = 0; b < (sampling - 1); b++) {
        p3 = sampling_tmp + b * 3;
        copy3f(p3, p1);
        p1 += 3;
      }
    }
  }
}

static
int CartoonExtrudeTube(short use_cylinders_for_strands, CExtrude *ex, CGO *cgo,
    float tube_radius, int tube_quality, int tube_cap, int is_picking){
  int ok = true;
  if (use_cylinders_for_strands){
    ok &= ExtrudeCylindersToCGO(ex, cgo, tube_radius, is_picking);
  } else {
    ok &= ExtrudeCircle(ex, tube_quality, tube_radius);
    if (ok)
      ExtrudeBuildNormals1f(ex);
    if (ok)
      ok &= ExtrudeCGOSurfaceTube(ex, cgo, tube_cap, NULL, use_cylinders_for_strands);
  }
  return ok;
}

static
int CartoonExtrudePutty(PyMOLGlobals *G, ObjectMolecule *obj, CoordSet *cs, CGO *cgo, CExtrude *ex,
                        int putty_quality, float putty_radius, float *putty_vals, int sampling){
  int ok;
  ok = ExtrudeCircle(ex, putty_quality, putty_radius);
  if (ok)
    ExtrudeBuildNormals1f(ex);
  if (ok)
    ok &= ExtrudeComputePuttyScaleFactors(ex, obj,
                                          SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_putty_transform),
                                          putty_vals[0], putty_vals[1], putty_vals[2], putty_vals[3],
                                          SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_putty_scale_power),
                                          SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_putty_range),
                                          SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_putty_scale_min),
                                          SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_putty_scale_max),
                                          sampling / 2);
  if (ok)
    ok &= ExtrudeCGOSurfaceVariableTube(ex, cgo, 1);
  return ok;
}

static
int CartoonExtrudeCircle(CExtrude *ex, CGO *cgo, short use_cylinders_for_strands, int loop_quality, float loop_radius, int loop_cap,
    int dash=0) {
  int ok;
  ok = ExtrudeCircle(ex, loop_quality, loop_radius);
  if (ok)
    ExtrudeBuildNormals1f(ex);
  if (ok)
    ok &= ExtrudeCGOSurfaceTube(ex, cgo, loop_cap, NULL, use_cylinders_for_strands, dash);
  return ok;
}

static
int CartoonExtrudeRect(PyMOLGlobals *G, CExtrude *ex, CGO *cgo, float width, float length, int highlight_color){
  int ok;
  if(highlight_color < 0) {
    ok = ExtrudeRectangle(ex, width, length, 0);
    if (ok)
      ExtrudeBuildNormals2f(ex);
    if (ok)
      ok &= ExtrudeCGOSurfacePolygon(ex, cgo, 1, NULL);
  } else {
    ok = ExtrudeRectangle(ex, width, length, 1);
    if (ok)
      ExtrudeBuildNormals2f(ex);
    if (ok)
      ok &= ExtrudeCGOSurfacePolygon(ex, cgo, 0, NULL);
    if (ok){
      ok &= ExtrudeRectangle(ex, width, length, 2);
      if (ok)
        ExtrudeBuildNormals2f(ex);
      if (ok)
        ok &= ExtrudeCGOSurfacePolygon(ex, cgo, 1, ColorGet(G, highlight_color));
    }
  }
  return ok;
}

static
int CartoonExtrudeOval(PyMOLGlobals *G, CExtrude *ex, CGO *cgo, short use_cylinders_for_strands, int oval_quality, float oval_width, float oval_length, int highlight_color){
  int ok;
  ok = ExtrudeOval(ex, oval_quality, oval_width, oval_length);
  if (ok)
    ExtrudeBuildNormals2f(ex);
  if (ok){
    if(highlight_color < 0)
      ok &= ExtrudeCGOSurfaceTube(ex, cgo, 1, NULL, use_cylinders_for_strands);
    else
      ok &= ExtrudeCGOSurfaceTube(ex, cgo, 1, ColorGet(G, highlight_color), use_cylinders_for_strands);
  }
  return ok;
}

static
int CartoonExtrudeArrow(PyMOLGlobals *G, CExtrude *ex, CGO *cgo, int sampling, float width, float length, int highlight_color){
  int ok;
  ok = ExtrudeRectangle(ex, width, length, 0);  // ex->Ns is always 8 for ExtrudeCGOSurfaceStrand
  if (ok)
    ExtrudeBuildNormals2f(ex);
  if (ok){
    if(highlight_color < 0)
      ok &= ExtrudeCGOSurfaceStrand(ex, cgo, sampling, NULL);
    else
      ok &= ExtrudeCGOSurfaceStrand(ex, cgo, sampling, ColorGet(G, highlight_color));
  }
  /* for PLY files      
     ExtrudeCircle(ex,loop_quality,loop_radius);
     ExtrudeBuildNormals1f(ex);
     ExtrudeCGOSurfaceTube(ex,cgo,loop_cap,NULL);
  */
  return ok;
}

static
int CartoonExtrudeDumbbell(PyMOLGlobals *G, CExtrude *ex, CGO *cgo, int sampling, float dumbbell_width, float dumbbell_length, int highlight_color, int loop_quality, float dumbbell_radius, short use_cylinders_for_strands){
  int ok;
  CExtrude *ex1;
  if(highlight_color < 0) {
    ok = ExtrudeDumbbell1(ex, dumbbell_width, dumbbell_length, 0);
    if (ok)
      ExtrudeBuildNormals2f(ex);
    if (ok)
      ok &= ExtrudeCGOSurfacePolygonTaper(ex, cgo, sampling, NULL);
  } else {
    ok = ExtrudeDumbbell1(ex, dumbbell_width, dumbbell_length, 1);
    if (ok)
      ExtrudeBuildNormals2f(ex);
    if (ok)
      ok &= ExtrudeCGOSurfacePolygonTaper(ex, cgo, sampling, NULL);
    if (ok)
      ok &= ExtrudeDumbbell1(ex, dumbbell_width, dumbbell_length, 2);
    if (ok)
      ExtrudeBuildNormals2f(ex);
    if (ok)
      ok &= ExtrudeCGOSurfacePolygonTaper(ex, cgo, sampling,
                                          ColorGet(G, highlight_color));
  }
  /*
    ExtrudeCGOSurfacePolygonX(ex,cgo,1); */
  
  if (ok){
    ex1 = ExtrudeCopyPointsNormalsColors(ex);
    CHECKOK(ok, ex1);
    if (ok)
      ExtrudeDumbbellEdge(ex1, sampling, -1, dumbbell_length);
    if (ok)
      ok &= ExtrudeComputeTangents(ex1);
  }
  if (ok)
    ok &= ExtrudeCircle(ex1, loop_quality, dumbbell_radius);
  if (ok)
    ExtrudeBuildNormals1f(ex1);
  
  if (ok)
    ok &= ExtrudeCGOSurfaceTube(ex1, cgo, 1, NULL, use_cylinders_for_strands);
  if (ok){
    ExtrudeFree(ex1);
    ex1 = ExtrudeCopyPointsNormalsColors(ex);
    CHECKOK(ok, ex1);
    if (ok)
      ExtrudeDumbbellEdge(ex1, sampling, 1, dumbbell_length);
    if (ok)
      ok &= ExtrudeComputeTangents(ex1);
    if (ok)
      ok &= ExtrudeCircle(ex1, loop_quality, dumbbell_radius);
    if (ok)
      ExtrudeBuildNormals1f(ex1);
    if (ok)
      ok &= ExtrudeCGOSurfaceTube(ex1, cgo, 1, NULL, use_cylinders_for_strands);
  }
  if (ex1)
    ExtrudeFree(ex1);
  return ok;
}

/*
 * Get cartoon quality setting, adapt to number of atoms if -1
 */
static int GetCartoonQuality(CoordSet * cs, int setting, int v1, int v2, int v3, int v4, int min_=3) {
  int quality =
    SettingGet_i(cs->State.G, cs->Setting, cs->Obj->Obj.Setting, setting);

  if (quality == -1) {
    int natom = cs->NIndex;
    quality =
      (natom < 100000) ? v1 :
      (natom < 500000) ? v2 :
      (natom < 999999) ? v3 : v4;
  } else if (quality < min_) {
    quality = min_;
  }

  return quality;
}

static
CGO *GenerateRepCartoonCGO(CoordSet *cs, ObjectMolecule *obj, nuc_acid_data *ndata, short use_cylinders_for_strands,
                           bool is_picking,
                           float *pv, int nAt, float *tv, float *pvo,
                           float *dl,
                           const CCInOut *car,
                           int *seg, int *at, int *nuc_flag,
                           float *putty_vals, float alpha){
  PyMOLGlobals *G = cs->State.G;
  int ok = true;
  CGO *cgo;
  int contigFlag, contFlag, extrudeFlag, n_p;
  CExtrude *ex = NULL;
  float dev;
  int *vi;
  int atom_index1, atom_index2;
  float *v, *v1, *v2, *vo;
  float *d, *vc = NULL, *vn;
  int *atp;
  int c1, c2;
  int a;
  int sampling;
  float *sampling_tmp;
  int *segptr;
  const CCInOut *cc;
  int cur_car;
  float loop_radius;
  int nucleic_color = 0;
  float throw_;
  float power_a = 5;
  float power_b = 5;
  int refine;
  float tube_radius;
  float putty_radius;
  int cartoon_debug, cylindrical_helices, cartoon_color, highlight_color, 
    discrete_colors, loop_quality, oval_quality, tube_quality, putty_quality, loop_cap, tube_cap;
  float length, width;
  float oval_width, oval_length;
  float dumbbell_radius, dumbbell_width, dumbbell_length;
  float ring_width;
  cartoon_color =
    SettingGet_color(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_color);
  ring_width =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ring_width);
  if(ring_width < 0.0F) {
    ring_width =
      fabs(SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_stick_radius)) * 0.5F;
  }
  length = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_rect_length);
  width = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_rect_width);
  oval_length =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_oval_length);
  dumbbell_length =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_dumbbell_length);
  oval_width =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_oval_width);
  dumbbell_width =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_dumbbell_width);
  dumbbell_radius =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_dumbbell_radius);
  if(dumbbell_radius < 0.01F)
    dumbbell_radius = 0.01F;

  tube_cap = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_tube_cap);
  loop_cap = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_loop_cap);

  tube_radius =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_tube_radius);
  if(tube_radius < 0.01F)
    tube_radius = 0.01F;
  putty_radius =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_putty_radius);
  /* WLD removed: if(putty_radius<0.01F) putty_radius=0.01F; --
     should not constrain what is effectively a scale factor */

  tube_quality  = GetCartoonQuality(cs, cSetting_cartoon_tube_quality,   9, 7, 6, 5);
  oval_quality  = GetCartoonQuality(cs, cSetting_cartoon_oval_quality,  10, 8, 7, 6);
  putty_quality = GetCartoonQuality(cs, cSetting_cartoon_putty_quality, 11, 9, 7, 5);
  loop_quality  = GetCartoonQuality(cs, cSetting_cartoon_loop_quality,   6, 6, 5, 4);
  sampling      = GetCartoonQuality(cs, cSetting_cartoon_sampling,       7, 5, 3, 2, 1);

  PRINTFB(G, FB_RepCartoon, FB_Blather)
    " RepCartoon: Use settings tube_quality=%d oval_quality=%d putty_quality=%d loop_quality=%d sampling=%d\n",
    tube_quality, oval_quality, putty_quality, loop_quality, sampling
    ENDFB(G);

  if(SettingGetGlobal_i(G, cSetting_ray_trace_mode) > 0)
    if(loop_quality < 12)
      loop_quality *= 2;
  refine = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_refine);
  power_a = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_power);
  power_b = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_power_b);
  throw_ = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_throw);
  loop_radius =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_loop_radius);
  if(loop_radius < 0.01F)
    loop_radius = 0.01F;
  discrete_colors =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_discrete_colors);
  nucleic_color =
    SettingGet_color(G, cs->Setting, obj->Obj.Setting,
                     cSetting_cartoon_nucleic_acid_color);
  if(nucleic_color == -1)
    nucleic_color = cartoon_color;
  highlight_color =
    SettingGet_color(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_highlight_color);

  cylindrical_helices =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_cylindrical_helices);

  sampling_tmp = Alloc(float, sampling * 3);
  cartoon_debug = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_debug);

  cgo = CGONew(G);
  if(alpha != 1.0F)
    CGOAlpha(cgo, alpha);
  /* debugging output */
  if(cartoon_debug > 0.5 && cartoon_debug < 2.5) {
    GenerateRepCartoonDrawDebugLineAlongPath(cgo, nAt, pv);
  }
  PRINTFD(G, FB_RepCartoon)
    " RepCartoon-Debug: creating 3D scaffold...\n" ENDFD;

  /* okay, we now have enough info to generate smooth interpolations */
  if(nAt > 1) {
    ex = ExtrudeNew(G);
    CHECKOK(ok, ex);
    if (ok)
      ok &= ExtrudeAllocPointsNormalsColors(ex, cs->NIndex * (3 * sampling + 3));
  }
  /* process cylindrical helices first */
  if(ok && (nAt > 1) && cylindrical_helices) {
    ok = GenerateRepCartoonProcessCylindricalHelices(G, obj, cs, cgo, ex, nAt, seg, pv, tv,
                                                     pvo, car, at, dl, cartoon_color, discrete_colors, loop_radius);
  }
  if(ok && nAt > 1) {
    EXTRUDE_TRUNCATE();
    v1 = pv;                    /* points */
    v2 = tv;                    /* tangents */
    vo = pvo;
    d = dl;
    segptr = seg;
    cc = car;
    atp = at;                   /* cs index pointer */
    a = 0;
    contFlag = true;
    cur_car = cCartoon_skip;
    extrudeFlag = false;
    contigFlag = false;

    while(contFlag) {
      if (CheckExtrudeContigFlags(nAt, n_p, a, &cur_car, cc, segptr, &contigFlag, &extrudeFlag)){
        EXTRUDE_TRUNCATE();
      }

      if(ok && !extrudeFlag) {
        if((a < (nAt - 1)) && (*segptr == *(segptr + 1))) {       /* working in the same segment... */
          AtomInfoType *ai1, *ai2;
          atom_index1 = cs->IdxToAtm[*atp];
          atom_index2 = cs->IdxToAtm[*(atp + 1)];
          ai1 = obj->AtomInfo + atom_index1;
          ai2 = obj->AtomInfo + atom_index2;
          ComputeCartoonAtomColors(G, obj, cs, nuc_flag, atom_index1, atom_index2, &c1, &c2, atp, cc, cur_car, cartoon_color, nucleic_color, discrete_colors, n_p, contigFlag);

          dev = throw_ * (*d);
          CartoonGenerateSample(G, sampling, &n_p, dev, vo, v1, v2, c1, c2, 
                                ai1->masked ? -1 : atom_index1, ai2->masked ? -1 : atom_index2, power_a, power_b, &vc, &vi, &v, &vn);

          /* now do a smoothing pass along orientation 
             vector to smooth helices, etc... */
          CartoonGenerateRefine(refine, sampling, v, vn, vo, sampling_tmp);
        }
        v1 += 3;
        v2 += 3;
        vo += 3;
        d++;
        atp += 1;
        segptr++;
        cc++;
      }

      a++;
      if(a == nAt) {  // if at end, don't continue and extrude if needed
        contFlag = false;
        if(n_p)
          extrudeFlag = true;
      }
      if(ok && extrudeFlag) {
        contigFlag = true;
        if((a < nAt) && extrudeFlag) {
          if(*(segptr - 1) != *(segptr))
            contigFlag = false;
        }

        if(ok && (cur_car != cCartoon_skip) && (cur_car != cCartoon_skip_helix)) {
          if((cartoon_debug > 0.5) && (cartoon_debug < 2.5)) {
            ok = GenerateRepCartoonDrawDebugNormals(cgo, ex, n_p);
          }

          if (ok){
            ExtrudeTruncate(ex, n_p);
            ok &= ExtrudeComputeTangents(ex);
          }
          if (ok){
          /* set up shape */
          switch (cur_car) {
          case cCartoon_tube:
            ok = CartoonExtrudeTube(use_cylinders_for_strands, ex, cgo, tube_radius, tube_quality, tube_cap, is_picking);
            break;
          case cCartoon_putty:
            ok = CartoonExtrudePutty(G, obj, cs, cgo, ex, putty_quality, putty_radius, putty_vals, sampling);
            if (!ok)
              contFlag = false;
            break;
          case cCartoon_loop:
            ok = CartoonExtrudeCircle(ex, cgo, use_cylinders_for_strands, loop_quality, loop_radius, loop_cap);
            break;
          case cCartoon_dash:
            ok = CartoonExtrudeCircle(ex, cgo, use_cylinders_for_strands, loop_quality, loop_radius, loop_cap, 2);
            break;
          case cCartoon_rect:
            ok = CartoonExtrudeRect(G, ex, cgo, width, length, highlight_color);
            break;
          case cCartoon_oval:
            ok = CartoonExtrudeOval(G, ex, cgo, use_cylinders_for_strands, oval_quality, oval_width, oval_length, highlight_color);
            break;
          case cCartoon_arrow:
            ok = CartoonExtrudeArrow(G, ex, cgo, sampling, width, length, highlight_color);
            break;
          case cCartoon_dumbbell:
            ok = CartoonExtrudeDumbbell(G, ex, cgo, sampling, dumbbell_width, dumbbell_length, highlight_color, loop_quality, dumbbell_radius, use_cylinders_for_strands);
            break;
          }
          if (!ok)
            contFlag = false;
          }
        }
        a--;                    /* undo above... */
        extrudeFlag = false;
        if (ok){
          EXTRUDE_TRUNCATE();  // doesn't include vi = ex->i, not used?
        }
        /*
          ExtrudeTruncate(ex, 0);
        n_p = 0;
        v = ex->p;
        vc = ex->c;
        vn = ex->n;*/
      }
    }
  }

  if(ok && nAt > 1) {
    if((cartoon_debug > 0.5) && (cartoon_debug < 2.5)) {
      ok = GenerateRepCartoonDrawDebugOrient(cgo, nAt, pv, pvo, tv);
    }
  }
  if(ex) {
    ExtrudeFree(ex);
  }
  /* draw the rings */
  if(ok && ndata->ring_anchor && ndata->n_ring) {
    ok = GenerateRepCartoonDrawRings(G, ndata, obj, is_picking, cs, cgo, ring_width, cartoon_color, alpha);
  }
  if (ok)
    CGOStop(cgo);

  FreeP(sampling_tmp);

  if (!ok){
    CGOFree(cgo);
  }
  return (cgo);
}

static
int RepCartoonSameVis(RepCartoon * I, CoordSet * cs)
{
  int same = true;
  char *lv;
  int a;
  AtomInfoType *ai;

  if (!I->LastVisib)
    return false;
  ai = cs->Obj->AtomInfo;
  lv = I->LastVisib;

  for(a = 0; a < cs->NIndex; a++) {
    if(*(lv++) != GET_BIT((ai + cs->IdxToAtm[a])->visRep, cRepCartoon)) {
      same = false;
      break;
    }
  }
  return (same);
}

static
void RepCartoonInvalidate(struct Rep *I, struct CoordSet *cs, int level)
{
  if (level >= cRepInvColor){
    RepCartoon *rc = (RepCartoon*)I;
    FreeP(rc->LastVisib);
    rc->LastVisib = NULL;
  }
  RepInvalidate(I, cs, level);
}

/*
 * nucleic acid cap
 */
class nuc_acid_cap {
  PyMOLGlobals * G;
  nuc_acid_data * ndata;
  CoordSet * cs;
  int idx;

public:
  int atm;
  AtomInfoType * ai;
  bool enabled;

  // constructor
  nuc_acid_cap(PyMOLGlobals * G_, nuc_acid_data * ndata_, CoordSet * cs_, int mode)
    : G(G_), ndata(ndata_), cs(cs_), idx(0), atm(0), ai(NULL) {
      enabled = (ndata->na_mode == 4 || ndata->na_mode == mode);
  }

  // set atom indices
  void set(int idx_, int atm_, AtomInfoType * ai_) {
    idx = idx_;
    atm = atm_;
    ai = ai_;
  }

  // check if this cap is enabled and has atom information
  bool active() {
    return (ai && enabled);
  }

  // make the cap, if active, and clear atom indices
  bool cap() {
    if(!active())
      return false;

    nuc_acid(G, ndata, idx, atm, ai, cs, cs->Obj, false);
    set(-1, -1, NULL);
    return true;
  }
};

/*
 * compute and fill "ndata" with:
 * - cartoon trace and segments
 * - cartoon types and secondary structure
 * - rings
 */
static
void RepCartoonGeneratePASS1(PyMOLGlobals *G, RepCartoon *I, ObjectMolecule *obj, CoordSet * cs,
                             nuc_acid_data *ndata){
  int st, nd;
  int a, a1, a3, a4 = 0;
  char *lv = I->LastVisib;
  int trace, trace_mode;
  AtomInfoType *ai, *last_ai = NULL;
  int cartoon_side_chain_helper;
  int cylindrical_helices;
  int fancy_helices;
  int fancy_sheets;
  int parity = 1;
  float *v_c, *v_n, *v_o;
  int sc_helper;
  int cur_car;
  nuc_acid_cap leading_O5p(G, ndata, cs, 3);
  nuc_acid_cap trailing_O3p(G, ndata, cs, 2);

  // settings
  fancy_sheets =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_fancy_sheets);
  fancy_helices =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_fancy_helices);
  cylindrical_helices =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_cylindrical_helices);
  cartoon_side_chain_helper =
    SettingGet_b(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_side_chain_helper);
  int trace_ostate =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_trace_atoms);
  trace_mode =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_trace_atoms_mode);
  auto gap_cutoff =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_gap_cutoff);

  // iterate over (sorted) atoms
  for(CoordSetAtomIterator iter(cs); iter.next();) {
    ai = iter.getAtomInfo();

    // cartoon rep for this atom?
    if(!(*(lv++) = GET_BIT(ai->visRep, cRepCartoon)))
      continue;

    const char * ai_name = LexStr(G, ai->name);

    // atom indices
    a = iter.getIdx();
    a1 = iter.getAtm();

    // rings
    if(ndata->ring_anchor
        && ai->protons != cAN_H
        && (ndata->ring_finder_eff >= 3 /* all 5-7 atom rings */
          || (ndata->ring_finder_eff <= 2 /*  C4-containing rings */
            && (WordMatchExact(G, "C4", ai_name, 1)))
          || (ndata->ring_finder_eff == 1
            && (WordMatchExact(G, "C4*", ai_name, 1)
              || WordMatchExact(G, "C4'", ai_name, 1))))) {
      VLACheck(ndata->ring_anchor, int, ndata->n_ring);
      ndata->ring_anchor[ndata->n_ring] = a1;
      ndata->n_ring++;
    }

    // ignore alternative conformations
    if(ai->alt[0] && ai->alt[0] != 'A')
      continue;

    // atom level setting
    AtomInfoGetSetting_i(G, ai, cSetting_cartoon_trace_atoms, trace_ostate, &trace);

    // CA or cartoon_trace_atoms
    if(trace || (ai->protons == cAN_C
          && WordMatchExact(G, "CA", ai_name, true)
          && !AtomInfoSameResidueP(G, last_ai, ai))) {

      PRINTFD(G, FB_RepCartoon)
        " RepCartoon: found CA in %d%c; a2 %d\n", ai->resv, ai->getInscode(true), ndata->a2 ENDFD;

      // 3' nucleic acid cap
      if(trailing_O3p.cap())
        ndata->a2 = -1;

      bool is_gap = false;

      // check for gap
      if(ndata->a2 >= 0) {
        if(!trace) {
          // CA->N->C->CA = 3 bonds
          if(!ObjectMoleculeCheckBondSep(obj, a1, ndata->a2, 3))
            is_gap = true;
        } else {
          if(!AtomInfoSequential(G, obj->AtomInfo + ndata->a2, ai, trace_mode))
            is_gap = true;
        }
      }

      cur_car = ai->cartoon;

      if (is_gap) {
        int delta = ai->resv - last_ai->resv;
        if (delta < 1 || delta > gap_cutoff || !AtomInfoSameChainP(G, ai, last_ai)) {
          ndata->a2 = -1;
        } else {
          (ndata->cc - 1)->setCCOut(cCartoon_dash);
        }
      }

      last_ai = ai;

      // start new segment?
      if(ndata->a2 < 0)
        ndata->nSeg++;

      (*ndata->fp) = ai->flags;    /* store atom flags */

      // side_chain_helper
      AtomInfoGetSetting_b(G, ai, cSetting_cartoon_side_chain_helper, cartoon_side_chain_helper, &sc_helper);
      if(sc_helper) {
        if(ai->visRep & (cRepLineBit | cRepCylBit | cRepSphereBit))
          (*ndata->fp) |= cAtomFlag_no_smooth;
      }

      // secondary structure type
      switch (ai->ssType[0]) {
      case 'H':
      case 'h':
        if(cur_car == cCartoon_auto) {
          cur_car = cylindrical_helices ? cCartoon_skip_helix :
            fancy_helices ? cCartoon_dumbbell : cCartoon_oval;
        }
        (*ndata->ss) = 1;          /* helix */
        parity = 0;
        break;
      case 'S':
      case 's':
        if(cur_car == cCartoon_auto) {
          cur_car = fancy_sheets ? cCartoon_arrow : cCartoon_rect;
        }
        (*ndata->ss) = 2;         /* sheet */
        parity = !parity;
        break;
      default:           /* 'L', 'T', 0, etc. */
        if(cur_car == cCartoon_auto) {
          cur_car = cCartoon_loop;
        }
        parity = 0;
        (*ndata->ss) = 0;
        break;
      }

      if(cur_car == cCartoon_putty)
        ndata->putty_flag = true;

      // coordinates
      copy3f(cs->Coord + 3 * a, ndata->vptr);

      *((ndata->cc)++) = cur_car;
      ndata->a2 = a1;
      ndata->vptr += 3;
      ndata->ss++;
      ndata->fp++;
      *(ndata->sptr++) = ndata->nSeg;
      ndata->nAt++;
      *(ndata->iptr++) = a;

      if (trace) {
        if (a == 0 || a + 1 == cs->NIndex ||
            (a3 = cs->atmToIdx(a1 - 1)) == -1 ||
            (a4 = cs->atmToIdx(a1 + 1)) == -1) {
          zero3f(ndata->voptr);
        } else {
          float t0[3], t1[3];
          subtract3f(cs->coordPtr(a), cs->coordPtr(a3), t0);
          subtract3f(cs->coordPtr(a), cs->coordPtr(a4), t1);
          add3f(t0, t1, ndata->voptr);
          normalize3f(ndata->voptr);
        }
        ndata->voptr += 3;
        continue;
      }

      // pointers to C+N+O coordinates
      v_c = NULL;
      v_n = NULL;
      v_o = NULL;

      // get start (st) and end (nd) indices of residue atoms
      AtomInfoBracketResidueFast(G, obj->AtomInfo, obj->NAtom, a1, &st, &nd);

      // find C+N+O coordinates
      for(a3 = st; a3 <= nd; a3++) {
        a4 = cs->atmToIdx(a3);

        if (a4 == -1)
          continue;

        const char * a3name = LexStr(G, obj->AtomInfo[a3].name);

        if(WordMatchExact(G, "C", a3name, true)) {
          v_c = cs->Coord + 3 * a4;
        } else if(WordMatchExact(G, "N", a3name, true)) {
          v_n = cs->Coord + 3 * a4;
        } else if(WordMatchExact(G, "O", a3name, true)) {
          v_o = cs->Coord + 3 * a4;
        }
      }

      // orientation vector
      if(!(v_c && v_n && v_o)) {
        zero3f(ndata->voptr);
      } else {
        float t0[3], t1[3];
        subtract3f(v_n, v_c, t0); /* t0 = N<---C */
        normalize3f(t0);
        subtract3f(v_n, v_o, t1); /* t1 = N<---O */
        normalize3f(t1);
        cross_product3f(t0, t1, ndata->voptr);
        normalize3f(ndata->voptr);
        if(parity) {
          invert3f(ndata->voptr);
        }
      }
      ndata->voptr += 3;

    } else if(
        !AtomInfoSameResidueP(G, last_ai, ai)
        && (ndata->na_mode != 1 ?
          // P atom
          (ai->protons == cAN_P && WordMatchExact(G, "P", ai_name, true)) :
          // C3* C3' atom
          (ai->protons == cAN_C && (WordMatchExact(G, NUCLEIC_NORMAL1, ai_name, 1) ||
                                    WordMatchExact(G, NUCLEIC_NORMAL2, ai_name, 1))))) {

      // check for gap
      if(ndata->a2 >= 0 &&
          // six bonds between phosphates
          !ObjectMoleculeCheckBondSep(obj, a1, ndata->a2, 6)) {
        /*  3' cap */
        trailing_O3p.cap();
        ndata->a2 = -1;
      }

      last_ai = ai;
      trailing_O3p.set(-1, -1, NULL);

      /*  5' cap */
      if(leading_O5p.active()
          && ndata->a2 < 0
          && !AtomInfoSameResidueP(G, ai, leading_O5p.ai)
          && ObjectMoleculeCheckBondSep(obj, a1, leading_O5p.atm, 5)) {
        leading_O5p.cap();
      }

      leading_O5p.set(-1, -1, NULL);

      /* this is the main nucleic acid cartoon section... */
      nuc_acid(G, ndata, a, a1, ai, cs, obj, true);

    } else if(
        // P -> O3* bond
        AtomInfoSameResidueP(G, last_ai, ai)
        && ndata->a2 >= 0
        && last_ai
        && cAN_P == last_ai->protons
        && cAN_O == ai->protons
        && trailing_O3p.enabled
        && (WordMatchExact(G, "O3'", ai_name, 1) ||
            WordMatchExact(G, "O3*", ai_name, 1))
        && ObjectMoleculeCheckBondSep(obj, a1, ndata->a2, 5)) {
      // remember trailing O3*
      trailing_O3p.set(a, a1, ai);

    } else if(
        // O5* atom
        ai->protons == cAN_O
        && leading_O5p.enabled
        && (WordMatchExact(G, "O5'", ai_name, 1) ||
            WordMatchExact(G, "O5*", ai_name, 1))) {
      leading_O5p.set(a, a1, ai);
    }
  }

  /* BEGIN 3' cap */
  if(trailing_O3p.cap()) {
    ndata->a2 = -1;
  }
}

static
void RepCartoonComputePuttyValues(ObjectMolecule *obj, float *putty_vals){
  double sum = 0.0, sumsq = 0.0;
  float value;
  int cnt = 0;
  AtomInfoType *ai;
  int a;
  for(a = 0; a < obj->NAtom; a++) {
    ai = obj->AtomInfo + a;
    
    if(ai->visRep & cRepCartoonBit) {
      value = ai->b;
      sum += value;
      sumsq += (value * value);
      if(value < putty_vals[2])
        putty_vals[2] = value;
      if(value > putty_vals[3])
        putty_vals[3] = value;
      cnt++;
    }
  }
  
  if(cnt) {
    putty_vals[0] = (float) (sum / cnt);
    putty_vals[1] = (float) sqrt1d((sumsq - (sum * sum / cnt)) / (cnt));
  } else {
    /* aren't these assignments unnecessary? */
    putty_vals[0] = 10.0F;
    putty_vals[1] = 10.0F;
    putty_vals[2] = 0.0F;
    putty_vals[3] = 10.0F;
  }
}

static
void RepCartoonComputeDifferencesAndNormals(PyMOLGlobals *G, int nAt, int *seg, float *pv, float *dv, float *nv, float *dl, int quiet){
  float *v1, *v2, *vptr, *d;
  int *sptr, a;
  /* compute differences and normals */
  sptr = seg;
  vptr = pv;
  v1 = dv;
  v2 = nv;
  d = dl;
  for(a = 0; a < (nAt - 1); a++) {
    if (!quiet){
      PRINTFD(G, FB_RepCartoon)
        " RepCartoon: seg %d *s %d , *(s+1) %d\n", a, *sptr, *(sptr + 1)
        ENDFD;
    }
    if(*sptr == *(sptr + 1)) {
      subtract3f(vptr + 3, vptr, v1);
      *d = (float) length3f(v1);
      if(*d > R_SMALL4) {
        float d_1;
        d_1 = 1.0F / (*d);
        scale3f(v1, d_1, v2);
      } else if(a) {  /* if zero, copy previous */
        copy3f(v2 - 3, v2);
      } else {
        zero3f(v2);
      }
    } else {
      zero3f(v2);
    }
    d++;
    vptr += 3;
    v1 += 3;
    v2 += 3;
    sptr++;
  }
}

static
void RepCartoonComputeTangents(int nAt, int *seg, float *nv, float *tv){
  float *vptr, *v1;
  int *sptr, a;
  sptr = seg;
  vptr = nv;
  v1 = tv;
  
  *(v1++) = *(vptr++);           /* first segment */
  *(v1++) = *(vptr++);
  *(v1++) = *(vptr++);
  sptr++;
  
  for(a = 1; a < (nAt - 1); a++) {
    if((*sptr == *(sptr - 1)) && (*sptr == *(sptr + 1))) {
      add3f(vptr, (vptr - 3), v1);  /* tangent vectors are head-to-tail sums within a segment */
      normalize3f(v1);
    } else if(*sptr == *(sptr - 1)) {
      *(v1) = *(vptr - 3);       /* end a segment */
      *(v1 + 1) = *(vptr - 2);
      *(v1 + 2) = *(vptr - 1);
    } else if(*sptr == *(sptr + 1)) {
      *(v1) = *(vptr);           /* new segment */
      *(v1 + 1) = *(vptr + 1);
      *(v1 + 2) = *(vptr + 2);
    }
    vptr += 3;
    v1 += 3;
    sptr++;
  }
  *(v1++) = *(vptr - 3);         /* last segment */
  *(v1++) = *(vptr - 2);
  *(v1++) = *(vptr - 1);
}

static
void RepCartoonComputeRoundHelices(nuc_acid_data *ndata, int nAt, int *seg, int *sstype, float *tv, float *pv){
  float *v0, *v1 = NULL, *v2 = NULL, *v3 = NULL, *v4 = NULL, *v5 = NULL, *vptr;
  int last, *sptr, *ss, a;
  float t0[3], t1[3], t2[3];
  sptr = seg;
  vptr = pv;
  ss = sstype;
  v0 = tv;
  last = 0;
  if(nAt > 1) {
    for(a = 0; a < nAt; a++) {
      if(a) {
        if(*sptr != *(sptr - 1)) {        /* contiguous helices in disconnected segments */
          v1 = NULL;
          v2 = NULL;
          v3 = NULL;
          v4 = NULL;
          v5 = NULL;
          last = 0;
        }
      }
      v5 = v4;
      v4 = v3;
      v3 = v2;
      v2 = v1;
      if(*ss == 1)          /* helix */
        v1 = vptr;
      else {                /* early termination ? */
        if(last < 2) {
          zero3f(t0);
          if(v2 && v3) {
            subtract3f(v2, vptr, t0);
            normalize3f(t0);
            subtract3f(v3, v2, t1);
            normalize3f(t1);
            add3f(t1, t0, t0);
            if(v4) {
              subtract3f(v4, v3, t1);
              normalize3f(t1);
              add3f(t1, t0, t0);
            }
            if(v5) {
              subtract3f(v5, v4, t1);
              normalize3f(t1);
              add3f(t1, t0, t0);
            }
            normalize3f(t0);
            cross_product3f(t0, v0 - 3, ndata->voptr - 3);
            normalize3f(ndata->voptr - 3);
            cross_product3f(t0, v0 - 6, ndata->voptr - 6);
            normalize3f(ndata->voptr - 6);
            if(v4) {
              cross_product3f(t0, v0 - 9, ndata->voptr - 9);
              normalize3f(ndata->voptr - 9);
            }
            if(v5) {
              cross_product3f(t0, v0 - 12, ndata->voptr - 12);
              normalize3f(ndata->voptr - 12);
            }
            if(v4 && v5) {
              /* now make sure there's no goofy flip on the end...
                 of a short, tight helix */
              if(dot_product3f(ndata->voptr - 9, ndata->voptr - 12) < -0.8F)
                invert3f(ndata->voptr - 12);
            }
          }
        }
        v1 = NULL;
        v2 = NULL;
        v3 = NULL;
        v4 = NULL;
        v5 = NULL;
        last = 0;
      }
      if(v1 && v2 && v3 && v4) {
        add3f(v1, v4, t0);
        add3f(v2, v3, t1);
        scale3f(t0, 0.2130F, t0);
        scale3f(t1, 0.2870F, t1);
        
        add3f(t0, t1, t0);
        if(last) {          /* 5th CA or later... */
          subtract3f(t2, t0, t1);
          normalize3f(t1);
          cross_product3f(t1, v0, ndata->voptr);
          normalize3f(ndata->voptr);
          cross_product3f(t1, v0 - 3, ndata->voptr - 3);
          normalize3f(ndata->voptr - 3);
          cross_product3f(t1, v0 - 6, ndata->voptr - 6);
          normalize3f(ndata->voptr - 6);
          if(last == 1) {   /* 5th */
            cross_product3f(t1, v0 - 9, ndata->voptr - 9);
            normalize3f(ndata->voptr - 9);
            cross_product3f(t1, v0 - 12, ndata->voptr - 12);
            normalize3f(ndata->voptr - 12);
          }
        }
        last++;
        copy3f(t0, t2);
      }
      vptr += 3;
      ss++;
      ndata->voptr += 3;
      v0 += 3;
      sptr++;
    }
  }
}

static
void RepCartoonRefineNormals(PyMOLGlobals *G, RepCartoon *I, ObjectMolecule *obj, CoordSet * cs,
                             nuc_acid_data *ndata, int nAt, int *seg, float *tv, float *pvo, 
                             float *pva, int *sstype, float *nv){
  int refine_normals =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_refine_normals);
  if(refine_normals < 0) {
    if(obj->NCSet > 1) {
      int i, n_set = 0;
      for(i = 0; i < obj->NCSet; i++)
        if(obj->CSet[i]) {
          n_set++;
          if(n_set > 1)
            refine_normals = 0;
          /* default behavior is to not refine normals for multi-state objects */
        }
    }
  }
  
  if(refine_normals) {
    /* first, make sure orientiation vectors are orthogonal to the tangent */
    float *v0, *v1, *vptr, *va, max_dot;
    int *sptr, a, *ss;
    float t0[3], t1[3], t2[3], t3[3], o0[12], o1[12];
    float dp;
    v1 = tv + 3;
    ndata->voptr = pvo + 3;
    sptr = seg + 1;
    for(a = 1; a < (nAt - 1); a++) {
      if((*sptr == *(sptr - 1)) && (*sptr == *(sptr + 1))) {
        /* only operate on vectors within the cartoon itself --
           not the end vectors */
            remove_component3f(ndata->voptr, v1, t0);
            normalize23f(t0, ndata->voptr);
            /* go on to next vertex */
      }
      v1 += 3;
      ndata->voptr += 3;
      sptr++;
    }
    /* now generate alternative inverted orientation vectors */
    va = pva;
    ndata->voptr = pvo;
    ss = sstype;
    for(a = 0; a < nAt; a++) {
      /* original */
      copy3f(ndata->voptr, va);
      va += 3;
      /* inverse */
      copy3f(ndata->voptr, va);
      if(*ss != 1) {
        invert3f(va);
        /* for helix, don't allow inversion of normals, since that
           would confuse the inside & outside of the helix  */
      }
      va += 3;
      /* go on to next vertex */
      ndata->voptr += 3;
      ss++;
    }
    /* now iterate forward through pairs */
    ndata->voptr = pvo + 3;
    va = pva + 6;
    vptr = nv + 3;             /* normals in direction of chain */
    sptr = seg + 1;
    for(a = 1; a < (nAt - 1); a++) {
      if((*sptr == *(sptr + 1)) && (*sptr == *(sptr - 1))) {    /* only operate within a segment */
        remove_component3f(ndata->voptr - 3, vptr - 3, o0);      /* previous orientation vector */
        normalize3f(o0);    /* is now perp to chain direction */
        v1 = va;            /* candidate orientation vectors for current CA */
        remove_component3f(v1, vptr - 3, o1);  /* removes chain direction from the two candidates */
        remove_component3f(v1 + 3, vptr - 3, o1 + 3);
        normalize3f(o1);
        normalize3f(o1 + 3);
        max_dot = dot_product3f(o0, o1);
        v0 = v1;
        dp = dot_product3f(o0, o1 + 3);
        if(dp > max_dot) {
          v0 = v1 + 3;
          max_dot = dp;
        }
        copy3f(v0, ndata->voptr);     /* updates atom with optimal orientation vector */
      }
      ndata->voptr += 3;
      va += 6;              /* candidate orientation vectors */
      vptr += 3;               /* normal */
      sptr++;
    }
    /* now soften up the kinks */
    v1 = tv + 3;
    va = pva + 6;
    ndata->voptr = pvo + 3;
    sptr = seg + 1;
    for(a = 1; a < (nAt - 1); a++) {
      if((*sptr == *(sptr - 1)) && (*sptr == *(sptr + 1))) {
        dp = (dot_product3f(ndata->voptr, ndata->voptr + 3) * dot_product3f(ndata->voptr, ndata->voptr - 3));
        if(dp < -0.10F) {   /* threshold value -- could be a setting */
          add3f(ndata->voptr + 3, ndata->voptr - 3, t0);
          scale3f(ndata->voptr, 0.001, t1);
          add3f(t1, t0, t0);
          remove_component3f(t0, v1, t0);
          normalize3f(t0);
          if(dot_product3f(ndata->voptr, t0) < 0.0F) {
            subtract3f(ndata->voptr, t0, t2);
          } else {
            add3f(ndata->voptr, t0, t2);
          }
          normalize3f(t2);
          dp = 2 * (-0.10F - dp);
          if(dp > 1.0F)
            dp = 1.0F;
          mix3f(ndata->voptr, t2, dp, t3);
          copy3f(t3, va);   /* store modified vector */
          invert3f3f(va, va + 3);
        } else {
          copy3f(ndata->voptr, va);   /* keep as is */
        }
      }
      v1 += 3;
      ndata->voptr += 3;
      va += 6;
      sptr++;
    }
    /* now update */
    va = pva + 6;
    ndata->voptr = pvo + 3;
    sptr = seg + 1;
    for(a = 1; a < (nAt - 1); a++) {
      if((*sptr == *(sptr - 1)) && (*sptr == *(sptr + 1))) {
        copy3f(va, ndata->voptr);
      }
      ndata->voptr += 3;
      va += 6;
      sptr++;
    }
  }
}

static
void RepCartoonFlattenSheets(PyMOLGlobals *G, ObjectMolecule *obj, CoordSet * cs, nuc_acid_data *ndata,
                             int nAt, int *seg,
                             const CCInOut * cc,
                             float *pv,
                             float *pvo, int *sstype, float *tv, float *tmp, int *flag_tmp){
  int *sptr, *ss, last, first, cur_car, end_flag, a, b, c, e, f;
  float *vptr, *v0;
  int flat_cycles;
  float t0[3];
  flat_cycles =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_flat_cycles);

  sptr = seg;
  vptr = pv;
  ss = sstype;
  ndata->voptr = pvo;
  v0 = tv;
  last = 0;
  first = -1;
  cur_car = *cc;
  end_flag = false;
  if(nAt > 1) {
    for(a = 0; a < nAt; a++) {
      if(a) {
        if(*sptr != *(sptr - 1)) {
          end_flag = true;
        } else if(*ss != 2) {
          end_flag = true;
        }
        if(a == (nAt - 1)) {
          end_flag = 1;
        }
      }
      if(end_flag) {
        if((cur_car != cCartoon_loop) && (cur_car != cCartoon_tube)) {
          f = 1;
          for(c = 0; c < flat_cycles; c++) {
            for(b = first + f; b <= last - f; b++) {        /* iterative averaging */
              zero3f(t0);
              for(e = -f; e <= f; e++) {
                add3f(pv + 3 * (b + e), t0, t0);
              }
              scale3f(t0, 1.0F / (f * 2 + 1), tmp + b * 3);
            }
            for(b = first + f; b <= last - f; b++) {
              if(!((*(flag_tmp + b) & cAtomFlag_no_smooth))) {
                copy3f(tmp + b * 3, pv + b * 3);
              }
            }
            for(b = first + f; b <= last - f; b++) {
              zero3f(t0);
              for(e = -f; e <= f; e++) {
                add3f(pvo + 3 * (b + e), t0, t0);
              }
              scale3f(t0, 1.0F / (f * 2 + 1), tmp + b * 3);
            }
            for(b = first + f; b <= last - f; b++) {
              copy3f(tmp + b * 3, pvo + b * 3);
              /*                  normalize3f(pvo+b*3); */
            }
            for(b = first + f; b <= last - f; b++) {
              subtract3f(pv + (b + 1) * 3, pv + (b - 1) * 3, tmp + b * 3);
              normalize3f(tmp + b * 3);
              remove_component3f(pvo + b * 3, tmp + b * 3, pvo + b * 3);
              normalize3f(pvo + b * 3);
            }
          }
        }
        first = -1;
        last = -1;
        end_flag = false;
      }
      if(*ss == 2) {
        if(first < 0)
          first = a;
        cur_car = *cc;
        last = a;
      }
      vptr += 3;
      ss++;
      ndata->voptr += 3;
      v0 += 3;
      sptr++;
      cc++;
    }
  }
}

static
void RepCartoonFlattenSheetsRefineTips(PyMOLGlobals *G, ObjectMolecule *obj, CoordSet * cs,
                                       int nAt, int *seg, int *sstype, float *tv){
  int *sptr, *ss, a;
  float *v2;
  float refine_tips;
  float t0[3];
  refine_tips =
    SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_refine_tips);
  sptr = seg + 1;
  ss = sstype + 1;
  v2 = tv + 3;            /* normal */
  for(a = 1; a < (nAt - 1); a++) {
    if((*ss == 2) && (*sptr == *(sptr + 1)) && (*sptr == *(sptr - 1))) {      /* sheet in same segment */
      if((*ss == *(ss + 1)) && (*ss != *(ss - 1))) {      /* start, bias forwards */
        scale3f(v2 + 3, refine_tips, t0);
        add3f(t0, v2, v2);
        normalize3f(v2);
      } else if((*ss != *(ss + 1)) && (*ss == *(ss - 1))) {       /* end, bias backwards */
        scale3f(v2 - 3, refine_tips, t0);
        add3f(t0, v2, v2);
        normalize3f(v2);
      }
    }
    v2 += 3;
    sptr++;
    ss++;
  }
}

static
void RepCartoonSmoothLoops(PyMOLGlobals *G, ObjectMolecule *obj, CoordSet * cs, nuc_acid_data *ndata, int nAt, int *seg, float *pv, int *sstype, float *pvo, float *tv, float *tmp, int *flag_tmp){
  int *sptr, *ss, last, first, end_flag, a, b, c, e, f;
  float *vptr, *v0;
  float t0[3];
  int smooth_first, smooth_last, smooth_cycles;
  smooth_first =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_smooth_first);
  smooth_last =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_smooth_last);
  smooth_cycles =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_smooth_cycles);

  sptr = seg;
  vptr = pv;
  ss = sstype;
  ndata->voptr = pvo;
  v0 = tv;
  last = 0;
  first = -1;
  end_flag = false;
  if(nAt > 1) {
    for(a = 0; a < nAt; a++) {
      if(a) {
        if(*sptr != *(sptr - 1)) {
          end_flag = true;
        } else if(*ss != 0) {
          end_flag = true;
        }
        if(a == (nAt - 1))
          end_flag = 1;
      }
      if(end_flag) {
        
        if(a)
          if(first > 0)     /* 011130 WLD */
            if(*(seg + first) == *(seg + first - 1))
              first--;
        
        if(last > 0)
          if(*sptr == *(sptr - 1))
            if(last < (nAt - 1))
              last++;
        
        for(f = smooth_first; f <= smooth_last; f++) {
          for(c = 0; c < smooth_cycles; c++) {
            for(b = first + f; b <= last - f; b++) {        /* iterative averaging */
              zero3f(t0);
              for(e = -f; e <= f; e++) {
                add3f(pv + 3 * (b + e), t0, t0);
              }
              scale3f(t0, 1.0F / (f * 2 + 1), tmp + b * 3);
            }
            for(b = first + f; b <= last - f; b++) {
              if(!(*(flag_tmp + b) & cAtomFlag_no_smooth)) {
                copy3f(tmp + b * 3, pv + b * 3);
              }
            }
            for(b = first + f; b <= last - f; b++) {
              zero3f(t0);
              for(e = -f; e <= f; e++) {
                add3f(pvo + 3 * (b + e), t0, t0);
              }
              scale3f(t0, 1.0F / (f * 2 + 1), tmp + b * 3);
            }
            for(b = first + f; b <= last - f; b++) {
              copy3f(tmp + b * 3, pvo + b * 3);
              normalize3f(pvo + b * 3);
            }
          }
        }
        first = -1;
        last = -1;
        end_flag = false;
      }
      if(*ss == 0) {
        if(first < 0)
          first = a;
        last = a;
      }
      vptr += 3;
      ss++;
      ndata->voptr += 3;
      v0 += 3;
      sptr++;
    }
  }
}

Rep *RepCartoonNew(CoordSet * cs, int state)
{
  PyMOLGlobals *G = cs->State.G;
  ObjectMolecule *obj;
  int *i, *sptr, *at, *seg, nAt, *sstype;
  CCInOut *car, *cc;
  float *pv = NULL;
  float *pvo = NULL, *pva = NULL;
  float *dv = NULL;
  float *nv = NULL;
  float *tv = NULL;
  float *tmp = NULL;
  float *dl = NULL;
  int *ss;

  int ladder_mode;
  int round_helices;
  int na_mode;
  int *flag_tmp;
  float putty_vals[4] = { 10.0F, 0.0F, FLT_MAX, -FLT_MAX }; // putty_mean, putty_stdev, putty_min, putty_max
  int *ring_anchor = NULL;
  int *nuc_flag = NULL;
  float alpha;
  int ok = true;
  nuc_acid_data ndata;
  short use_shaders = SettingGetGlobal_b(G, cSetting_use_shaders);
  short na_strands_as_cylinders = use_shaders && 
    (SettingGetGlobal_i(G, cSetting_cartoon_nucleic_acid_as_cylinders) & 2) && 
    SettingGetGlobal_b(G, cSetting_render_as_cylinders);

  // skip if not visible
  if(!cs->hasRep(cRepCartoonBit))
    return NULL;

  /* THIS IS BY FAR THE WORST ROUTINE IN PYMOL!
   * DEVELOP ON IT ONLY AT EXTREME RISK TO YOUR MENTAL HEALTH */

  OOCalloc(G, RepCartoon);

  PRINTFD(G, FB_RepCartoon)
    " RepCartoonNew-Debug: entered.\n" ENDFD;

  obj = cs->Obj;

  RepInit(G, &I->R);

  alpha =
    1.0F - SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_transparency);
  round_helices =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_round_helices);
  na_mode =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_nucleic_acid_mode);
  ladder_mode =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ladder_mode);

  I->R.fRender = (void (*)(struct Rep *, RenderInfo *)) RepCartoonRender;
  I->R.fSameVis = (int (*)(struct Rep *, struct CoordSet *)) RepCartoonSameVis;
  I->R.fFree = (void (*)(struct Rep *)) RepCartoonFree;
  I->R.fInvalidate = RepCartoonInvalidate;
  I->R.fRecolor = NULL;
  I->R.obj = &obj->Obj;
  I->R.cs = cs;
  I->ray = NULL;
  I->std = NULL;
  I->preshader = NULL;
  I->pickingCGO = NULL;
  I->R.context.object = (void *) obj;
  I->R.context.state = state;

  /* find all of the CA points */

  at = Alloc(int, cs->NAtIndex);        /* cs index pointers */
  pv = Alloc(float, cs->NAtIndex * 3);
  tmp = Alloc(float, cs->NAtIndex * 3);
  pvo = Alloc(float, cs->NAtIndex * 3); /* orientation vector */
  pva = Alloc(float, cs->NAtIndex * 6); /* alternative orientation vectors, two per atom */
  seg = Alloc(int, cs->NAtIndex);
  car = Calloc(CCInOut, cs->NAtIndex);       /* cartoon type for each atom */
  sstype = Alloc(int, cs->NAtIndex);
  flag_tmp = Calloc(int, cs->NAtIndex);
  nuc_flag = Calloc(int, cs->NAtIndex);

  I->LastVisib = Calloc(char, cs->NAtIndex);
  
  i = at;
  sptr = seg;
  cc = car;
  ss = sstype;
  nAt = 0;

  ndata.na_mode = na_mode;
  ndata.nuc_flag = nuc_flag;
  ndata.a2 = -1;
  ndata.nSeg = 0;
  ndata.v_o_last = NULL;
  ndata.sptr = sptr;
  ndata.iptr = i;
  ndata.cc = cc;
  ndata.nAt = nAt;
  ndata.ss = ss;
  ndata.putty_flag = false;
  ndata.fp = flag_tmp;
  ndata.vptr = pv;
  ndata.voptr = pvo;
  ndata.ring_mode = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ring_mode);
  ndata.ring_finder =
    SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_ring_finder);
  ndata.ring_finder_eff = ndata.ring_finder;
  if((!ndata.ring_mode) || (ndata.ring_finder == 2))
    ndata.ring_finder_eff = 1;
  if(ndata.ring_mode || ladder_mode) {
    ring_anchor = VLAlloc(int, cs->NAtIndex / 10 + 1);
  }
  ndata.ring_anchor = ring_anchor;
  ndata.n_ring = 0;

  RepCartoonGeneratePASS1(G, I, obj, cs, &ndata);
  nAt = ndata.nAt;
  if(nAt && ndata.putty_flag) {
    RepCartoonComputePuttyValues(obj, putty_vals);
  }

  PRINTFD(G, FB_RepCartoon)
    " RepCartoon-Debug: path outlined, interpolating... nAt=%d\n", nAt ENDFD;

  if(nAt) {
    dv = Alloc(float, nAt * 3);  /* differences between next and current 3f */
    nv = Alloc(float, nAt * 3);  /* normal */
    dl = Alloc(float, nAt);      /* length (i.e., normal * length = difference) */
    RepCartoonComputeDifferencesAndNormals(G, nAt, seg, pv, dv, nv, dl, true);

    /* compute tangents */
    tv = Alloc(float, nAt * 3 + 6);
    RepCartoonComputeTangents(nAt, seg, nv, tv);

    PRINTFD(G, FB_RepCartoon)
      " RepCartoon-Debug: generating coordinate systems...\n" ENDFD;

    if(round_helices) {
      ndata.voptr = pvo;
      RepCartoonComputeRoundHelices(&ndata, nAt, seg, sstype, tv, pv);
    }

    RepCartoonRefineNormals(G, I, obj, cs, &ndata, nAt, seg, tv, pvo, pva, sstype, nv);

    {
      int smooth_loops = SettingGet_i(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_smooth_loops);
      int cartoon_flat_sheets = SettingGet_f(G, cs->Setting, obj->Obj.Setting, cSetting_cartoon_flat_sheets);
      if(smooth_loops || cartoon_flat_sheets) {
        if(cartoon_flat_sheets) {
          RepCartoonFlattenSheets(G, obj, cs, &ndata, nAt, seg, car, pv, pvo, sstype, tv, tmp, flag_tmp);
        }
        if(smooth_loops) {
          RepCartoonSmoothLoops(G, obj, cs, &ndata, nAt, seg, pv, sstype, pvo, tv, tmp, flag_tmp);
        }
        /* recompute differences and normals */
        RepCartoonComputeDifferencesAndNormals(G, nAt, seg, pv, dv, nv, dl, true);
        /* recompute tangents */
        RepCartoonComputeTangents(nAt, seg, nv, tv);
        if(cartoon_flat_sheets) {
          RepCartoonFlattenSheetsRefineTips(G, obj, cs, nAt, seg, sstype, tv);
        }
      }
    }
  }

  I->ray = GenerateRepCartoonCGO(cs, obj, &ndata, na_strands_as_cylinders, false, pv, nAt, tv, pvo, dl, car, seg, at, nuc_flag,
                                 putty_vals, alpha);

  if (I->ray && I->ray->has_begin_end){
    CGOCombineBeginEnd(&I->ray);
  }

  I->preshader = I->ray;

  CHECKOK(ok, I->ray);

  if (ok){
    int has_picking_cgo = 0;
    CGO *convertcgo = GenerateRepCartoonCGO(cs, obj, &ndata, false, true, pv, nAt, tv, pvo, dl, car, seg, at, nuc_flag,
                                            putty_vals, alpha);
    if (convertcgo){
      I->pickingCGO = CGOSimplify(convertcgo, 0);
      if (I->pickingCGO){
	CGOFree(convertcgo);
	convertcgo = CGOCombineBeginEnd(I->pickingCGO, 0);
	if (convertcgo){
	  CGOFree(I->pickingCGO);
	  I->pickingCGO = convertcgo;
	  has_picking_cgo = true;
	} else {
	  CGOFree(I->pickingCGO);
	  I->pickingCGO = NULL;
	}
      } else {
	CGOFree(convertcgo);
	convertcgo = NULL;
      }
    }
    ok &= has_picking_cgo;  /* If picking object can't load, remove object */
  }

  ok &= !G->Interrupt;
  if (!ok){
    /* cannot generate RepCartoon */
    RepCartoonFree(I);
    I = NULL;
  }
  FreeP(dv);
  FreeP(dl);
  FreeP(tv);
  FreeP(nv);
  FreeP(at);
  FreeP(seg);
  FreeP(pv);
  FreeP(pvo);
  FreeP(pva);
  FreeP(car);
  FreeP(tmp);
  FreeP(sstype);
  FreeP(flag_tmp);
  FreeP(nuc_flag);
  VLAFreeP(ndata.ring_anchor);
  return (Rep *) I;
}
