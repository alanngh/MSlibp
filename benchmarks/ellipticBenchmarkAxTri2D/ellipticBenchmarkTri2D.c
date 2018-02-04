#include "ellipticBenchmarkTri2D.h"

int main(int argc, char **argv){

  // start up MPI
  MPI_Init(&argc, &argv);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if(argc<4){
    printf("usage 1: ./ellipticBenchmarkTri2D kernel.okl ../../meshes/cavityH005.msh N\n");
    printf("usage 2: ./ellipticBenchmarkTri2D kernel.okl ../../meshes/cavityH005.msh N Nblocks Nnodes\n");
    exit(-1);
  }

  // int specify polynomial degree
  int N = atoi(argv[3]);
  int Nblocks = 1;
  int Nnodes = 1;

  if (argc == 6) {
    Nblocks = atoi(argv[4]);
    Nnodes = atoi(argv[5]);
  }

  char *kernelFileName = strdup(argv[1]);

  // set up mesh
  mesh2D *mesh = meshSetupTri2D(argv[2], N);

  // solver can be CG or PCG
  // can add FLEXIBLE and VERBOSE options
  // method can be IPDG or CONTINUOUS
  // basis can be NODAL or BERN
  // preconditioner can be NONE, JACOBI, OAS, MASSMATRIX, FULLALMOND, or MULTIGRID
  // OAS and MULTIGRID: smoothers can be FULLPATCH, FACEPATCH, LOCALPATCH, OVERLAPPINGPATCH, or DAMPEDJACOBI
  //                      patch smoothers can include EXACT
  // MULTIGRID: smoothers can include CHEBYSHEV for smoother acceleration
  // MULTIGRID: levels can be ALLDEGREES, HALFDEGREES, HALFDOFS
  // FULLALMOND: can include MATRIXFREE option
  char *options =
    //strdup("solver=PCG,FLEXIBLE,VERBOSE method=IPDG basis=NODAL preconditioner=OAS smoother=FULLPATCH");
  strdup("solver=PCG,FLEXIBLE,SPARSE,VERBOSE,LEFT, method=CONTINUOUS basis=NODAL preconditioner=FULLALMOND");    
//strdup("solver=PCG,FLEXIBLE,VERBOSE method=BRDG basis=BERN preconditioner=FULLALMOND");
    //strdup("solver=PCG,FLEXIBLE,VERBOSE method=IPDG basis=NODAL preconditioner=NONE");
    //strdup("solver=PCG,FLEXIBLE,VERBOSE method=IPDG basis=NODAL preconditioner=JACOBI");

  //FULLALMOND, OAS, and MULTIGRID will use the parAlmondOptions in setup
  // solver can be EXACT, KCYCLE, or VCYCLE
  // smoother can be DAMPEDJACOBI or CHEBYSHEV
  // can add GATHER to build a gsop
  // partition can be STRONGNODES, DISTRIBUTED, SATURATE
  char *parAlmondOptions =
 strdup("solver=KCYCLE,VERBOSE smoother=CHEBYSHEV partition=STRONGNODES");    
//strdup("solver=EXACT,VERBOSE smoother=CHEBYSHEV partition=STRONGNODES");

  // parameter for elliptic problem (-laplacian + lambda)*q = f
  //dfloat lambda = 1;
  dfloat lambda = 0;

  // Boundary Type translation. Just default from the mesh file.
  int BCType[3] = {0,1,2};

  dfloat tau;
  if (strstr(options,"IPDG")) {
    tau = 2.0*(mesh->N+1)*(mesh->N+2)/2.0;
  } else if (strstr(options,"BRDG")) {
    tau = 1.0;
  }



  // set up
  occa::kernelInfo kernelInfo;

  //add user defined boundary data
  char *boundaryHeaderFileName = strdup("homogeneous2D.h"); // default
  kernelInfo.addInclude(boundaryHeaderFileName);

  solver_t *solver = ellipticSetupTri2D(mesh, tau, lambda, BCType, kernelInfo, options, parAlmondOptions, Nblocks, Nnodes);
  solver->lambda = lambda;

  ellipticRunBenchmark2D(solver,options,kernelInfo,kernelFileName,Nblocks,Nnodes);

  // close down MPI
  MPI_Finalize();

  exit(0);
  return 0;
}