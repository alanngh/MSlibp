#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <mpi.h>

#include "mesh2D.h"

extern "C"
{
  void *gsParallelGatherScatterSetup(int Ngather, int *gatherIds);
  void gsParallelGatherScatter(void *gsh, void *v, const char *type, const char *op);
  void gsParallelGatherScatterDestroy(void *gsh);
}

// assume nodes locally sorted by rank then global index
// assume gather and scatter are the same sets
ogs_t *meshParallelGatherScatterSetup2D(mesh2D *mesh,    // provides DEVICE
					iint Nlocal,     // number of local nodes
					iint Nbytes,     // number of bytes per node
					iint *gatherLocalIds,  // local index of nodes
					iint *gatherBaseIds,   // global index of their base nodes
					iint *gatherHaloFlags){   // 1 for halo node, 0 for not

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  ogs_t *ogs = (ogs_t*) calloc(1, sizeof(ogs_t));

  // ------------------------------------------------------------
  // 0. propagate halo flags uniformly using a disposable gs instance

  void *allGsh = gsParallelGatherScatterSetup(Nlocal, gatherBaseIds);

  // compute max halo flag using global numbering
  gsParallelGatherScatter(allGsh, gatherHaloFlags, "int", "max"); // should use iint

  // tidy up
  gsParallelGatherScatterDestroy(allGsh);
  
  // ------------------------------------------------------------
  // 1. count number of unique base nodes on this process 

  ogs->Ngather = 0; // assumes at least one base node
  for(iint n=0;n<Nlocal;++n){
    iint test = (n==0) ? 1: (gatherBaseIds[n] != gatherBaseIds[n-1]);
    ogs->Ngather += test;
  }
  
  iint *gatherOffsets = (iint*) calloc(ogs->Ngather+1, sizeof(iint)); // offset into sorted list of nodes

  // only finds bases
  ogs->Ngather = 0; // reset counter
  for(iint n=0;n<Nlocal;++n){
    iint test = (n==0) ? 1: (gatherBaseIds[n] != gatherBaseIds[n-1]);
    if(test){
      gatherOffsets[ogs->Ngather++] = n;  // increment unique base counter and record index into shuffled list of ndoes
    }
  }
  gatherOffsets[ogs->Ngather] = Nlocal;

  char *gatherTmp = (char*) calloc(ogs->Ngather*Nbytes, sizeof(char));
  
  // allocate buffers on DEVICE
  ogs->o_gatherTmp      = mesh->device.malloc(ogs->Ngather*Nbytes, gatherTmp);
  ogs->o_gatherOffsets  = mesh->device.malloc((ogs->Ngather+1)*sizeof(iint), gatherOffsets);
  ogs->o_gatherLocalIds = mesh->device.malloc(Nlocal*sizeof(iint), gatherLocalIds);
  
  ogs->gatherLocalIds  = gatherLocalIds;
  ogs->gatherBaseIds   = gatherBaseIds;
  ogs->gatherHaloFlags = gatherHaloFlags;

  // list of nodes to extract from DEVICE gathered array
  ogs->Nhalo = 0; 
  for(iint n=0;n<ogs->Ngather;++n){ // could be this? 
    for(iint id=gatherOffsets[n];id<gatherOffsets[n+1];++id){
      if(gatherHaloFlags[id]){
	// if any of these are labelled as halo then mark
	++ogs->Nhalo;
	break;
      }
    }
  }
  
  // set up gather-scatter of halo nodes
  ogs->gatherGsh = NULL;
  if(ogs->Nhalo){
    
    iint *haloLocalIds  = (iint*) calloc(ogs->Nhalo, sizeof(iint));
    iint *haloGlobalIds = (iint*) calloc(ogs->Nhalo, sizeof(iint));

    ogs->Nhalo = 0;
    for(iint n=0;n<ogs->Ngather;++n){
      for(iint id=gatherOffsets[n];id<gatherOffsets[n+1];++id){
	if(gatherHaloFlags[id]){
	  haloLocalIds[ogs->Nhalo] = n;
	  haloGlobalIds[ogs->Nhalo] = gatherBaseIds[id];
	  ++ogs->Nhalo;
	  break;
	}
      }
    }     
    
    // list of halo node indices
    ogs->o_haloLocalIds = mesh->device.malloc(ogs->Nhalo*sizeof(iint), haloLocalIds);
    
    // allocate buffer for gathering on halo nodes (danger on size of buffer)
    ogs->haloTmp = (dfloat*) calloc(ogs->Nhalo, sizeof(dfloat));
    ogs->o_haloTmp  = mesh->device.malloc(ogs->Nhalo*Nbytes, ogs->haloTmp);
    
    // initiate gslib gather-scatter comm pattern
    ogs->gatherGsh = gsParallelGatherScatterSetup(ogs->Nhalo, haloGlobalIds);
    
    free(haloGlobalIds);
    free(haloLocalIds);
  }
  
  free(gatherOffsets);

  return ogs;
}
