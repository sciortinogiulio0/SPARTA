#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <ctime>
#include <string>

#include "globheads.h"
#include "protos.h"
#include "utilities.h"
#include "mkl.h"


int main(int argc, char *argv[]) {
	
	double eps;
	if (argc > 1) {
		eps = stod(argv[1]);
	} 
	else {
		eps = 0.8; // 0 < eps < 1
	}
    //this value sets how different two rows in the same block can be.
    //eps = 1 means only rows with equal structure are merged into a block
    //eps = 0 means all rows are merged into a single block
    

//INPUT SHOULD BE ALWAYS CONVERTED TO CSR BEFORE FURTHER MANIPULATION

	SparMat spmat; //this will hold the CSR matrix

//______________________________________
//INPUT EXAMPLE 1: RANDOM CSR  
    //create a random sparse matrix
    	Mat rand_mat;

   	float k = 0.6; //fraction of non-zero entries
	int n = 20;

	random_sparse_mat(rand_mat, n, k); //generate random Mat
	convert_to_CSR(rand_mat, spmat);

//______________________________________    
//INPUT EXAMPLE 2: read graph in edgelist format into CSR

//	cout << "READING GRAPH" << endl;    
//	read_snap_format(spmat, "testgraph.txt");         //Read a CSR matrix from a .txt edgelist (snap format)

    

//______________________________________
//INPUT EXAMPLE 3: read from MTX format    
    	//read from mtx
//	read_mtx_format(spmat, "testmat.mtx");


//______________________________________
//INPUT EXAMPLE 4: create a random matrix with block structure
/* 

	int n = 18; //side lenght of the matrix
	int n_block = 3; //number of blocks 
	float k_block = 0.6; //percentage of non-zero blocks
	float k = 0.5; //percentage of non-zero entries in the whole matrix


	Mat rnd_bmat;
	random_sparse_blocks_mat(rnd_bmat, n, n_block, k_block, k);

	cout << "CREATED A RND BLOCK MAT" << endl;

	convert_to_CSR(spmat,rnd_block_spmat);

	//optional: scramble the matrix?

*/
//___________________________________________
//*******************************************
//		END OF INPUT
//spmat must hold a SparMat matrix at this point
//******************************************

//reorder the CSR matrix spmat and generate a Block Sparse Matrix


        VBSparMat vbmat;
        make_sparse_blocks(spmat, vbmat,eps);	

	cout<<"CSR permuted. VBSparMat created"<<endl;

//create a dense array matrix from spmat (for GEMM with MKL)
	Mat mat;
	int mat_n = spmat.n;
	double* mat_arr;
	mat_arr = new double[mat_n*mat_n];
	convert_from_CSR(spmat, mat);
	std::copy((mat.vec).begin(), (mat.vec).end(), mat_arr);

	cout << fixed;


//create a MKL sparse matrix from spmat
        sparse_matrix_t mkl_spmat;
        convert_to_MKL(spmat, mkl_spmat);

	

	
//	cout << "PRINTING SPARSE MATRIX IN DENSE FORM" <<endl;
//	matprint(mat_arr,mat_n,mat_n);    
//	cout << "PRINTING SPARSE MATRIX IN BLOCK FORM" <<endl;    
//	matprint(vbmat);



//*******************************************
//        REPORT ON BLOCK STRUCTURE
//*******************************************
	ofstream CSV_out;
	CSV_out.open("output.txt");

	string CSV_header = "MatrixSize,OriginalSparsity,Divisions,NonzeroBlocks,AvgBlockHeight,AvgBHError,AvgBlockLength,AvgBLError,NonzeroAreaFraction,AverageBlockPopulation,ABPError,NewSparsity";
	CSV_out << CSV_header << endl;


	bool verbose = true; //print mat analysis on screen?
	features_to_CSV(&vbmat, CSV_out, verbose);//write mat analysis on csv
	CSV_out.close();
	

//*******************************************
//         MULTIPLICATION PHASE
//___________________________________________
//various ways of multiplying the sparse matrix
//with a dense one, with benchmarks
//******************************************


//TODO put all matrices in column-major format
	cout << "\n \n **************************** \n STARTING THE MULTIPLICATION PHASE \n" << endl; 
//creating the dense matrix X
	int X_rows = spmat.n;
	int X_cols = 1;	

	int seed = 123;
  	srand(seed);
	double X[X_rows*X_cols];
  	for (int k=0; k<X_rows*X_cols; k++) {
    		double x =  rand()%100;
    		X[k] = x/100;
  	}

        double X_c[X_rows*X_cols]; //column major version of X
        convert_to_col_major(X, X_c, X_rows, X_cols);


//----------------------------
//creating the output matrix Y
	double Y_gemm[spmat.n * X_cols];
    	double Y_csr[spmat.n * X_cols];
    	double Y_block[spmat.n * X_cols] = {};
	double Y_batch[spmat.n * X_cols] = {};

//dense-dense mkl gemm multiplication
    
    clock_t start_t = clock();
    cblas_dgemm (CblasRowMajor, CblasNoTrans, CblasNoTrans, mat_n, X_cols, mat_n, 1.0, mat_arr, mat_n, X, X_cols, 0, Y_gemm, X_cols);
    double total_t = (clock() - start_t)/(double) CLOCKS_PER_SEC;
        cout<<"Dense-Dense multiplication. Time taken: " << total_t<<endl;

    
//csr-dense mkl multiplication

	matrix_descr descr_spmat;
	descr_spmat.type = SPARSE_MATRIX_TYPE_GENERAL;
   	

	start_t = clock();
	mkl_sparse_d_mm (SPARSE_OPERATION_NON_TRANSPOSE, 1.0, mkl_spmat, descr_spmat, SPARSE_LAYOUT_ROW_MAJOR, X, X_cols, X_cols , 0.0, Y_csr, X_cols);
    	total_t = (clock() - start_t)/(double) CLOCKS_PER_SEC;
        cout<<"CSR-Dense multiplication. Time taken: " << total_t<<endl;
//------------------------------
	

//vbr-dense mkl multiplication	
	double Y_block_c[X_rows*X_cols] = {};

        start_t = clock();

	block_mat_multiply(vbmat, X_c, X_cols, Y_block_c);	
	
	total_t = (clock() - start_t)/(double) CLOCKS_PER_SEC;
	
	convert_to_row_major(Y_block_c,Y_block, spmat.n,X_cols);
	
	cout <<"BlockSparse-Dense multiplication. Time taken: " << total_t<<endl;


//BATCH MULTIPLUCATION NOT WORKING
    
/*
//vbr-dense BATCH mkl multiplication
	double Y_batch_c[X_rows*X_cols] = {};

        start_t = clock();

        block_mat_batch_multiply(vbmat, X_c, X_cols, Y_block_c);

        total_t = (clock() - start_t)/(double) CLOCKS_PER_SEC;

        convert_to_row_major(Y_batch_c,Y_batch, spmat.n,X_cols);

	cout <<"BlockSparse-Dense BATCH multiplication. Time taken: " << total_t<<endl;
*/
 
 
 
//PRINT RESULTING MATRICES
/*
	cout << "CSR RESULT" << endl;
        matprint(&Y_csr[0],spmat.n, X_cols);

	cout << "GEMM RESULT" << endl;
	matprint(&Y_gemm[0],spmat.n, X_cols);
	
	cout << "BLOCK RESULT" << endl;
	matprint(&Y_block[0],spmat.n, X_cols);

	cout << "BLOCK BATCH RESULT" << endl;
        matprint(&Y_batch[0],spmat.n, X_cols);
*/



}
