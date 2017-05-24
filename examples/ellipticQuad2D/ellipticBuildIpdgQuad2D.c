
#include "ellipticQuad2D.h"

typedef struct{

  iint row;
  iint col;
  iint ownerRank;
  dfloat val;

} nonZero_t;

void ellipticBuildIpdgQuad2D(mesh2D *mesh, dfloat lambda, nonZero_t **A, iint *nnzA, const char *options){

  iint size, rankM;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rankM);

  /* find number of elements on each rank */
  iint *rankNelements = (iint*) calloc(size, sizeof(iint));
  iint *rankStarts = (iint*) calloc(size+1, sizeof(iint));
  MPI_Allgather(&(mesh->Nelements), 1, MPI_IINT,
    rankNelements, 1, MPI_IINT, MPI_COMM_WORLD);
  for(iint r=0;r<size;++r){
    rankStarts[r+1] = rankStarts[r]+(rankNelements[r])*mesh->Np;
  }
  
  /* do a halo exchange of local node numbers */

  iint nnzLocalBound = mesh->Np*mesh->Np*(1+mesh->Nfaces)*mesh->Nelements;

  *A = (nonZero_t*) calloc(nnzLocalBound, sizeof(nonZero_t));

  // drop tolerance for entries in sparse storage
  dfloat tol = 1e-8;
  dfloat tau = 2; // hackery

  // build some monolithic basis arrays (use Dr,Ds,Dt and insert MM instead of weights for tet version)
  dfloat *B  = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  dfloat *Br = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  dfloat *Bs = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));

  iint mode = 0;
  for(iint nj=0;nj<mesh->N+1;++nj){
    for(iint ni=0;ni<mesh->N+1;++ni){

      iint node = 0;

      for(iint j=0;j<mesh->N+1;++j){
        for(iint i=0;i<mesh->N+1;++i){

          if(nj==j && ni==i)
            B[mode*mesh->Np+node] = 1;
          if(nj==j)
            Br[mode*mesh->Np+node] = mesh->D[ni+mesh->Nq*i]; 
          if(ni==i)
            Bs[mode*mesh->Np+node] = mesh->D[nj+mesh->Nq*j]; 
          
          ++node;
        }
      }
      
      ++mode;
    }
  }
  
  // reset non-zero counter
  int nnz = 0;
      
  // loop over all elements
  for(iint eM=0;eM<mesh->Nelements;++eM){
    
    /* build Dx,Dy (forget the TP for the moment) */
    for(iint n=0;n<mesh->Np;++n){
      for(iint m=0;m<mesh->Np;++m){ // m will be the sub-block index for negative and positive trace
        dfloat Anm = 0;

        // (grad phi_n, grad phi_m)_{D^e}
        for(iint i=0;i<mesh->Np;++i){
          iint base = eM*mesh->Np*mesh->Nvgeo + i;
          dfloat drdx = mesh->vgeo[base+mesh->Np*RXID];
          dfloat drdy = mesh->vgeo[base+mesh->Np*RYID];
          dfloat dsdx = mesh->vgeo[base+mesh->Np*SXID];
          dfloat dsdy = mesh->vgeo[base+mesh->Np*SYID];
          dfloat JW   = mesh->vgeo[base+mesh->Np*JWID];
          
          int idn = n*mesh->Np+i;
          int idm = m*mesh->Np+i;
          dfloat dlndx = drdx*Br[idn] + dsdx*Bs[idn];
          dfloat dlndy = drdy*Br[idn] + dsdy*Bs[idn];
          dfloat dlmdx = drdx*Br[idm] + dsdx*Bs[idm];
          dfloat dlmdy = drdy*Br[idm] + dsdy*Bs[idm];
          Anm += JW*(dlndx*dlmdx+dlndy*dlmdy);
          Anm += lambda*JW*B[idn]*B[idm];
        }

        // loop over all faces in this element
        for(iint fM=0;fM<mesh->Nfaces;++fM){
          // accumulate flux terms for negative and positive traces
          dfloat AnmP = 0;
          for(iint i=0;i<mesh->Nfp;++i){
            iint vidM = mesh->faceNodes[i+fM*mesh->Nfp];

            // grab vol geofacs at surface nodes
            iint baseM = eM*mesh->Np*mesh->Nvgeo + vidM;
            dfloat drdxM = mesh->vgeo[baseM+mesh->Np*RXID];
            dfloat drdyM = mesh->vgeo[baseM+mesh->Np*RYID];
            dfloat dsdxM = mesh->vgeo[baseM+mesh->Np*SXID];
            dfloat dsdyM = mesh->vgeo[baseM+mesh->Np*SYID];

            // double check vol geometric factors are in halo storage of vgeo
            iint idM     = eM*mesh->Nfp*mesh->Nfaces+fM*mesh->Nfp+i;
            iint vidP    = mesh->vmapP[idM]%mesh->Np; // only use this to identify location of positive trace vgeo
            iint localEP = mesh->vmapP[idM]/mesh->Np;
            iint baseP   = localEP*mesh->Np*mesh->Nvgeo + vidP; // use local offset for vgeo in halo
            dfloat drdxP = mesh->vgeo[baseP+mesh->Np*RXID];
            dfloat drdyP = mesh->vgeo[baseP+mesh->Np*RYID];
            dfloat dsdxP = mesh->vgeo[baseP+mesh->Np*SXID];
            dfloat dsdyP = mesh->vgeo[baseP+mesh->Np*SYID];
            
            // grab surface geometric factors
            iint base = mesh->Nsgeo*(eM*mesh->Nfp*mesh->Nfaces + fM*mesh->Nfp + i);
            dfloat nx = mesh->sgeo[base+NXID];
            dfloat ny = mesh->sgeo[base+NYID];
            dfloat wsJ = mesh->sgeo[base+WSJID];
            dfloat hinv = mesh->sgeo[base+IHID];
            
            // form negative trace terms in IPDG
            int idnM = n*mesh->Np+vidM; 
            int idmM = m*mesh->Np+vidM;
            int idmP = m*mesh->Np+vidP;

            dfloat dlndxM = drdxM*Br[idnM] + dsdxM*Bs[idnM];
            dfloat dlndyM = drdyM*Br[idnM] + dsdyM*Bs[idnM];
            dfloat ndotgradlnM = nx*dlndxM+ny*dlndyM;
            dfloat lnM = B[idnM];

            dfloat dlmdxM = drdxM*Br[idmM] + dsdxM*Bs[idmM];
            dfloat dlmdyM = drdyM*Br[idmM] + dsdyM*Bs[idmM];
            dfloat ndotgradlmM = nx*dlmdxM+ny*dlmdyM;
            dfloat lmM = B[idmM];
            
            dfloat dlmdxP = drdxP*Br[idmP] + dsdxP*Bs[idmP];
            dfloat dlmdyP = drdyP*Br[idmP] + dsdyP*Bs[idmP];
            dfloat ndotgradlmP = nx*dlmdxP+ny*dlmdyP;
            dfloat lmP = B[idmP];
            
            dfloat penalty = tau*(mesh->N+1)*(mesh->N+1)*hinv;     

            Anm += -0.5*wsJ*lnM*ndotgradlmM;  // -(ln^-, N.grad lm^-)
            Anm += -0.5*wsJ*ndotgradlnM*lmM;  // -(N.grad ln^-, lm^-)
            Anm += +0.5*wsJ*penalty*lnM*lmM; // +((tau/h)*ln^-,lm^-)

            iint eP    = mesh->EToE[eM*mesh->Nfaces+fM];
            if (eP<0) {
             Anm += +0.5*wsJ*lnM*ndotgradlmM;  // -(ln^-, -N.grad lm^-)
             Anm += +0.5*wsJ*ndotgradlnM*lmM;  // +(N.grad ln^-, lm^-)
             Anm += -0.5*wsJ*penalty*lnM*lmM; // -((tau/h)*ln^-,lm^-) 
            } else {
             AnmP += -0.5*wsJ*lnM*ndotgradlmP;  // -(ln^-, N.grad lm^+)
             AnmP += +0.5*wsJ*ndotgradlnM*lmP;  // +(N.grad ln^-, lm^+)
             AnmP += -0.5*wsJ*penalty*lnM*lmP; // -((tau/h)*ln^-,lm^+)
            }
          }
          
          if(fabs(AnmP)>tol){
            // remote info
            iint eP    = mesh->EToE[eM*mesh->Nfaces+fM];
            iint rankP = mesh->EToP[eM*mesh->Nfaces+fM]; 
            if (eP <0) eP = eM;
            if (rankP<0) rankP = rankM;
            (*A)[nnz].row = n + eM*mesh->Np + rankStarts[rankM];
            (*A)[nnz].col = m + eP*mesh->Np + rankStarts[rankP]; // this is wrong
            (*A)[nnz].val = AnmP;
            (*A)[nnz].ownerRank = rankM;
            ++nnz;
          }
        }
        
        if(fabs(Anm)>tol){
          // local block
          (*A)[nnz].row = n + eM*mesh->Np + rankStarts[rankM];
          (*A)[nnz].col = m + eM*mesh->Np + rankStarts[rankM];
          (*A)[nnz].val = Anm;
          (*A)[nnz].ownerRank = rankM;
          ++nnz;
        }
      }
    }
  }

  // free up unused storage
  *A = (nonZero_t*) realloc(*A, nnz*sizeof(nonZero_t));
  
  *nnzA = nnz;
  
  free(B);  free(Br); free(Bs); 
  free(rankNelements); free(rankStarts);
}