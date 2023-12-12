#include "Utilities.h"
std::vector<cl::Device> get_xilinx_devices() 
{
    size_t i;
    cl_int err;
    std::vector<cl::Platform> platforms;
    err = cl::Platform::get(&platforms);
    cl::Platform platform;
    for (i  = 0 ; i < platforms.size(); i++){
        platform = platforms[i];
        std::string platformName = platform.getInfo<CL_PLATFORM_NAME>(&err);
        if (platformName == "Xilinx"){
            std::cout << "INFO: Found Xilinx Platform" << std::endl;
            break;
        }
    }
    if (i == platforms.size()) {
        std::cout << "ERROR: Failed to find Xilinx platform" << std::endl;
        exit(EXIT_FAILURE);
    }
   
    //Getting ACCELERATOR Devices and selecting 1st such device 
    std::vector<cl::Device> devices;
    err = platform.getDevices(CL_DEVICE_TYPE_ACCELERATOR, &devices);
    return devices;
}
   
char* read_binary_file(const std::string &xclbin_file_name, unsigned &nb) 
{
    if(access(xclbin_file_name.c_str(), R_OK) != 0) {
        printf("ERROR: %s xclbin not available please build\n", xclbin_file_name.c_str());
        exit(EXIT_FAILURE);
    }
    //Loading XCL Bin into char buffer 
    std::cout << "INFO: Loading '" << xclbin_file_name << "'\n";
    std::ifstream bin_file(xclbin_file_name.c_str(), std::ifstream::binary);
    bin_file.seekg (0, bin_file.end);
    nb = bin_file.tellg();
    bin_file.seekg (0, bin_file.beg);
    char *buf = new char [nb];
    bin_file.read(buf, nb);
    return buf;
}

// An event callback function that prints the operations performed by the OpenCL
// runtime.
void event_cb(cl_event event1, cl_int cmd_status, void *data) {
  cl_int err;
  cl_command_type command;
  cl::Event event(event1, true);
  OCL_CHECK(err, err = event.getInfo(CL_EVENT_COMMAND_TYPE, &command));
  cl_int status;
  OCL_CHECK(err,
            err = event.getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &status));
  const char *command_str;
  const char *status_str;
  switch (command) {
  case CL_COMMAND_READ_BUFFER:
    command_str = "buffer read";
    break;
  case CL_COMMAND_WRITE_BUFFER:
    command_str = "buffer write";
    break;
  case CL_COMMAND_NDRANGE_KERNEL:
    command_str = "kernel";
    break;
  case CL_COMMAND_MAP_BUFFER:
    command_str = "kernel";
    break;
  case CL_COMMAND_COPY_BUFFER:
    command_str = "kernel";
    break;
  case CL_COMMAND_MIGRATE_MEM_OBJECTS:
    command_str = "buffer migrate";
    break;
  default:
    command_str = "unknown";
  }
  switch (status) {
  case CL_QUEUED:
    status_str = "Queued";
    break;
  case CL_SUBMITTED:
    status_str = "Submitted";
    break;
  case CL_RUNNING:
    status_str = "Executing";
    break;
  case CL_COMPLETE:
    status_str = "Completed";
    break;
  default:
    status_str = "unknown";
  }
  printf("[%s]: %s %s\n", reinterpret_cast<char *>(data), status_str,
         command_str);
  fflush(stdout);
}

// Sets the callback for a particular event
void set_callback(cl::Event event, const char *queue_name) {
  cl_int err;
  OCL_CHECK(err,
            err = event.setCallback(CL_COMPLETE, event_cb, (void *)queue_name));
}
void Exit_with_error(const char *s)
{
    printf("%s\n", s);
    exit(EXIT_FAILURE);
}

void pin_thread_to_cpu(std::thread &t, int cpu_num)
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__APPLE__)
    return;
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_num, &cpuset);
    int rc =
        pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0)
    {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
#endif
}

void pin_main_thread_to_cpu0()
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__APPLE__)
    return;
#else
    pthread_t thread;
    thread = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    int rc =
        pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0)
    {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
#endif
}

//void Load_data(unsigned char *Data)
//{
//    unsigned int Size = FRAMES * FRAME_SIZE;
//
//    FILE *File = fopen("../data/Input.bin", "rb");
//    if (File == NULL)
//        Exit_with_error("fopen for Load_data failed");
//
//    if (fread(Data, 1, Size, File) != Size)
//        Exit_with_error("fread for Load_data failed");
//
//    if (fclose(File) != 0)
//        Exit_with_error("fclose for Load_data failed");
//}
//
//void Store_data(const char *Filename, unsigned char *Data, unsigned int Size)
//{
//    FILE *File = fopen(Filename, "wb");
//    if (File == NULL)
//        Exit_with_error("fopen for Store_data failed");
//
//    if (fwrite(Data, 1, Size, File) != Size)
//        Exit_with_error("fwrite for Store_data failed");
//
//    if (fclose(File) != 0)
//        Exit_with_error("fclose for Store_data failed");
//}
//
//void Check_data(unsigned char *Data, unsigned int Size)
//{
//    int error_code = 0;
//    unsigned char *Data_golden = (unsigned char *)malloc(MAX_OUTPUT_SIZE);
//    FILE *File = fopen("../data/Golden.bin", "rb");
//    if (File == NULL)
//        Exit_with_error("fopen for Check_data failed");
//
//    if (fread(Data_golden, 1, Size, File) != Size)
//        Exit_with_error("fread for Check_data failed");
//
//    if (fclose(File) != 0)
//        Exit_with_error("fclose for Check_data failed");
//
//    for (unsigned int i = 0; i < Size; i++)
//    {
//        if (Data_golden[i] != Data[i])
//        {
//            free(Data_golden);
//            error_code = i + 1;
//        }
//    }
//
//    free(Data_golden);
//
//    if (error_code != 0)
//    {
//        printf("You got errors in data %d\n", error_code);
//        Exit_with_error("Input.bin and Golden.bin doesn't match");
//    }
//    else
//    {
//        printf("Application completed successfully.\n");
//    }
//}
