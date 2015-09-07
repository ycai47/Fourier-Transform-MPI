// Distributed two-dimensional Discrete FFT transform
// Yushan Cai
// ECE4412 Project 1

//mpirun -np 16 ./fft2d
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <signal.h>
#include <math.h>
#include <mpi.h>
#include "Complex.h"
#include "InputImage.h"

using namespace std;

void Transform1D(Complex* h, int w, Complex* H)
{
  // Implement a simple 1-d DFT using the double summation equation
  // given in the assignment handout.  h is the time-domain input
  // data, w is the width (N), and H is the output array.
  Complex W(cos(double(2*M_PI/w)),-sin(double(2*M_PI/w)));
  Complex W_nk;
  for (int n = 0; n < w; ++n)
  {
    H[n].real = 0;
    H[n].imag = 0;
    for (int k = 0; k < w; ++k)
    {
      W_nk.real = pow(W.Mag().real, n*k) * cos(W.Angle().real*n*k);
      W_nk.imag = - pow(W.Mag().real, n*k) * sin(W.Angle().real*n*k);
      H[n] = H[n] + W_nk*h[k];
    }
  }
}

void Transform2D(const char* inputFN) 
{ // Do the 2D transform here.
  // 1) Use the InputImage object to read in the Tower.txt file and
  //    find the width/height of the input image.
    InputImage * image = new InputImage(inputFN); // Create the helper object for reading the image
    int width = image->GetWidth();
    int height = image->GetHeight();
  // 2) Use MPI to find how many CPUs in total, and which one
  //    this process is
    int rank, numtasks, rc;
    rc = MPI_Init(NULL, NULL);
    if (rc != MPI_SUCCESS)
    {
      printf ("Error starting MPI program. Terminating.\n");
      MPI_Abort(MPI_COMM_WORLD, rc);
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks); 
  // 3) Allocate an array of Complex object of sufficient size to
  //    hold the 2d DFT results (size is width * height)
  // 4) Obtain a pointer to the Complex 1d array of input data
    Complex * data = image->GetImageData();
    const int row_per_proc = height / numtasks;
    const int col_per_proc = width / numtasks;
    int start_row = rank * row_per_proc;
    Complex * result_1d = new Complex[row_per_proc * width];
    if (result_1d == NULL)
    {
      printf("Error: memory could not be allocated on rank %d", rank);
    }
  // 5) Do the individual 1D transforms on the rows assigned to your CPU
    for (int i = 0; i < row_per_proc; ++i)
    {
      Transform1D(&data[(start_row+i)*width], width, &result_1d[i*width]);
    }
    printf("1d transform for rank %d done\n", rank);
  // 6) Send the resultant transformed values to the appropriate
  //    other processors for the next phase.
  // 6a) To send and receive rows, you might need a separate
  //     Complex array of the correct size.
    Complex * mess_send = new Complex[row_per_proc * col_per_proc];
    MPI_Request request;
    MPI_Status status;
    for (int i = 0; i < numtasks; ++i)
    {
      if (i == rank) continue;
      else
      {
        for (int j = 0; j < row_per_proc; ++j) //assign message
        {
          for (int k = 0; k < col_per_proc; ++k)
          {
            mess_send[j * width + k] = result_1d[(start_row+j) * width + i*col_per_proc+k];
          }
        }
        rc = MPI_Isend(mess_send, row_per_proc * col_per_proc, MPI_COMPLEX, i,
                         0, MPI_COMM_WORLD, &request);
        if (rc != MPI_SUCCESS)
        {
          cout << "Rank " << rank << "to process" << i << " send failed, rc " << rc << endl;
          MPI_Finalize();
          return;
        }
        MPI_Wait(&request, &status);
      }
    }
  // 7) Receive messages from other processes to collect your rows
    Complex * data_2d = new Complex[row_per_proc * width];
    for (int i = 0; i < numtasks; ++i)
    {
      if (i == rank) continue;
      else
      {
        MPI_Status status;
        int flag = 0;
        MPI_Iprobe(i, 0, MPI_COMM_WORLD, &flag, &status);
        while(!flag){}//wait till ready to receive
        Complex * mess_recv = new Complex[row_per_proc * col_per_proc];//create buffer
        rc = MPI_Irecv(mess_recv, row_per_proc * col_per_proc, MPI_COMPLEX, MPI_ANY_SOURCE,
                         0, MPI_COMM_WORLD, &request);
        if (rc != MPI_SUCCESS)
        {
          cout << "Rank " << rank << " recv failed, rc " << rc << endl;
          MPI_Finalize();
          return;
        }
        for (int j = 0; j < row_per_proc; ++j)
        {
          for (int k = 0; k < col_per_proc; ++k)
          {
            data_2d[(start_row+j) * width + i*col_per_proc+k] = mess_recv[j * width + k];
          }
        }
        printf("rank %d reveive data from process %d\n", rank, i);
      }
    }
  // 8) When all rows received, do the 1D transforms on the rows
    Complex * result = new Complex[row_per_proc * width];
    for (int i = 0; i < col_per_proc; ++i)
    {
      Transform1D(&data_2d[(start_row+i)*width], width, &result[i*width]);
    }
  // 9) Send final answers to CPU 0 (unless you are CPU 0)
  //   9a) If you are CPU 0, collect all values from other processors
  //       and print out with SaveImageData().
    if (rank == 0)
    {
      Complex * result_2d = new Complex[width * height];
      for (int i = 1; i < numtasks; ++i)
      {
        MPI_Status status;
        int flag = 0;
        MPI_Iprobe(i, 0, MPI_COMM_WORLD, &flag, &status);
        while(!flag){}//wait till ready to receive
        Complex * rows = new Complex[row_per_proc * width];
        rc = MPI_Irecv(rows, row_per_proc * width, MPI_COMPLEX, MPI_ANY_SOURCE,
                         0, MPI_COMM_WORLD, &request);
        if (rc != MPI_SUCCESS)
        {
          cout << "From rank " << rank << " recv failed, rc " << rc << endl;
          MPI_Finalize();
          return;
        }
        for (int j = 0; j < height; ++j)
        {
          for (int k = 0; k < width; ++k)
          {
            result_2d[(i*row_per_proc+j)*width+k] = rows[j*width+k];
          }
        }
      }
      string fn("Finshed.txt");
      image->SaveImageData(fn.c_str(), result_2d, width, height);
    }
    else
    {
      rc = MPI_Isend(result, row_per_proc * width, MPI_COMPLEX, 0,
                         0, MPI_COMM_WORLD, &request);
    }
      MPI_Finalize();
}

int main(int argc, char** argv)
{
  string fn("Tower.txt"); // default file name
  if (argc > 1) fn = string(argv[1]);  // if name specified on cmd line
  // MPI initialization here
  Transform2D(fn.c_str()); // Perform the transform.
  // Finalize MPI here
}