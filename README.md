# FPGA-compression
A compression algorithm based on content-defined chunking and LZW encoding meant to run on an AMD Xilinx Ultra96 FPGA.

This was a project done in partial fulfillment of the SoC Architecture course at the University of Pennsylvania. Our final project was a compression algorithm that accepted an input data stream (coming over ethernet), created chunks based on the content of the data, removed duplicate chunks, and performed an LZW compression on the original chunks. The actual compression ratio varied between 30% and 80% depending on the type of input data, and the throughput was measured to be around 23 Mbps.  

This was implemented in C++ and OpenCL. We used Vitis HLS to synthesis the RTL for our Ultra96 -- which is essentially using C++ with #pragmas to instruct the compiler how to unroll loops, pipeline different parts of the program, partition arrays, and do other typical hardware acceleration tasks.
