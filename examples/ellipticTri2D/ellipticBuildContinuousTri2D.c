#include "ellipticTri2D.h"

typedef struct{

  iint row;
  iint col;
  iint ownerRank;
  dfloat val;

} nonZero_t;

int parallelCompareRowColumn(const void *a, const void *b);

void ellipticBuildContinuousTri2D(mesh2D *mesh, dfloat lambda, nonZero_t **A, iint *nnz, hgs_t **hgs, iint *globalStarts, const char* options) {  
  
  iint rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // ------------------------------------------------------------------------------------
  // 1. Create a contiguous numbering system, starting from the element-vertex connectivity
  iint Nnum = mesh->Np*mesh->Nelements;
  iint *globalOwners = (iint*) calloc(Nnum, sizeof(iint));
  iint *globalNumbering = (iint*) calloc(Nnum, sizeof(iint));

  for (iint n=0;n<Nnum;n++) {
    iint id = mesh->gatherLocalIds[n]; 
    globalNumbering[id] = mesh->gatherBaseIds[n];
  }

  // squeeze node numbering
  meshParallelConsecutiveGlobalNumbering(Nnum, globalNumbering, globalOwners, globalStarts);
  
  //use the ordering to define a gather+scatter for assembly
  *hgs = meshParallelGatherSetup(mesh, Nnum, globalNumbering, globalOwners);

  // 2. Build non-zeros of stiffness matrix (unassembled)
  iint nnzLocal = mesh->Np*mesh->Np*mesh->Nelements;
  iint   *rows = (iint*) calloc(nnzLocal, sizeof(iint));
  iint   *cols = (iint*) calloc(nnzLocal, sizeof(iint));
  dfloat *vals = (dfloat*) calloc(nnzLocal, sizeof(dfloat));

  nonZero_t *sendNonZeros = (nonZero_t*) calloc(nnzLocal, sizeof(nonZero_t));
  iint *AsendCounts  = (iint*) calloc(size, sizeof(iint));
  iint *ArecvCounts  = (iint*) calloc(size, sizeof(iint));
  iint *AsendOffsets = (iint*) calloc(size+1, sizeof(iint));
  iint *ArecvOffsets = (iint*) calloc(size+1, sizeof(iint));

  //Build unassembed non-zeros
  printf("Building full matrix system\n");
  iint cnt =0;
  for (iint e=0;e<mesh->Nelements;e++) {
    for (iint n=0;n<mesh->Np;n++) {
      for (iint m=0;m<mesh->Np;m++) {
        dfloat val = 0.;

        dfloat Grr = mesh->ggeo[e*mesh->Nggeo + G00ID];
        dfloat Grs = mesh->ggeo[e*mesh->Nggeo + G01ID];
        dfloat Gss = mesh->ggeo[e*mesh->Nggeo + G11ID];
        dfloat J   = mesh->ggeo[e*mesh->Nggeo + GWJID];

        val += Grr*mesh->Srr[m+n*mesh->Np];
        val += Grs*mesh->Srs[m+n*mesh->Np];
        val += Grs*mesh->Ssr[m+n*mesh->Np];
        val += Gss*mesh->Sss[m+n*mesh->Np];
        val += J*lambda*mesh->MM[m+n*mesh->Np];

        dfloat nonZeroThreshold = 1e-7;
        if (fabs(val)>nonZeroThreshold) {
          vals[cnt] = val;
          rows[cnt] = e*mesh->Np + n;
          cols[cnt] = e*mesh->Np + m;

          // pack non-zero
          sendNonZeros[cnt].val = val;
          sendNonZeros[cnt].row = globalNumbering[e*mesh->Np + n];
          sendNonZeros[cnt].col = globalNumbering[e*mesh->Np + m];
          sendNonZeros[cnt].ownerRank = globalOwners[e*mesh->Np + n];
          cnt++;
        }
      }
    }
  }

  // count how many non-zeros to send to each process
  for(iint n=0;n<cnt;++n)
    AsendCounts[sendNonZeros[n].ownerRank] += sizeof(nonZero_t);

  // sort by row ordering
  qsort(sendNonZeros, cnt, sizeof(nonZero_t), parallelCompareRowColumn);
  
  // find how many nodes to expect (should use sparse version)
  MPI_Alltoall(AsendCounts, 1, MPI_IINT, ArecvCounts, 1, MPI_IINT, MPI_COMM_WORLD);

  // find send and recv offsets for gather
  *nnz = 0;
  for(iint r=0;r<size;++r){
    AsendOffsets[r+1] = AsendOffsets[r] + AsendCounts[r];
    ArecvOffsets[r+1] = ArecvOffsets[r] + ArecvCounts[r];
    *nnz += ArecvCounts[r]/sizeof(nonZero_t);
  }

  *A = (nonZero_t*) calloc(*nnz, sizeof(nonZero_t));
  
  // determine number to receive
  MPI_Alltoallv(sendNonZeros, AsendCounts, AsendOffsets, MPI_CHAR,
    (*A), ArecvCounts, ArecvOffsets, MPI_CHAR,
    MPI_COMM_WORLD);

  // sort received non-zero entries by row block (may need to switch compareRowColumn tests)
  qsort((*A), *nnz, sizeof(nonZero_t), parallelCompareRowColumn);

  // compress duplicates
  cnt = 0;
  for(iint n=1;n<*nnz;++n){
    if((*A)[n].row == (*A)[cnt].row &&
       (*A)[n].col == (*A)[cnt].col){
      (*A)[cnt].val += (*A)[n].val;
    }
    else{
      ++cnt;
      (*A)[cnt] = (*A)[n];
    }
  }
  *nnz = cnt+1;

  free(globalNumbering); free(globalOwners);
  free(rows); free(cols); free(vals);
  free(sendNonZeros);

  free(sendNonZeros);
  free(AsendCounts);
  free(ArecvCounts);
  free(AsendOffsets);
  free(ArecvOffsets);
}