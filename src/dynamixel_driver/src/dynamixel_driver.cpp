#include "ros/ros.h"
#include "std_msgs/String.h"
#include "std_msgs/Float32MultiArray.h"
#include "dynamixel_driver/SetDynamixelPositions.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <bitset>
#include <string>

#include <chrono>
#include <thread>

#include <vector>

#include <dynamixel_sdk/dynamixel_sdk.h>
// https://github.com/ROBOTIS-GIT/DynamixelSDK/tree/master/c%2B%2B/src/dynamixel_sdk

using namespace std::chrono;

// Control table address   Control table -> http://support.robotis.com/en/product/actuator/dynamixel_x/xm_series/xm430-w350.html
const int ADDR_PRO_TORQUE_ENABLE = 64;
const int ADDR_PRO_GOAL_POS = 116;
#define ADDR_PRO_PRESENT_POSITION  132
const int ADDR_PRO_OPERATING_MODE = 11;
const int ADDR_PRO_GOAL_VEL = 104;
const int ADDR_PRO_PRESENT_VEL = 128;
const int ADDR_PRO_PRESENT_CURRENT = 126;
const int ADDR_PRO_GOAL_CUR = 102;
const int ADDR_PRO_HOMING_OFFSET = 20;

// Data Byte Length
const int LEN_PRO_GOAL_VEL = 4;
const int LEN_PRO_PRESENT_VEL = 4;
const int LEN_PRO_GOAL_POS = 4;
#define LEN_PRO_PRESENT_POSITION  4
const int LEN_PRO_PRESENT_CURRENT = 2;
const int LEN_PRO_GOAL_CUR = 2;
const int LEN_PRO_HOMING_OFFSET = 4;

// Protocol version
#define PROTOCOL_VERSION                2.0

// Default setting
#define DXL1_ID                         1
#define DXL2_ID                         2
#define BAUDRATE                        4000000
#define DEVICENAME                      "/dev/ttyUSB0"

#define TORQUE_ENABLE                   1                   // Value for enabling the torque
#define TORQUE_DISABLE                  0                   // Value for disabling the torque
#define DXL_MOVING_STATUS_THRESHOLD     25                  // Dynamixel moving status threshold
#define POS_CONTROL_MODE                3
#define VEL_CONTROL_MODE                1
#define CUR_CONTROL_MODE                0

#define DXL1_OFFSET                     2365 // 309
#define DXL2_OFFSET                     1855

#define DXL1_HOMING_OFFSET              1024

#define DXL1_MAX_POS                    1150  // 1570
#define DXL1_MIN_POS                    -750  // -583
#define DXL2_MAX_POS                    1200  // 3700
#define DXL2_MIN_POS                    -1200  // 0

int controlMode = -1;

// Init Port Handler and Packet Handler
dynamixel::PortHandler *portHandler = dynamixel::PortHandler::getPortHandler(DEVICENAME);
dynamixel::PacketHandler *packetHandler = dynamixel::PacketHandler::getPacketHandler(PROTOCOL_VERSION);

// Initialize GroupSyncWrite instance
dynamixel::GroupSyncWrite groupSyncWrite_Vel(portHandler, packetHandler, ADDR_PRO_GOAL_VEL, LEN_PRO_GOAL_VEL);
dynamixel::GroupSyncWrite groupSyncWrite_Pos(portHandler, packetHandler, ADDR_PRO_GOAL_POS, LEN_PRO_GOAL_POS);
dynamixel::GroupSyncWrite groupSyncWrite_Torq(portHandler, packetHandler, ADDR_PRO_GOAL_CUR, LEN_PRO_GOAL_CUR);

// Group Bulk Read
dynamixel::GroupBulkRead groupBulkRead(portHandler, packetHandler);

// Initialize Groupsyncread instance for Present Position
dynamixel::GroupSyncRead groupSyncRead_Pos(portHandler, packetHandler, ADDR_PRO_PRESENT_POSITION,
                                           LEN_PRO_PRESENT_POSITION);
dynamixel::GroupSyncRead groupSyncRead_Vel(portHandler, packetHandler, ADDR_PRO_PRESENT_VEL, LEN_PRO_PRESENT_VEL);
dynamixel::GroupSyncRead groupSyncRead_Cur(portHandler, packetHandler, ADDR_PRO_PRESENT_CURRENT,
                                           LEN_PRO_PRESENT_CURRENT);
//dynamixel::GroupSyncRead groupSyncRead_All(portHandler, packetHandler, ADDR_PRO_PRESENT_CURRENT, 2);


int32_t bulkRead(dynamixel::GroupBulkRead &gsr, uint8_t id, uint16_t addr, u_int16_t dataLen);

int32_t syncRead(dynamixel::GroupSyncRead &gsr, uint8_t id, uint16_t addr, u_int16_t dataLen);

void syncWrite(dynamixel::GroupSyncWrite &gsw, uint8_t id, int32_t data);

void setTorque(dynamixel::PacketHandler *paH, dynamixel::PortHandler *poH, uint8_t data);

void initRobot(dynamixel::PacketHandler *paH, dynamixel::PortHandler *poH, dynamixel::GroupSyncRead &gsrPos,
               dynamixel::GroupSyncRead &gsrVel, dynamixel::GroupSyncRead &gsrCur);

bool withinSafeZone(int32_t dxl1_pos, int32_t dxl2_pos);

void emergencyStop();

void setControlMode(int control_mode);

float fromPosToRad(int32_t dataPos);

int32_t fromRadToPos(float dataInRad);

float fromTickToRad(int32_t dataInTick);

int32_t fromRadToTick(float dataInRad);

float fromCurToTorq(int16_t dataInCur);

int16_t fromTorqToCur(float dataInTorq);

void setDynamixelCallbackVelocity(const std_msgs::Float32MultiArray::ConstPtr &msg) {
    if (controlMode != VEL_CONTROL_MODE) {
        setControlMode(VEL_CONTROL_MODE);
        controlMode = VEL_CONTROL_MODE;
    }

    syncWrite(groupSyncWrite_Vel, DXL1_ID, fromRadToTick(msg->data[0]));
    syncWrite(groupSyncWrite_Vel, DXL2_ID, fromRadToTick(msg->data[1]));

    // Syncwrite goal position
    int dxl_comm_result = groupSyncWrite_Vel.txPacket();
    if (dxl_comm_result != COMM_SUCCESS) printf(packetHandler->getTxRxResult(dxl_comm_result));

    // Clear syncwrite parameter storage
    groupSyncWrite_Vel.clearParam();
}

void setDynamixelCallbackTorque(const std_msgs::Float32MultiArray::ConstPtr &msg) {
    if (controlMode != CUR_CONTROL_MODE) {
        setControlMode(CUR_CONTROL_MODE);
        controlMode = CUR_CONTROL_MODE;
    }

    syncWrite(groupSyncWrite_Torq, DXL1_ID, fromTorqToCur(msg->data[0]));
    syncWrite(groupSyncWrite_Torq, DXL2_ID, fromTorqToCur(msg->data[1]));

    // Syncwrite goal position
    int dxl_comm_result = groupSyncWrite_Torq.txPacket();
    if (dxl_comm_result != COMM_SUCCESS) printf(packetHandler->getTxRxResult(dxl_comm_result));

    // Clear syncwrite parameter storage
    groupSyncWrite_Torq.clearParam();
}

bool setDynamixelService(dynamixel_driver::SetDynamixelPositions::Request &req,
                         dynamixel_driver::SetDynamixelPositions::Response &res) {

    setControlMode(POS_CONTROL_MODE);

    // Use input data req.inputPos[i] to get motor positions sendt
    int32_t dxl1_target_pos = (fromRadToPos(req.inputPos[0]) + DXL1_OFFSET);
    int32_t dxl2_target_pos = (fromRadToPos(req.inputPos[1]) + DXL2_OFFSET);

    syncWrite(groupSyncWrite_Pos, DXL1_ID, dxl1_target_pos);
    syncWrite(groupSyncWrite_Pos, DXL2_ID, dxl2_target_pos);
    // Syncwrite goal position
    int dxl_comm_result = groupSyncWrite_Pos.txPacket();
    if (dxl_comm_result != COMM_SUCCESS) printf(packetHandler->getTxRxResult(dxl_comm_result));
    groupSyncWrite_Pos.clearParam();

    int32_t dxl1_present_position = 0, dxl2_present_position = 0;

    do {
        try {
            if (!withinSafeZone(dxl1_target_pos - DXL1_OFFSET, dxl2_target_pos - DXL2_OFFSET)) {
                std::cerr << "Target Pos out of range" << std::endl;
                break;
            }

            // Syncread present position
            dxl_comm_result = groupSyncRead_Pos.txRxPacket();
            if (dxl_comm_result != COMM_SUCCESS) printf(packetHandler->getTxRxResult(dxl_comm_result));
            dxl1_present_position = syncRead(groupSyncRead_Pos, DXL1_ID, ADDR_PRO_PRESENT_POSITION,
                                             LEN_PRO_PRESENT_POSITION);
            dxl2_present_position = syncRead(groupSyncRead_Pos, DXL2_ID, ADDR_PRO_PRESENT_POSITION,
                                             LEN_PRO_PRESENT_POSITION);

        }
        catch (const char *error_msg) {
            std::cerr << error_msg << std::endl;
            break;
        }

    } while ((abs(dxl1_target_pos - dxl1_present_position) > DXL_MOVING_STATUS_THRESHOLD) ||
             (abs(dxl2_target_pos - dxl2_present_position) > DXL_MOVING_STATUS_THRESHOLD));

    // Response with a requst of some type res.outputPos[i]

    res.outputPos.push_back(fromPosToRad(dxl1_present_position - DXL1_OFFSET));
    res.outputPos.push_back(fromPosToRad(dxl2_present_position - DXL2_OFFSET));

    setControlMode(controlMode);

    // Return true when ready to send data back
    return true;
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "dynamixel_driver");

    ros::NodeHandle n;
    ros::ServiceServer service = n.advertiseService("dynamixel_set_positions_service", setDynamixelService);
    ros::Subscriber sub_vel = n.subscribe("dynamixel_set_velocity", 1000, setDynamixelCallbackVelocity);
    ros::Subscriber sub_torq = n.subscribe("dynamixel_set_torque", 1000, setDynamixelCallbackTorque);
    ros::Publisher pub_pos = n.advertise<std_msgs::Float32MultiArray>("dynamixel_present_position", 1000);
    ros::Publisher pub_vel = n.advertise<std_msgs::Float32MultiArray>("dynamixel_present_velocity", 1000);
    ros::Publisher pub_cur = n.advertise<std_msgs::Float32MultiArray>("dynamixel_present_torque", 1000);

    ros::Rate loop_rate(50);

    int dxl1_target_vel = 0, dxl2_target_vel = 0;
    int dxl_comm_result = COMM_TX_FAIL;
    int32_t dxl1_present_position = 0, dxl2_present_position = 0, dxl1_present_vel = 0, dxl2_present_vel = 0;
    int16_t dxl1_present_current = 0, dxl2_present_current = 0;

    try {
        initRobot(packetHandler, portHandler, groupSyncRead_Pos, groupSyncRead_Vel, groupSyncRead_Cur);
    }
    catch (const char *error_msg) {
        std::cerr << error_msg << std::endl;
        return 0;
    }

    setTorque(packetHandler, portHandler, TORQUE_ENABLE);


    steady_clock::time_point last_time = steady_clock::now();
    while (ros::ok()) {
        try {
            groupBulkRead.clearParam();
            int param_result = groupBulkRead.addParam(DXL1_ID, ADDR_PRO_PRESENT_CURRENT, 10);
            if (!param_result)
                throw "param res";
            param_result = groupBulkRead.addParam(DXL2_ID, ADDR_PRO_PRESENT_CURRENT, 10);
            if (!param_result)
                throw "param res";

            if (groupBulkRead.txRxPacket() != COMM_SUCCESS) {
                throw "tx rx packet";
            }

            dxl1_present_vel = bulkRead(groupBulkRead, DXL1_ID, ADDR_PRO_PRESENT_VEL, LEN_PRO_PRESENT_VEL);
            dxl2_present_vel = bulkRead(groupBulkRead, DXL2_ID, ADDR_PRO_PRESENT_VEL, LEN_PRO_PRESENT_VEL);
            dxl1_present_position =
                    bulkRead(groupBulkRead, DXL1_ID, ADDR_PRO_PRESENT_POSITION, LEN_PRO_PRESENT_POSITION) -
                    DXL1_OFFSET;
            dxl2_present_position =
                    bulkRead(groupBulkRead, DXL2_ID, ADDR_PRO_PRESENT_POSITION, LEN_PRO_PRESENT_POSITION) -
                    DXL2_OFFSET;
            dxl1_present_current = (int16_t) bulkRead(groupBulkRead, DXL1_ID, ADDR_PRO_PRESENT_CURRENT,
                                                      LEN_PRO_PRESENT_CURRENT);
            dxl2_present_current = (int16_t) bulkRead(groupBulkRead, DXL2_ID, ADDR_PRO_PRESENT_CURRENT,
                                                      LEN_PRO_PRESENT_CURRENT);

        } catch (const char *error_msg) {
            std::cerr << error_msg << std::endl;
            break;
        }

        if (!withinSafeZone(dxl1_present_position, dxl2_present_position)) {
            try {
                emergencyStop();
            }
            catch (const char *error_msg) {
                std::cerr << error_msg << std::endl;
            }
        }

        // Create msg
        std_msgs::Float32MultiArray msg_pos;
        std_msgs::Float32MultiArray msg_vel;
        std_msgs::Float32MultiArray msg_torq;
        // push in the data
        msg_pos.data.push_back(fromPosToRad(dxl1_present_position));
        msg_pos.data.push_back(fromPosToRad(dxl2_present_position));
        msg_vel.data.push_back(fromTickToRad(dxl1_present_vel));
        msg_vel.data.push_back(fromTickToRad(dxl2_present_vel));
        msg_torq.data.push_back(fromCurToTorq(dxl1_present_current));
        msg_torq.data.push_back(fromCurToTorq(dxl2_present_current));

        // publish to topic
        pub_pos.publish(msg_pos);
        pub_vel.publish(msg_vel);
        pub_cur.publish(msg_torq);

        ros::spinOnce();
        loop_rate.sleep();
    }

    setTorque(packetHandler, portHandler, TORQUE_DISABLE);
    portHandler->closePort();
    return 0;
}

int32_t fromRadToPos(float dataInRad) {
    return (dataInRad * 4096.) / (2. * 3.1415);
}

float fromPosToRad(int32_t dataPos) {
    return (dataPos * 2. * 3.1415) / 4096.;
}

float fromCurToTorq(int16_t dataInCur)
{
  return (dataInCur * 0.00269 * 1.667);
}

int16_t fromTorqToCur(float dataInTorq)
{
  return ((dataInTorq) / (0.00269 * 1.667));
}

float fromTickToRad(int32_t dataInTick) {
    return (dataInTick * 0.229 * 2. * 3.1415) / 60.;
}

int32_t fromRadToTick(float dataInRad) {
    return dataInRad * 60. / (0.229 * 2. * 3.1415);
}

bool withinSafeZone(int32_t dxl1_pos, int32_t dxl2_pos) {
    if (dxl1_pos > DXL1_MAX_POS || dxl1_pos < DXL1_MIN_POS || dxl2_pos > DXL2_MAX_POS || dxl2_pos < DXL2_MIN_POS)
        return false;
    else
        return true;
}

void emergencyStop() {
    setTorque(packetHandler, portHandler, TORQUE_DISABLE);
    //throw "EMERGENCY STOP -- TORQUE DISABLED!";
}

int32_t syncRead(dynamixel::GroupSyncRead &gsr, uint8_t id, uint16_t addr, u_int16_t dataLen) {
    // Check if groupsyncread data of is available
    bool dxl_getdata_result = gsr.isAvailable(id, addr, dataLen);
    if (dxl_getdata_result != true)
        throw "SyncRead Failed";
    else
        return gsr.getData(id, addr, dataLen);
}

int32_t bulkRead(dynamixel::GroupBulkRead &gsr, uint8_t id, uint16_t addr, u_int16_t dataLen) {
    // Check if groupsyncread data of is available
    bool dxl_getdata_result = gsr.isAvailable(id, addr, dataLen);
    if (dxl_getdata_result != true)
        throw "SyncRead Failed";
    else
        return gsr.getData(id, addr, dataLen);
}

void syncWrite(dynamixel::GroupSyncWrite &gsw, uint8_t id, int32_t data) {
    uint8_t dxl_param[4];
    // Allocate goal position value into byte array
    dxl_param[0] = DXL_LOBYTE(DXL_LOWORD(data));
    dxl_param[1] = DXL_HIBYTE(DXL_LOWORD(data));
    dxl_param[2] = DXL_LOBYTE(DXL_HIWORD(data));
    dxl_param[3] = DXL_HIBYTE(DXL_HIWORD(data));

    // Add Dynamixel#1 goal vel value to the Syncwrite storage
    bool dxl_addparam_result = gsw.addParam(id, dxl_param);
    if (dxl_addparam_result != true) {
        throw "SyncWrite Failed";
    }
}

void setTorque(dynamixel::PacketHandler *paH, dynamixel::PortHandler *poH, uint8_t data) {
    // Enable Dynamixel#1 Torque
    paH->write1ByteTxRx(poH, DXL1_ID, ADDR_PRO_TORQUE_ENABLE, data);
    // Enable Dynamixel#2 Torque
    paH->write1ByteTxRx(poH, DXL2_ID, ADDR_PRO_TORQUE_ENABLE, data);
    //paH->write1
    //std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void setControlMode(int control_mode) {
    uint8_t dxl_error;
    try {
        setTorque(packetHandler, portHandler, TORQUE_DISABLE);
        // Enable Position Controlmode #1
        packetHandler->write1ByteTxRx(portHandler, DXL1_ID, ADDR_PRO_OPERATING_MODE, control_mode, &dxl_error);
        if (dxl_error != COMM_SUCCESS)
            throw packetHandler->getRxPacketError(dxl_error);

        // Enable Position Controlmode #2
        packetHandler->write1ByteTxRx(portHandler, DXL2_ID, ADDR_PRO_OPERATING_MODE, control_mode, &dxl_error);
        if (dxl_error != COMM_SUCCESS)
            throw packetHandler->getRxPacketError(dxl_error);
    }
    catch (const char *error_msg) {
        std::cerr << error_msg << std::endl;
    }

    setTorque(packetHandler, portHandler, TORQUE_ENABLE);
}

void initRobot(dynamixel::PacketHandler *paH, dynamixel::PortHandler *poH, dynamixel::GroupSyncRead &gsrPos,
               dynamixel::GroupSyncRead &gsrVel, dynamixel::GroupSyncRead &gsrCur) {
    // Open port
    if (poH->openPort())
        std::cout << "Port opened successfully" << std::endl;
    else {
        throw "Failed to open port";
    }

    // Set port baudrate
    if (poH->setBaudRate(BAUDRATE))
        std::cout << "Baudrate set to: " << BAUDRATE << std::endl;
    else {
        throw "Failed to set Baudrate";
    }

    // Add parameter storage for Dynamixel#1 present position value
    bool dxl_addparam_result = gsrPos.addParam(DXL1_ID);
    if (!dxl_addparam_result)
        throw "addparam failed";

    // Add parameter storage for Dynamixel#2 present position value
    dxl_addparam_result = gsrPos.addParam(DXL2_ID);
    if (!dxl_addparam_result)
        throw "addparam failed";

    // Add parameter storage for Dynamixel#1 present velocity value
    dxl_addparam_result = gsrVel.addParam(DXL1_ID);
    if (!dxl_addparam_result)
        throw "addparam failed";

    // Add parameter storage for Dynamixel#2 present velocity value
    dxl_addparam_result = gsrVel.addParam(DXL2_ID);
    if (!dxl_addparam_result)
        throw "addparam failed";

    // Add parameter storage for Dynamixel#1 present current value
    dxl_addparam_result = gsrCur.addParam(DXL1_ID);
    if (!dxl_addparam_result)
        throw "addparam failed";

    // Add parameter storage for Dynamixel#2 present current value
    dxl_addparam_result = gsrCur.addParam(DXL2_ID);
    if (!dxl_addparam_result)
        throw "addparam failed";

    uint8_t dxl_error = 0;
    // Enable Velocity Controlmode #1
    paH->write1ByteTxRx(poH, DXL1_ID, ADDR_PRO_OPERATING_MODE, CUR_CONTROL_MODE, &dxl_error);
    if (dxl_error != COMM_SUCCESS)
        throw paH->getRxPacketError(dxl_error);

    // Enable Velocity Controlmode #2
    paH->write1ByteTxRx(poH, DXL2_ID, ADDR_PRO_OPERATING_MODE, CUR_CONTROL_MODE, &dxl_error);
    if (dxl_error != COMM_SUCCESS)
        throw paH->getRxPacketError(dxl_error);

    controlMode = CUR_CONTROL_MODE;

    std::cout << "Initialization Complete" << std::endl;
}
