//==============================================================
// Copyright © 2020 Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

/**
 * Matrix_mul multiplies two large matrices both the CPU and the offload device,
 * then compares results. If the code executes on both CPU and the offload
 * device, the name of the offload device and a success message are displayed.
 *
 * For comprehensive instructions regarding DPC++ Programming, go to
 * https://software.intel.com/en-us/oneapi-programming-guide and search based on
 * relevant terms noted in the comments.
 */

#include <CL/sycl.hpp>
#include <iostream>
#include <limits>
#include <sycl/ext/intel/fpga_extensions.hpp>
#include "FakeIOPipes.hpp"
#include "HostSideChannel.hpp"
// dpc_common.hpp can be found in the dev-utilities include folder.
// e.g., $ONEAPI_ROOT/dev-utilities/<version>/include/dpc_common.hpp
#include "dpc_common.hpp"

using namespace std;
using namespace sycl;

struct DeviceToHostSideChannelID;
struct SideChannelMainKernel;
size_t vector_size = 10000;
typedef std::vector<int> IntVector; 
using MyDeviceToHostSideChannel = DeviceToHostSideChannel<DeviceToHostSideChannelID, int, true, 8>;
float a_max = 0;
float a_min = 10000000;
/**
 * Each element of the product matrix c[i][j] is computed from a unique row and
 * column of the factor matrices, a[i][k] and b[k][j]
 */

// Matrix size constants.
constexpr int m_size = 150 * 8;  // Must be a multiple of 8.
constexpr int M = m_size / 8;
constexpr int N = m_size / 4;
constexpr int P = m_size / 2;
const double MAX = 3400000000000000000000000.0;

/**
 * Perform matrix multiplication on host to verify results from device.
 */
int VerifyResult(float (*c_back)[P], float (*a_host)[N], float (*b_host)[P]);

int write_file(const char *address, const float (*a)[N], const float (*b)[P]){
  FILE* f = fopen(address,"w");
    
  for (int i = 0; i < M; i++)
    for (int j = 0; j < N; j++) fprintf(f,"%f ", a[i][j]);
    
  for (int i = 0; i < N; i++)
    for (int j = 0; j < P; j++) fprintf(f,"%f ", b[i][j]);
    
    return 0;
}

void mutate(float *a, int knob, float value){
    //srand(time(NULL) + rand());
    //int knob = rand()%4+1;
    
    if (knob==1){
        *a = value;
    }
    else if (knob==2){
        *a = 0;
    }
    else if (knob==3){
       // for (int i = 1; i < pos_i; i++){
            *a = *a * 100000;
        //}
    }
    else if (knob==4){
        //for (int i = 1; i < pos_i; i++){
            *a = *a / 100 - 100000;
            //if (a[i] < a_min){  
             //   a_min = a[i];
            //}
        //}
    }
}

int main(int argc, char* argv[]) {
  // Host memory buffer that device will write data back before destruction.
  float(*c_back)[P][10] = new float[M][P][10];
    
  float(*a)[N] = new float[M][N];
  float(*b)[P] = new float[N][P];
    
  ext::intel::fpga_emulator_selector d_selector;
  
  IntVector sum_f, flag;
  sum_f.resize(2);
  flag.resize(100);
  for (int i = 0; i < 100; i++) flag[i] = 0;
  
  std::string file;
  if (argc > 1) file = argv[1];
  std::ifstream read(file);
    
  if (!read.is_open()){
      std::cout << "Could not open the input file.\n";
  } 
  float number;
  int i = 0;
  while ((read >> number) and (i<vector_size)){
    a[i/N][i%N] = number;
    if (number>a_max) a_max = number;
    if (number<a_max) a_max = number;
    i = i + 1;
  }
 
  i = 0;
  while ((read >> number) and (i<vector_size)){
    b[i/P][i%P] = number;
    if (number>a_max) a_max = number;
    if (number<a_max) a_max = number;
    i = i + 1;
  }
  read.close();

  int file_number = 0;
  int current_file = 0;
  int result;
  
  // Initialize the device queue with the default selector. The device queue is
  // used to enqueue kernels. It encapsulates all states needed for execution.
  try {
    queue q(d_selector, dpc_common::exception_handler);
    sum_f[0] = 0;
    cout << "Device: " << q.get_device().get_info<info::device::name>() << "\n";

    // Create 2D buffers for matrices, buffer c is bound with host memory c_back

    buffer a_buf(reinterpret_cast<float *>(a), range(M, N));
    buffer b_buf(reinterpret_cast<float *>(b), range(N, P));
    buffer c_buf(reinterpret_cast<float *>(c_back), range(M, P,10));

    buffer sum_buf(sum_f);
    

    cout << "Problem size: c(" << M << "," << P << ") = a(" << M << "," << N
         << ") * b(" << N << "," << P << ")\n";

    // Submit command group to queue to multiply matrices: c = a * b
    q.submit([&](auto &h) {
        
      // Read from a and b, write to c
      accessor a(a_buf, h, read_only);
      accessor b(b_buf, h, read_only);
      accessor c(c_buf, h, write_only);
      accessor sum_f(sum_buf, h, write_only);
      
      float value = rand();
      int pos_1 = rand()% P*M;
      int pos_2 = rand()% pos_1;
      int knob = rand() % 4+1;

      int width_a = a_buf.get_range()[1];
      
 
      // Execute kernel.
      h.parallel_for(range(10),[=](auto index){
      #pragma unroll 50
      for (int pos=0;pos<P*M;pos++){
        
        //if ((row<pos_1)and(col<pos_2)){
        //    mutate(&(a[row][1]),knob,value);
        //    mutate(&(b[1][col]),knob,value);
        //}
        
        float sum = 0.0f;
        // Compute the result of one element of c
        for (int i = 0; i < width_a; i++) {
            float ai = a[pos/P][i];
            if ((pos>pos_2)and(pos<pos_1)){
                mutate(&ai, knob, value);
            }
          sum += ai * b[i][pos-P*pos/P];
        }
        c[pos/P][pos-P*pos/P][index[0]] = sum;
      }
    });
    });
  int interested = 0;
  std::cout<<sum_f[0]<<" Overflow were found.\n";

  
  cout << "Result of matrix multiplication using DPC++: ";

  } catch (sycl::exception const &e) {
    cout << "An exception is caught while multiplying matrices.\n";
    terminate();
  }
  
  
  delete[] c_back;
  std::ofstream outfile;
  outfile.open("exec_fpga_info.txt");
  outfile << sum_f[0] << std::endl << sum_f[0] << std::endl << sum_f[0];
  outfile.close();

  return result;
}

bool ValueSame(float a, float b) {
  return fabs(a - b) <= a * 0.001;
}

int VerifyResult(float (*c_back)[P], float (*a_host)[N], float (*b_host)[P]) {
  // Check that the results are correct by comparing with host computing.
  int i, j, k;

  // 2D arrays on host side.

  float(*c_host)[P] = new float[M][P];

  // c_host is initialized to zero.
  for (i = 0; i < M; i++)
    for (j = 0; j < P; j++) c_host[i][j] = 0.0f;

  for (i = 0; i < M; i++) {
    for (k = 0; k < N; k++) {
      // Each element of the product is just the sum 1+2+...+n
      for (j = 0; j < P; j++) {
        c_host[i][j] += a_host[i][k] * b_host[k][j];
      }
    }
  }

  bool mismatch_found = false;

  // Compare host side results with the result buffer from device side: print
  // mismatched data 5 times only.
  int print_count = 0;

  for (i = 0; i < M; i++) {
    for (j = 0; j < P; j++) {
      if (!ValueSame(c_back[i][j], c_host[i][j])) {
        cout << "Fail - The result is incorrect for element: [" << i << ", "
             << j << "], expected: " << c_host[i][j]
             << ", but found: " << c_back[i][j] << "\n";
        mismatch_found = true;
        print_count++;
        if (print_count == 5) break;
      }
    }

    if (print_count == 5) break;
  }
    

  delete[] c_host;

  if (!mismatch_found) {
    cout << "Success - The results are correct!\n";
    return 0;
  } else {
    cout << "Fail - The results mismatch!\n";
    return -1;
  }
}