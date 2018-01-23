#include "vmu_utils.h"
#include "lcmtypes/vmu931_data_imu.h"

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <lcm/lcm.h>


int connect_to_serial_port(char device_filename[]) {
    struct termios options;

    int fd = open(device_filename, O_RDWR | O_SYNC | O_NOCTTY);

    while( fd < 0 ) {
        // printf("WARN: Failed to connect to %s. Retrying...\n", device_filename);
        fd = open(device_filename, O_RDWR | O_SYNC | O_NOCTTY);
        sleep(1);
    }

    tcgetattr(fd, &options);
    memset(&options, 0, sizeof(options));

    options.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
    options.c_iflag = IGNPAR | ICRNL;
    options.c_oflag = 0;
    options.c_lflag = 0;

    options.c_cc[VINTR] = 0;
    options.c_cc[VQUIT] = 0;
    options.c_cc[VERASE] = 0;
    options.c_cc[VKILL] = 0;
    options.c_cc[VEOF] = 0;
    options.c_cc[VTIME] = 0;
    options.c_cc[VMIN] = 0;
    options.c_cc[VSWTC] = 0;

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &options);

    return fd;
}


// Return -1 on error. Otherwise, return the number of bytes received.
int load_msg(int vmu_fd) {
    unsigned char byte;
    unsigned char msg_size;
    unsigned char data[VMU_RAW_MESS_SIZE];
    unsigned char  msg[VMU_RAW_MESS_SIZE];
    MessType string_or_data_msg;

    while( read(vmu_fd, &byte, 1) ) {

        // Search for the start of a data message.
        if( byte == VMU_DATA_START ) {

            // Read in the length of the message.
            if( !read(vmu_fd, &msg_size, 1) ) {
            //    printf("WARN: Failed to read length of message.\n"); 
               return -1;
            }

            // If the message is too short to contain any data, return an error. 
            if( msg_size < 2 ) { return -1; }

            if( !read(vmu_fd, &data, msg_size-2) ) {
                // printf("WARN: Failed to read message data.\n");
                return -1;
            }

            // Check the message footer.
            if( data[msg_size-3] != VMU_DATA_END ) {
                continue;
            }

            msg[VMU_START_BYTE_OFFS] = VMU_DATA_START;
            msg[VMU_SIZE_OFFS] = msg_size;
            memcpy(&msg[VMU_SIZE_OFFS+1], data, msg_size-2);

            string_or_data_msg = mt_data;

            // Load the message for future use.
            if( VMU_ERR_SUCCESS != vmutils_loadMessage(msg, msg_size, &string_or_data_msg) ) {
                // printf("WARN: Failed to load message.\n");
                return -1;
            }
            return msg_size;

        } else if( byte == VMU_STRING_START) {

            // Read in the length of the message.
            if( !read(vmu_fd, &msg_size, 1) ) {
                // printf("WARN: Failed to read length of message.\n");
                return -1;
            }

            // If the message is too short to contain any data, return an error. 
            if( msg_size < 2 ) { return -1; }
            
            if( !read(vmu_fd, &data, msg_size-2) ) {
                // printf("WARN: Failed to read string from message.\n");
                return -1;
            }

            // Check the message footer.
            if( data[msg_size-3] != VMU_STRING_END ) {
               return -1; 
            }

            msg[VMU_START_BYTE_OFFS] = VMU_STRING_START;
            msg[VMU_SIZE_OFFS] = msg_size;
            memcpy(&msg[VMU_SIZE_OFFS+1], data, msg_size-2);

            string_or_data_msg = mt_string;

            // Load the message for future use.
            if( VMU_ERR_SUCCESS != vmutils_loadMessage(msg, msg_size, &string_or_data_msg) ) {
                // printf("WARN: Failed to load message. %d\n", vmutils_loadMessage(msg, msg_size, &string_or_data_msg));
                return -1;
            }
            return msg_size;

        }
    }
    return -1;
}

int send_cmd(int vmu_fd, Cmd cmd) {
    char cmd_buffer[VMU_CMD_NUM_BYTES];

    if( vmutils_buildCmd(cmd, cmd_buffer) != VMU_ERR_SUCCESS) {
        // printf("WARN: Failed to build command.\n");
        return -1;
    }


    int i;
    for( i=0; i<VMU_CMD_NUM_BYTES; ++i ) {
        int success = write(vmu_fd, &cmd_buffer[i], 1);
        usleep(100);
    }
    return 1;

}

int update_status(int vmu_fd) {

    int msg_size, err_code, retry_count;

    // Send status requests until you get a response.
    while(1) {
        err_code = send_cmd(vmu_fd, cmd_req_status);
        if( -1 == err_code ) { continue; }

        retry_count = 1000;
        while(retry_count-- > 0) {
            msg_size = load_msg(vmu_fd);
            if( msg_size == -1 ) { continue; } 
            
            if( singleMessage.messType == mt_data && vmutils_retrieveDataType() == dt_status ) {
                // Got a status message!
                return 0;
            }
            usleep(1000);
        }
    }
}

int set_sensors_status(int vmu_fd, char accel_en, char euler_en, char gyro_en,
                                   char heading_en, char mag_en, char quat_en) {
    Data_status status;
    update_status(vmu_fd);
    vmutils_retrieveStatus(&status);


    char accel_en_   = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_ACCEL   ) != 0;
    char euler_en_   = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_EULER   ) != 0;
    char gyro_en_    = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_GYRO    ) != 0;
    char heading_en_ = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_HEADING ) != 0;
    char mag_en_     = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_MAG     ) != 0;
    char quat_en_    = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_QUAT    ) != 0;

    char keep_going = 1;
    while( keep_going == 1 ) {

        keep_going = 0;

        if(   accel_en_ !=   accel_en ) { send_cmd(vmu_fd, cmd_toggle_accel); keep_going = 1; }
        if(   euler_en_ !=   euler_en ) { send_cmd(vmu_fd, cmd_toggle_euler); keep_going = 1; }
        if(    gyro_en_ !=    gyro_en ) { send_cmd(vmu_fd, cmd_toggle_gyro);  keep_going = 1; }
        if( heading_en_ != heading_en ) { send_cmd(vmu_fd, cmd_toggle_heading); keep_going = 1; }
        if(     mag_en_ !=     mag_en ) { send_cmd(vmu_fd, cmd_toggle_mag); keep_going = 1; }
        if(    quat_en_ !=    quat_en ) { send_cmd(vmu_fd, cmd_toggle_quat); keep_going = 1; }


        if( keep_going == 0 ) { break; }

        usleep(1000000);
        update_status(vmu_fd);
        vmutils_retrieveStatus(&status);

        accel_en_   = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_ACCEL   ) != 0;
        euler_en_   = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_EULER   ) != 0;
        gyro_en_    = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_GYRO    ) != 0;
        heading_en_ = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_HEADING ) != 0;
        mag_en_     = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_MAG     ) != 0;
        quat_en_    = ( status.data_streaming_array[3] & VMU_STATUS_MASK_STREAM_QUAT    ) != 0;

    }
    return 1;
}


int main(char argc, char** argv) {

    int err_code;
    char device_filename[] = "/dev/VMU931";

    Data_xyz    data_accel;
    Data_xyz    data_euler;
    Data_xyz    data_gyro;
    Data_h      data_heading;
    Data_xyz    data_mag;
    Data_wxyz   data_quat;

    lcm_t * lcm = lcm_create(NULL);

    if(!lcm)
    {
        printf("Error: Failed to create LCM.");
        return 1;
    }

    vmu931_data_imu data_imu;

    int vmu_fd = connect_to_serial_port(device_filename);
    printf("Connected!\n");

    set_sensors_status(vmu_fd, 1, 1, 1, 1, 1, 1);

    while (1) {
        int msg_length = load_msg(vmu_fd);
        if( msg_length <= 0 ) { continue; }

        DataType data_type = vmutils_retrieveDataType();

        switch( data_type ) {
            case dt_accel:
                // printf("A\n");
                vmutils_retrieveXYZData(&data_accel);
                data_imu.data_accel.timestamp = data_accel.timestamp;
                data_imu.data_accel.x = data_accel.x;
                data_imu.data_accel.y = data_accel.y;
                data_imu.data_accel.z = data_accel.z;
                vmu931_data_imu_publish(lcm, "VMU931", &data_imu);
                break;
            case dt_euler:
                // printf("E\n");
                vmutils_retrieveXYZData(&data_euler);
                data_imu.data_euler.timestamp = data_euler.timestamp;
                data_imu.data_euler.x = data_euler.x;
                data_imu.data_euler.y = data_euler.y;
                data_imu.data_euler.z = data_euler.z;
                vmu931_data_imu_publish(lcm, "VMU931", &data_imu);
                break;
            case dt_gyro:
                // printf("G\n");
                vmutils_retrieveXYZData(&data_gyro);
                data_imu.data_gyro.timestamp = data_gyro.timestamp;
                data_imu.data_gyro.x = data_gyro.x;
                data_imu.data_gyro.y = data_gyro.y;
                data_imu.data_gyro.z = data_gyro.z;
                vmu931_data_imu_publish(lcm, "VMU931", &data_imu);
                break;
            case dt_heading:
                // printf("H\n");
                vmutils_retrieveHData(&data_heading);
                data_imu.data_heading.timestamp = data_heading.timestamp;
                data_imu.data_heading.h = data_heading.h;
                vmu931_data_imu_publish(lcm, "VMU931", &data_imu);
                break;
            case dt_mag:
                // printf("M\n");
                vmutils_retrieveXYZData(&data_mag);
                data_imu.data_mag.timestamp = data_mag.timestamp;
                data_imu.data_mag.x = data_mag.x;
                data_imu.data_mag.y = data_mag.y;
                data_imu.data_mag.z = data_mag.z;
                vmu931_data_imu_publish(lcm, "VMU931", &data_imu);
                break;
            case dt_quat:
                // printf("Q\n");
                vmutils_retrieveWXYZData(&data_quat);
                data_imu.data_quat.timestamp = data_quat.timestamp;
                data_imu.data_quat.w = data_quat.w;
                data_imu.data_quat.x = data_quat.x;
                data_imu.data_quat.y = data_quat.y;
                data_imu.data_quat.z = data_quat.z;
                vmu931_data_imu_publish(lcm, "VMU931", &data_imu);
                break;
            default:
                break;
        }

    }

    close(vmu_fd);
    lcm_destroy(lcm);
    return 0;
}