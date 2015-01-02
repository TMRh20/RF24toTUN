/*
 * The MIT License (MIT)
 * Copyright (c) 2014 Rei <devel@reixd.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * The TUN/TAP interface code is based code from
 * http://backreference.org/2010/03/26/tuntap-interface-tutorial/
 *
 */

#ifndef __RF24TOTUN_H__
#define __RF24TOTUN_H__

/**
 *
 * @file RF24toTUN.h
 *
 */
#include "net/if_arp.h"
#include <cstdlib>
#include <stdio.h>
#include <cstdint>
#include <string>
#include <cstring>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include "ThreadSafeQueue.h"
#include "Message.h"
#include <RF24/RF24.h>
#include <RF24Network/RF24Network.h>

//#define USE_RF24MESH - This should be enabled when compiling by calling " make MESH=1 "

#if defined (USE_RF24MESH)
  #include <RF24Mesh/RF24Mesh.h>
#endif

#define PRINT_DEBUG 0

#ifndef IFF_MULTI_QUEUE
	#define IFF_MULTI_QUEUE 0x0100
#endif

#define MAX_TUN_BUF_SIZE (10 * 1024) // should be enough for now

#ifndef MAX_FRAME_SIZE
    #define MAX_FRAME_SIZE 32   /**<The NRF24L01 frames are only 32Bytes long */
#endif
#ifndef MAX_PAYLOAD_SIZE
    #define MAX_PAYLOAD_SIZE 1500 /**<The maximal payload size to be sent over the air. */
#endif

/**
 * Radio configuration settings
 */
//         CE Pin,            CSN Pin,           SPI Speed
RF24 radio(RPI_V2_GPIO_P1_15, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ);
RF24Network network(radio);
uint16_t thisNodeAddr; /**< Address of our node in Octal format (01,021, etc) */
uint16_t otherNodeAddr;     /**< Address of the other node */
const uint8_t channel = 97;
unsigned long packets_sent;  /**< How many have we sent already */

#if defined (USE_RF24MESH)
RF24Mesh mesh(radio,network);
#endif

/**
 * Thread stuff
 */
ThreadSafeQueue< Message > radioRxQueue;
ThreadSafeQueue< Message > radioTxQueue;

boost::scoped_ptr< boost::thread > radioRxTxThread;
boost::scoped_ptr< boost::thread > tunRxThread;
boost::scoped_ptr< boost::thread > tunTxThread;

/**
* TUN/TAP variabled
*/
char tunName[IFNAMSIZ];
int tunFd;

uint8_t config_TUN;
uint8_t mesh_enabled;
uint16_t RF24Network_address;
uint8_t mesh_nodeID;
rf24_datarate_e dataRate;

/**
* Configuration and setup of the NRF24 radio
*
* @return True if the radio was configured successfully
*/
bool configureAndSetUpRadio();

/**
* Configure, set up and allocate the TUN/TAP device.
*
* If the TUN/TAP device was allocated successfully the file descriptor is returned.
* Otherwise the application closes with an error.
* @note Currently only a TUN device with multi queue is used.
*
* @return The TUN/TAP device file descriptor
*/
int configureAndSetUpTunDevice(uint16_t address);

/**
* Allocate the TUN/TAP device (if needed)
*
* The TUN/TAP interface code is based code from
* http://backreference.org/2010/03/26/tuntap-interface-tutorial/
*
* @param *dev the name of an interface (or '\0'). MUST have enough space to hold the interface name if '\0' is passed
* @param flags interface flags (eg, IFF_TUN etc.)
* @param address RF24Network address of the node, used to set the MAC address of the interface.
* @return The file descriptor for the allocated interface or -1 is an error ocurred
*/
int allocateTunDevice(char *dev, int flags, uint16_t address);

/**
* The thread function in charge receiving and transmitting messages with the radio.
* The received messages from RF24Network and NRF24L01 device and enqueued in the rxQueue and forwaded to the TUN/TAP device.
* The messages from the TUN/TAP device (in the txQueue) are sent to the RF24Network lib and transmited over the air.
*
* @note Optimization: Use two thread for rx and tx with the radio, but thread synchronisation and semaphores are needed.
*       It may increase the throughput.
*/
void radioRxTxThreadFunction();

/**
* Thread function in charge of reading, framing and enqueuing the packets from the TUN/TAP interface in the RxQueue.
*
* This thread uses "select()" with timeout to avoid a busy waiting
*/
void tunRxThreadFunction();

/**
* This thread function waits for incoming messages from the radio and forwards them to the TUN/TAP interface.
*
* This threads blocks until a message is received avoiding busy waiting.
*/
void tunTxThreadFunction();

/**
* Projecure to wait and join all the threads.
*
* @note If an interrupt was not before called, this procedure may block for ever.
*/
void joinThreads();

/**
* This procedure is called before terminating the programm to properly close and terminate all the threads and file handlers.
*
*/
void on_exit();

/**
* Debug output of the given message buffer preceeded by the debugMsg.
*
* @param buffer The std::string with the message to debug.
* @param debugMsg The info message preceeding the buffer.
*/
void printPayload(std::string buffer, std::string debugMsg);

/**
* Debug output of the given message buffer preceeded by the debugMsg.
*
* @param buffer The char array with the message to debug.
* @param nread The length of the message buffer.
* @param debugMsg The info message preceeding the buffer.
*/
void printPayload(char *buffer, int nread, std::string debugMsg);

#endif // __RF24TOTUN_H__
