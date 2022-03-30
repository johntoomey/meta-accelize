/*
 * Copyright (c) 2021-2021 ProvenRun S.A.S
 * All Rights Reserved.
 *
 * This software is the confidential and proprietary information of
 * ProvenRun S.A.S ("Confidential Information"). You shall not
 * disclose such Confidential Information and shall use it only in
 * accordance with the terms of the license agreement you entered
 * into with ProvenRun S.A.S
 *
 * PROVENRUN S.A.S MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE
 * SUITABILITY OF THE SOFTWARE, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT. PROVENRUN S.A.S SHALL
 * NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING,
 * MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
 */
/*
 * File: drmtest.cpp
 */

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <CL/opencl.h>
#include <CL/cl_ext.h>

// XRT low level API
#include "xclhal2.h"

// Accelize DRMLib
#if __cplusplus
#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <json/json.h>

#include "accelize/drm.h"

using namespace std;
using namespace Accelize::DRM;
#else
#include "accelize/drmc.h"
#endif


#define DRM_CTRL_ADDRESS    0xa0010000
#define DRM_ACTR_ADDRESS    0xa0020000

#define REG_CTRL_VERSION         0x0
#define REG_CTRL_MAILBOX_SIZE    0x8

#define REG_ACTR_STATUS          0x38
#define REG_ACTR_MAILBOX         0x3C
#define REG_ACTR_VERSION         0x40
#define REG_ACTR_EVENT_INC       0x40
#define REG_ACTR_EVENT_CNT       0x44

uint32_t DrmAsyncError = 0;


//*******************************************
// Access to PL from PS
//*******************************************

// xclhal2 library handler
xclDeviceHandle boardHandler;

//
// Initialize xclhal2 path to access PL registry
//
int32_t init_register() {
    int ret = 0;
    ret = xclProbe();
    if(ret < 1) {
        printf("ERROR - %s: xclProbe failed with code %d\n", __FUNCTION__, ret);
        return 1;
    }
    boardHandler = xclOpen(0, "xclhal2_logfile.log", XCL_ERROR);
    if(boardHandler == NULL) {
        printf("ERROR - %s: xclOpen failed\n", __FUNCTION__);
        return 1;
    }
    printf("Access to PL registry is enabled\n");
    return 0;
}

//
// Read PL registers
//
int32_t read_register(uint32_t addr, uint32_t* value) {
    if(xclLockDevice(boardHandler)) {
        printf("ERROR - %s: xclLock failed.", __FUNCTION__);
        return 1;
    }
    int ret = (int)xclRead(boardHandler, XCL_ADDR_KERNEL_CTRL, addr, value, 4);
    if(ret <= 0) {
        printf("ERROR - %s: Unable to read FPGA at 0x%08X (ret = %d)\n", __FUNCTION__, addr, ret);
        return 1;
    }
    if(xclUnlockDevice(boardHandler)) {
        printf("ERROR - %s: xclUnlock failed.", __FUNCTION__);
        return -1;
    }
    return 0;
}

//
// Write PL registers
//
int32_t write_register(uint32_t addr, uint32_t value) {
    if(xclLockDevice(boardHandler)) {
        printf("ERROR - %s: xclLock failed.", __FUNCTION__);
        return 1;
    }
    int ret = (int)xclWrite(boardHandler, XCL_ADDR_KERNEL_CTRL, addr, &value, 4);
    if(ret <= 0) {
        printf("ERROR - %s: Unable to write FPGA at 0x%08X (ret = %d)\n", __FUNCTION__, addr, ret);
        return 1;
    }
    if(xclUnlockDevice(boardHandler)) {
        printf("ERROR - %s: xclUnlock failed.", __FUNCTION__);
        return 1;
    }
    return 0;
}

//
// Close access to PL registry
//
int32_t uninit_register() {
    // Release xclhal2 board handler
    xclClose(boardHandler);
    printf("Access to PL registry is disabled\n");
    return 0;
}


//
// DRMLib Error Callback Function
//
void drm_error_callback( const char* errmsg, void* user_p ){
    printf("DRM ERROR: %s", errmsg);
    DrmAsyncError ++;
}


cl_uint load_file_to_memory(const char *filename, char **result)
{
    cl_uint size = 0;
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        *result = NULL;
        return -1; // -1 means file opening fail
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    *result = (char *)malloc(size+1);
    if (size != fread(*result, sizeof(char), size, f)) {
        free(*result);
        return -2; // -2 means file reading fail
    }
    fclose(f);
    (*result)[size] = 0;
    return size;
}


uint32_t check_drm_states( DrmManager *pDrmManager, bool expected_active, 
                        std::string expected_license_type ) {
    uint32_t err = 0;
    uint32_t reg = 0;
#if __cplusplus
    bool license_status = pDrmManager->get<bool>(ParameterKey::license_status);
    bool session_status = pDrmManager->get<bool>(ParameterKey::session_status);
    std::string license_type = pDrmManager->get<std::string>(ParameterKey::drm_license_type);
#else
    bool license_status = DrmManager_get_bool(pDrmManager, DRM__license_status);
    bool session_status = DrmManager_get_bool(DRM__session_status);
    std::string license_type = DrmManager_get_string(pDrmManager, DRM__drm_license_type);
#endif
    read_register(DRM_ACTR_ADDRESS + REG_ACTR_STATUS, &reg);
    if (expected_active && (reg!=3)) {
        printf("ERROR: Unexpected DRM Activator status: %u\n", reg);
        err ++;
    } else if (!expected_active && (license_type != "Node-Locked") && (reg!=0)) {
        printf("ERROR: Unexpected DRM Activator status: %u\n", reg);
        err ++;
    }
    if (license_status != expected_active) {
        printf("ERROR: Unexpected DRM License status: %u\n", license_status);
        err ++;
    }
    if (session_status != expected_active) {
        printf("ERROR: Unexpected DRM Session status: %u\n", session_status);
        err ++;
    }
    if (expected_license_type.size() && (license_type != expected_license_type)) {
        printf("ERROR: Unexpected license type: %s\n", license_type.c_str());
        err ++;
    }
    return err + DrmAsyncError;
}

uint32_t generate_drm_event(uint32_t count) {
    uint32_t err = 0;
    for(uint32_t i=0; i<count; i++) {
        err += write_register(DRM_ACTR_ADDRESS + REG_ACTR_EVENT_INC, 1);
    }
    if (err)
        printf("ERROR: Failed to generate %u DRM events\n", count);
    return err + DrmAsyncError;
}


uint32_t check_drm_event(DrmManager* pDrmManager, uint32_t expected_event) {
    uint32_t drm_lib_evt;
    uint32_t drm_act_evt;
    uint32_t err=0;
    // Check DRM Lib event
#if __cplusplus
        Json::Value json_value;
        json_value["metered_data"] = Json::nullValue;
        pDrmManager->get( json_value );
        drm_lib_evt = json_value["metered_data"][0].asInt64();
#else
        DrmManager_get_uint64(pDrmManager, DRM__metered_data, &drm_lib_evt);
#endif
    if (drm_lib_evt != expected_event) {
        printf("ERROR: Unexpected DRM Library event counter: get %u but expect %d\n", 
                    drm_lib_evt, expected_event);
        err ++;
    }
    // Check DRM Activator event
    read_register(DRM_ACTR_ADDRESS + REG_ACTR_EVENT_CNT, &drm_act_evt);
    if (drm_act_evt != expected_event) {
        printf("ERROR: drm_act_evt DRM Activator event counter: get %u but expect %d\n", 
                    drm_lib_evt, expected_event);
        err ++;
    }
    return err + DrmAsyncError;
}


//*******************************************
// MAIN
//*******************************************


void print_usage() {
    printf("Usage: drmtest [MODE]\n");
    printf("Example: drmtest 1\n");
    printf("Run Accelize DRM self-test application. Two test modes can be executed: nodelocked or metering modes\n");
    printf("In nodelocked mode, no Internet access is required except for the first run to provision the permanent license file. \
You can also request the license file by email at support@accelize.com.\n");
    printf("In metering mode, an Internet access is required regularly.\n");
    printf("MODE\tSelect test mode: 0=nodelocked, otherwize metering.\n");
}


int main(int argc, char **argv) {

    if (argc < 2) {
        print_usage();
        return -1;
    }
    
    bool nodelock = std::string(argv[1]) == "0";
    printf("Selected nodelocked mode: %u\n", nodelock);    
    
    std::string conf_path("/usr/bin/kria_nodelock_conf.json");
    if (!nodelock) {
        conf_path = std::string("/usr/bin/kria_metering_conf.json");
    }
    std::string cred_path("/usr/bin/kria_cred.json");
    std::string xclbin("/lib/firmware/xilinx/fpgatest/fpgatest.xclbin");
    printf("Using xclbin file: %s\n", xclbin.c_str());
    
    DrmAsyncError = 0;
    cl_int err = -1;
    unsigned long long drmlib_evt_cnt, drmtest_evt_cnt, drmact_evt_cnt;
    std:string license_type_expected;    
    DrmManager *pDrmManager = NULL;   
    uint32_t reg, reg_expected;
    cl_platform_id platform_id;         // platform id
    cl_device_id device_id;             // compute device id
    cl_context context;                 // compute context
    cl_program program;                 // compute programs
    char cl_platform_vendor[1001];

    // Get all platforms and then select Xilinx platform
    cl_platform_id platforms[16];       // platform id
    cl_uint platform_count;
    cl_uint platform_found = 0;
    err = clGetPlatformIDs(16, platforms, &platform_count);
    if (err != CL_SUCCESS) {
        printf("ERROR: Failed to find an OpenCL platform!\n");
        return EXIT_FAILURE;
    }
    printf("INFO: Found %d platforms\n", platform_count);

    // Find Xilinx Plaftorm
    for (cl_uint iplat=0; iplat<platform_count; iplat++) {
        err = clGetPlatformInfo(platforms[iplat], CL_PLATFORM_VENDOR, 1000, (void *)cl_platform_vendor,NULL);
        if (err != CL_SUCCESS) {
            printf("ERROR: clGetPlatformInfo(CL_PLATFORM_VENDOR) failed!\n");
            return EXIT_FAILURE;
        }
        if (strcmp(cl_platform_vendor, "Xilinx") == 0) {
            printf("INFO: Selected platform %d from %s\n", iplat, cl_platform_vendor);
            platform_id = platforms[iplat];
            platform_found = 1;
        }
    }
    if (!platform_found) {
        printf("ERROR: Platform Xilinx not found. Exit.\n");
        return EXIT_FAILURE;
    }

    // Get Accelerator compute device
    cl_uint num_devices;
    cl_uint device_found = 0;
    cl_device_id devices[16];  // compute device id
    char cl_device_name[1001];
    err = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_ACCELERATOR, 16, devices, &num_devices);
    printf("INFO: Found %d devices\n", num_devices);
    if (err != CL_SUCCESS) {
        printf("ERROR: Failed to create a device group!\n");
        return EXIT_FAILURE;
    }

    //iterate all devices to select the target device.
    err = clGetDeviceInfo(devices[0], CL_DEVICE_NAME, 1024, cl_device_name, 0);
    if (err != CL_SUCCESS) {
        printf("ERROR: Failed to get device name for device %d!\n", 0);
        return EXIT_FAILURE;
    }
    device_id = devices[0];
    printf("Selected %s as the target device\n", cl_device_name);
    
    // Create a compute context
    context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
    if (!context) {
        printf("ERROR: Failed to create a compute context!\n");
        return EXIT_FAILURE;
    }

    cl_int status;

    // Create Program Objects
    // Load binary from disk
    unsigned char *kernelbinary;
    
    //------------------------------------------------------------------------------
    // xclbin
    //------------------------------------------------------------------------------
    printf("INFO: loading xclbin %s\n", xclbin.c_str());
    cl_uint n_i0 = load_file_to_memory(xclbin.c_str(), (char **) &kernelbinary);
    if (n_i0 < 0) {
        printf("ERROR: failed to load kernel from xclbin: %s\n", xclbin.c_str());
        return EXIT_FAILURE;
    }

    size_t n0 = n_i0;

    // Create the compute program from offline
    program = clCreateProgramWithBinary(context, 1, &device_id, &n0,
                                        (const unsigned char **) &kernelbinary, &status, &err);
    free(kernelbinary);

    if ((!program) || (err!=CL_SUCCESS)) {
        printf("ERROR: Failed to create compute program from binary %d!\n", err);
        return EXIT_FAILURE;
    }

    // Build the program executable
    //
    err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t len;
        char buffer[2048];
        printf("ERROR: Failed to build program executable!\n");
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
        printf("%s\n", buffer);
        return EXIT_FAILURE;
    }

    // Init xclhal2 library
    if (init_register()) {
        err ++;
        goto quit;
    }
    
    //--------------------------------------
    // TESTING DRM BRIDGE
    //--------------------------------------

    // Read DRM Controller Bridge version
    read_register(DRM_CTRL_ADDRESS + REG_CTRL_VERSION, &reg);
    printf("DRM Controller Bridge version: 0x%08X\n", reg);

    // Read DRM Controller Mailbox size register
    read_register(DRM_CTRL_ADDRESS + REG_CTRL_MAILBOX_SIZE, &reg);
    printf("DRM Controller Mailbox sizes: 0x%08X\n", reg);
    
    //--------------------------------------
    // TESTING DRM ACTIVATOR
    //--------------------------------------

    // Check DRM Activator existence
    read_register(DRM_ACTR_ADDRESS + REG_ACTR_VERSION, &reg);
    printf("DRM Activator existence: 0x%08X\n", reg);

    // Reset DRM Activator event counter
    write_register(DRM_ACTR_ADDRESS + REG_ACTR_EVENT_CNT, 0);
    read_register(DRM_ACTR_ADDRESS + REG_ACTR_EVENT_CNT, &reg);
    if (reg != 0) {
        printf("ERROR: Failed to reset event counter on DRM Activator\n");
        err ++;
        goto quit;
    }
    drmtest_evt_cnt = 0;
    
    
// ACCELIZE CODE TO UNLOCK APPLICATION STARTS HERE
    printf("[DRMLIB] Starting DRM session ...\n");
#if __cplusplus
    pDrmManager = new DrmManager(
        conf_path,
        cred_path,
        nullptr,
        nullptr,
        [&]( const  std::string & err_msg) {
           drm_error_callback(err_msg.c_str(), nullptr);
        }
    );
    try {
        err += check_drm_states(pDrmManager, false, "");
        pDrmManager->activate();        
    } catch( const Exception& e ) {                                                                
        printf("ERROR: DRM failure: %s\n", e.what());
        err ++;
    }
#else
    if (DRM_OK != DrmManager_alloc(&pDrmManager,
            conf_path.c_str(), 
            cred_path.c_str(),
            NULL, NULL, drm_error_callback,
            NULL )) {
        printf("ERROR: Error allocating DRM Manager object: %s\n", pDrmManager->error_message);
        err ++;
    }
    err += check_drm_states(pDrmManager, false, "");
    if (DRM_OK != DrmManager_activate(pDrmManager, false)) {
        printf("ERROR: Error activating DRM Manager object: %s\n", pDrmManager->error_message);
        err ++;        
    }
#endif
    license_type_expected = (nodelock)? std::string("Node-Locked") : std::string("Floating/Metering");
    err += check_drm_states(pDrmManager, !nodelock, license_type_expected);
    if (err || DrmAsyncError) {
        goto quit;
    }
    printf("[DRMLIB] DRM Session started (PL is unlocked)\n");
// ACCELIZE CODE TO UNLOCK APPLICATION ENDS HERE

    if (!nodelock) {
        // Check DRM events
        if (check_drm_event(pDrmManager, drmtest_evt_cnt)) {
            err ++;
            goto quit;
        }
        // Generate DRM events
        drmtest_evt_cnt += 7;
        if (generate_drm_event(drmtest_evt_cnt)) {
            err ++;
            goto quit;
        }
        // Check DRM events
        if (check_drm_event(pDrmManager, drmtest_evt_cnt)) {
            err ++;
            goto quit;
        }
    }
    
quit:


// ACCELIZE CODE TO LOCK APPLICATION STARTS HERE
    printf("[DRMLIB] Stopping DRM session ...\n");
#if __cplusplus
    try {                                                                                          
        pDrmManager->deactivate();
        err += check_drm_states(pDrmManager, false, "");
    } catch( const Exception& e ) {                                                                
        printf("DRM ERROR: %s\n", e.what());
        err ++;
    }
    delete pDrmManager;
#else
    if (DRM_OK != DrmManager_deactivate(pDrmManager, false)) {
        printf("ERROR: Error deactivating DRM Manager object: %s\n", pDrmManager->error_message);
        err ++;
    }
    err += check_drm_states(pDrmManager, false, "");    
    if (DrmManager_free(&pDrmManager)) {
        printf("ERROR: Failed to free DRM manager object: %s\n", pDrmManager->error_message);
        err ++;
    }
#endif
    printf("[DRMLIB] DRM Session stopped (PL is locked)\n");    
// ACCELIZE CODE TO LOCK APPLICATION ENDS HERE

    clReleaseProgram(program);
    clReleaseContext(context);

    // Release xclhal2 board handler
    uninit_register();

    err += DrmAsyncError;

    if (err == 0)
        printf("SUCCESS (%d)\n", err);
    else
        printf("!!! FAILURE (%d) !!!\n", err);
    
    return err;
}
