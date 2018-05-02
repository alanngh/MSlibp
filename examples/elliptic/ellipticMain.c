#include "elliptic.h"

int main(int argc, char **argv){

  // start up MPI
  MPI_Init(&argc, &argv);

  if(argc!=2){
    printf("usage: ./ellipticMain setupfile\n");
    exit(-1);
  }

  // if argv > 2 then should load input data from argv
  setupAide options(argv[1]);
  
  // set up mesh stuff
  string fileName;
  int N, dim, elementType;

  options.getArgs("MESH FILE", fileName);
  options.getArgs("POLYNOMIAL DEGREE", N);
  options.getArgs("ELEMENT TYPE", elementType);
  options.getArgs("MESH DIMENSION", dim);

  // set up mesh
  mesh_t *mesh;
  switch(elementType){
  case TRIANGLES:
    mesh = meshSetupTri2D((char*)fileName.c_str(), N); break;
  case QUADRILATERALS:
    mesh = meshSetupQuad2D((char*)fileName.c_str(), N); break;
  case TETRAHEDRA:
    mesh = meshSetupTet3D((char*)fileName.c_str(), N); break;
  case HEXAHEDRA:
    mesh = meshSetupHex3D((char*)fileName.c_str(), N); break;
  }

  // parameter for elliptic problem (-laplacian + lambda)*q = f
  //dfloat lambda = 1;
  dfloat lambda;
  options.getArgs("LAMBDA", lambda);

  // set up
  occa::kernelInfo kernelInfo;
  elliptic_t *elliptic = ellipticSetup(mesh, kernelInfo, options);

  // convergence tolerance
  dfloat tol = 1e-8;
  ellipticSolve(elliptic, lambda, tol, elliptic->o_r, elliptic->o_x, options);


  if(options.compareArgs("DISCRETIZATION","CONTINUOUS")){
    dfloat zero = 0.;
    elliptic->addBCKernel(mesh->Nelements,
                       zero,
                       mesh->o_x,
                       mesh->o_y,
                       mesh->o_z,
                       elliptic->o_mapB,
                       elliptic->o_x);
  }

  // copy solution from DEVICE to HOST
  o_x.copyTo(mesh->q);

  if (options.compareArgs("BASIS","BERN"))   applyElementMatrix(mesh,mesh->VB,mesh->q,mesh->q);

  dfloat maxError = 0;
  for(dlong e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->Np;++n){
      dlong   id = e*mesh->Np+n;
      dfloat xn = mesh->x[id];
      dfloat yn = mesh->y[id];
      dfloat exact = sin(M_PI*xn)*sin(M_PI*yn);
      dfloat error = fabs(exact-mesh->q[id]);

      maxError = mymax(maxError, error);
      mesh->q[id] -= exact;
    }
  }

  dfloat globalMaxError = 0;
  MPI_Allreduce(&maxError, &globalMaxError, 1, MPI_DFLOAT, MPI_MAX, MPI_COMM_WORLD);
  if(rank==0)
    printf("globalMaxError = %g\n", globalMaxError);

  char fname[BUFSIZ];
  string outName;
  options.getArgs("OUTPUT FILE NAME", outName);
  sprintf(fname, "%s_%04d.vtu",(char*)outName.c_str(), rank);
  if(elliptic->dim==3)
    meshPlotVTU3D(mesh, fname, 0);
  else 
    meshPlotVTU2D(mesh, fname, 0);

  // close down MPI
  MPI_Finalize();

  exit(0);
  return 0;
}
