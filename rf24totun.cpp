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
 */

#include "rf24totun.h"
#if defined (USE_RF24MESH)
  #include <RF24Mesh/RF24Mesh.h>
#endif
#include <unistd.h>

/**
* Configuration and setup of the NRF24 radio
*
* @return True if the radio was configured successfully
*/
bool configureAndSetUpRadio() {

    #if defined (USE_RF24MESH)
	if(mesh_enabled){
	  if(!thisNodeAddr){	  
	     mesh.setNodeID(0);
	  }else{
		if(!mesh_nodeID){
			mesh_nodeID = 253;
		}
		mesh.setNodeID(mesh_nodeID); //Try not to conflict with any low-numbered node-ids
	  }
	  mesh.setChannel(channel);
	  mesh.begin();
	}else{
	#endif
	  radio.begin();
      delay(5);
      const uint16_t this_node = thisNodeAddr;
      network.begin(/*channel*/ channel, /*node address*/ this_node);
	#if defined (USE_RF24MESH)  
	}
	#endif
	network.multicastRelay=1;
	radio.setDataRate(dataRate);
    if (PRINT_DEBUG >= 1) {
        radio.printDetails();
    }

    return true;
}

/**
* Configure, set up and allocate the TUN/TAP device.
*
* If the TUN/TAP device was allocated successfully the file descriptor is returned.
* Otherwise the application closes with an error.
* @note Currently only a TUN device with multi queue is used.
*
* @return The TUN/TAP device file descriptor
*/
int configureAndSetUpTunDevice(uint16_t address) {

    std::string tunTapDevice = "tun_nrf24";
    strcpy(tunName, tunTapDevice.c_str());

	int flags;
	if(config_TUN){
      flags = IFF_TUN | IFF_NO_PI | IFF_MULTI_QUEUE;
	}else{
	  flags = IFF_TAP | IFF_NO_PI | IFF_MULTI_QUEUE;
    }
	tunFd = allocateTunDevice(tunName, flags, address);
    if (tunFd >= 0) {
        std::cout << "Successfully attached to tun/tap device " << tunTapDevice << std::endl;
    } else {
        std::cerr << "Error allocating tun/tap interface: " << tunFd << std::endl;
        exit(1);
    }

    return tunFd;
}

/**
* Allocate the TUN/TAP device (if needed)
*
* The TUN/TAP interface code is based code from
* http://backreference.org/2010/03/26/tuntap-interface-tutorial/
*
* @param *dev the name of an interface (or '\0'). MUST have enough space to hold the interface name if '\0' is passed
* @param flags interface flags (eg, IFF_TUN etc.)
* @return The file descriptor for the allocated interface or -1 is an error ocurred
*/
int allocateTunDevice(char *dev, int flags, uint16_t address) {
    struct ifreq ifr;
    int fd;

    /* open the clone device */
    if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
       return fd;
    }

    // preparation of the struct ifr, of type "struct ifreq"
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;   // IFF_TUN or IFF_TAP, plus maybe IFF_NO_PI

    if (*dev) {
        // if a device name was specified, put it in the structure; otherwise,
        // the kernel will try to allocate the "next" device of the
        // specified type
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    // try to create the device
    if(ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
        //close(fd);
        std::cerr << "Error: enabling TUNSETIFF" << std::endl;
		std::cerr << "If changing from TAP/TUN, run 'sudo ip link delete tun_nrf24' to remove the interface" << std::endl;
        return -1;
    }

    //Make interface persistent
    if(ioctl(fd, TUNSETPERSIST, 1) < 0){
        std::cerr << "Error: enabling TUNSETPERSIST" << std::endl;
        return -1;
    }

	if(!config_TUN){
	  struct sockaddr sap;
      sap.sa_family = ARPHRD_ETHER;
      ((char*)sap.sa_data)[4]=address;
      ((char*)sap.sa_data)[5]=address>>8;
      ((char*)sap.sa_data)[0]=0x52;
      ((char*)sap.sa_data)[1]=0x46;
      ((char*)sap.sa_data)[2]=0x32;
      ((char*)sap.sa_data)[3]=0x34;
	
	  //printf("Address 0%o first %u last %u\n",address,sap.sa_data[0],sap.sa_data[1]);
      memcpy((char *) &ifr.ifr_hwaddr, (char *) &sap, sizeof(struct sockaddr));	

      if (ioctl(fd, SIOCSIFHWADDR, &ifr) < 0) {
        fprintf(stderr, "TAP: failed to set MAC address\n");
      }
	}
    // if the operation was successful, write back the name of the
    // interface to the variable "dev", so the caller can know
    // it. Note that the caller MUST reserve space in *dev (see calling
    // code below)
    strcpy(dev, ifr.ifr_name);

    // this is the special file descriptor that the caller will use to talk
    // with the virtual interface
    return fd;
}

/**
* The thread function in charge receiving and transmitting messages with the radio.
* The received messages from RF24Network and NRF24L01 device and enqueued in the rxQueue and forwaded to the TUN/TAP device.
* The messages from the TUN/TAP device (in the txQueue) are sent to the RF24Network lib and transmited over the air.
*
* @note Optimization: Use two thread for rx and tx with the radio, but thread synchronisation and semaphores are needed.
*       It may increase the throughput.
*/
void radioRxTxThreadFunction() {
 uint32_t timer = millis();
 
    while(1) {
    try {
        /*if(millis() - timer > 5000){	 
			 radioRxTxThread->interrupt();
		}*/
		
			
		//delay(1);
		if(mesh_enabled){
		#if defined(USE_RF24MESH)
		  if(mesh_enabled){
			mesh.update();
			if(!thisNodeAddr){
				mesh.DHCP();
			}
		  }
		#endif
		}else{
           network.update();
        }
         //RX section
         
        while ( network.available() ) { // Is there anything ready for us?

            RF24NetworkHeader header;        // If so, grab it and print it out
            Message msg;
            uint8_t buffer[MAX_PAYLOAD_SIZE];

            unsigned int bytesRead = network.read(header,buffer,MAX_PAYLOAD_SIZE);
            if (bytesRead > 0) {
                msg.setPayload(buffer,bytesRead);
                if (PRINT_DEBUG >= 1) {
                    std::cout << "Radio: Received "<< bytesRead << " bytes ... " << std::endl;
                }
                if (PRINT_DEBUG >= 3) {
                    printPayload(msg.getPayloadStr(),"radio RX");
                }
                radioRxQueue.push(msg);
            } else {
                std::cerr << "Radio: Error reading data from radio. Read '" << bytesRead << "' Bytes." << std::endl;
            }
        } //End RX

		//delay(1);
		
		if(mesh_enabled){
        #if defined(USE_RF24MESH)
			mesh.update();
			if(!thisNodeAddr){
				mesh.DHCP();
			}
		#endif
		}else{
            network.update();
		}
		if(dataRate == RF24_2MBPS){
		 delayMicroseconds(1000);
		}else
		if(dataRate == RF24_1MBPS){
		 delayMicroseconds(1700);
		}else
		if(dataRate == RF24_250KBPS){
		 delayMicroseconds(4500);
		}
		network.update();
         // TX section
        
		bool ok = 0;
		boost::this_thread::interruption_point();
        while(!radioTxQueue.empty() && !radio.available() && !network.available()) {
			
			
            Message msg = radioTxQueue.pop();

            if (PRINT_DEBUG >= 1) {
                std::cout << "Radio: Sending "<< msg.getLength() << " bytes ... ";
            }
            if (PRINT_DEBUG >= 3) {
                std::cout << std::endl; //PrintDebug == 1 does not have an endline.
                printPayload(msg.getPayloadStr(),"radio TX");
            }
			
			uint8_t *tmp = msg.getPayload();
			/*printf("********WRITING************\n");
			for(int i=0; i<8; i++){
				printf("0%#x\n",tmp[i]);
			}*/
			
			//tmp = msg.getPayload();
		  
		  
		  if(!config_TUN ){ //TAP can use RF24Mesh for address assignment, but will still use ARP for address resolution
		
			uint32_t RF24_STR = 0x34324652; //Identifies the mac as an RF24 mac
			uint32_t ARP_BC = 0xFFFFFFFF;   //Broadcast address
			struct macStruct{				
				uint32_t rf24_Verification;
				uint16_t rf24_Addr;
			};
			
		
			macStruct macData;
			memcpy(&macData.rf24_Addr,tmp+4,2);
			memcpy(&macData.rf24_Verification,tmp,4);
			
			
			if(macData.rf24_Verification == RF24_STR){
				const uint16_t other_node = macData.rf24_Addr;			
				RF24NetworkHeader header(/*to node*/ other_node, EXTERNAL_DATA_TYPE);
				ok = network.write(header,msg.getPayload(),msg.getLength());

			}else
			if(macData.rf24_Verification == ARP_BC){
				RF24NetworkHeader header(/*to node*/ 00, EXTERNAL_DATA_TYPE); //Set to master node, will be modified by RF24Network if multi-casting
			  if(msg.getLength() <= 42){	
				if(thisNodeAddr == 00){ //Master Node
				
				    uint32_t arp_timeout = millis();
					
					ok=network.multicast(header,msg.getPayload(),msg.getLength(),1 ); //Send to Level 1
					while(millis() - arp_timeout < 5){network.update();}
					network.multicast(header,msg.getPayload(),msg.getLength(),1 ); //Send to Level 1					
					arp_timeout=millis();
					while(millis()- arp_timeout < 15){network.update();}
					network.multicast(header,msg.getPayload(),msg.getLength(),1 ); //Send to Level 1					

				}else{
					ok = network.write(header,msg.getPayload(),msg.getLength());					
				}
			  }
			}
		  }else{ // TUN always needs to use RF24Mesh for address assignment AND resolution
		     #if defined (USE_RF24MESH)
			   uint8_t lastOctet = tmp[19];
			   uint8_t meshAddr;
			 
			  if ( (meshAddr = mesh.getAddress(lastOctet)) > 0) {
			    RF24NetworkHeader header(meshAddr, EXTERNAL_DATA_TYPE);
			    ok = network.write(header, msg.getPayload(), msg.getLength());
			  }
			 
			 #endif
		  
		  }
		
		

			//printf("Addr: 0%#x\n",macData.rf24_Addr);
			//printf("Verif: 0%#x\n",macData.rf24_Verification);
            if (ok) {
               // std::cout << "ok." << std::endl;
            } else {
                std::cerr << "failed." << std::endl;
            }
			
        } //End Tx


    } catch(boost::thread_interrupted&) {
        std::cerr << "radioRxThreadFunction is stopped" << std::endl;
		//exit(EXIT_SUCCESS);
        return;
    }
    }
}

/**
* Thread function in charge of reading, framing and enqueuing the packets from the TUN/TAP interface in the RxQueue.
*
* This thread uses "select()" with timeout to avoid a busy waiting
*/
void tunRxThreadFunction() {

    fd_set socketSet;
    struct timeval selectTimeout;
    uint8_t buffer[MAX_TUN_BUF_SIZE];
    int nread;

    while(1) {
    try {

        // reset socket set and add tap descriptor
        FD_ZERO(&socketSet);
        FD_SET(tunFd, &socketSet);

        // initialize timeout
        selectTimeout.tv_sec = 1;
        selectTimeout.tv_usec = 0;

        // suspend thread until we receive a packet or timeout
        if (select(tunFd + 1, &socketSet, NULL, NULL, &selectTimeout) != 0) {
            if (FD_ISSET(tunFd, &socketSet)) {
                if ((nread = read(tunFd, buffer, MAX_TUN_BUF_SIZE)) >= 0) {

                    if (PRINT_DEBUG >= 1) {
                        std::cout << "Tun: Successfully read " << nread  << " bytes from tun device" << std::endl;
                    }
                    if (PRINT_DEBUG >= 3) {
                        //printPayload(std::string(buffer, nread),"Tun read");
                    }
					
					/*for(int i=0; i<nread; i++){
						//std::cout << std::hex << buffer[i] <<std::endl;
						printf("%#x\n",(uint8_t)buffer[i]);
					}*/

                    // copy received data into new Message
                    Message msg;
                    msg.setPayload(buffer,nread);

                    // send downwards
					/*while(radioTxQueue.size() > 2){
						boost::this_thread::sleep(boost::posix_time::milliseconds(1));
					}*/
					if(radioTxQueue.size() < 5){ // 150kB max queue size
						radioTxQueue.push(msg);
					}else{
					  std::cout << "**** Tun Drop ****" << std::endl;
					}

                } else
                    std::cerr << "Tun: Error while reading from tun/tap interface." << std::endl;
            }
        }

    boost::this_thread::interruption_point();
    } catch(boost::thread_interrupted&) {
        std::cerr << "tunRxThreadFunction is stopped" << std::endl;
        return;
    }
    }
}

/**
* This thread function waits for incoming messages from the radio and forwards them to the TUN/TAP interface.
*
* This threads blocks until a message is received avoiding busy waiting.
*/
void tunTxThreadFunction() {
    while(1) {
    try {
        //Wait for Message from radio
        Message msg = radioRxQueue.pop();

        //assert(msg.getLength() <= MAX_TUN_BUF_SIZE);
        if(msg.getLength() > MAX_TUN_BUF_SIZE){
			printf("*****WTF OVER *****");
			return;
		}
		
        if (msg.getLength() > 0) {

            size_t writtenBytes = write(tunFd, msg.getPayload(), msg.getLength());
			
			uint32_t timeout = millis();
			while(!writtenBytes){  
				delay(3);
				writtenBytes = write(tunFd, msg.getPayload(), msg.getLength()); 
				if(millis()-timeout > 500){
					//Timeout after .5 seconds
					break;
				}
			}
            if (writtenBytes != msg.getLength()) {
                std::cerr << "Tun: Less bytes written to tun/tap device then requested." << std::endl;
            } else {
                if (PRINT_DEBUG >= 1) {
                    std::cout << "Tun: Successfully wrote " << writtenBytes  << " bytes to tun device" << std::endl;
                }
            }

            if (PRINT_DEBUG >= 3) {
                printPayload(msg.getPayloadStr(),"tun write");
            }

        }

	boost::this_thread::interruption_point();
    } catch(boost::thread_interrupted&) {
        std::cerr << "tunTxThreadFunction is stopped" << std::endl;
        return;
    }
    }
}

/**
* Debug output of the given message buffer preceeded by the debugMsg.
*
* @param buffer The std::string with the message to debug.
* @param debugMsg The info message preceeding the buffer.
*/
void printPayload(std::string buffer, std::string debugMsg = "") {

    std::cout << "********************************************************************************" << std::endl
    << debugMsg << std::endl
    << "Buffer size: " << buffer.size() << " bytes" << std::endl
    << std::hex << buffer << std::endl
    << "********************************************************************************" << std::endl;
}

/**
* Debug output of the given message buffer preceeded by the debugMsg.
*
* @param buffer The char array with the message to debug.
* @param nread The length of the message buffer.
* @param debugMsg The info message preceeding the buffer.
*/
void printPayload(char *buffer, int nread, std::string debugMsg = "") {

    std::cout << "********************************************************************************" << std::endl
    << debugMsg << std::endl
    << "Buffer size: " << nread << " bytes" << std::endl
    << std::hex << std::string(buffer,nread) << std::endl
    << std::cout << "********************************************************************************" << std::endl;
}

void terminate(int signum){
	if(signum == SIGTERM){
		std::cout << "Terminate" << std::endl;
		exit(EXIT_SUCCESS);
	}
}

/**
* This procedure is called before terminating the programm to properly close and terminate all the threads and file handlers.
*
*/
void on_exit() {
    std::cout << "Cleaning up and exiting" << std::endl;
    
    if (radioRxTxThread) {
        radioRxTxThread->interrupt();
		//radioRxTxThread->join();
    }

	if (tunRxThread) {
        tunRxThread->interrupt();
		//tunRxThread->join();
    }

    if (tunTxThread) {
        tunTxThread->interrupt();
		//tunTxThread->join();

	}
	
	//Wait for threads to finish
	sleep(1);
    if (tunFd >= 0)
        close(tunFd);
}

/**
* Projecure to wait and join all the threads.
*
* @note If an interrupt was not before called, this procedure may block for ever.
*/
void joinThreads() {
    if (tunRxThread) {
        tunRxThread->join();
    }

    if (tunTxThread) {
        tunTxThread->join();
    }

    if (radioRxTxThread) {
        radioRxTxThread->join();
    }
}

/**
* Main
*
* @TODO better address management
*
* @param argc
* @param **argv
* @return Exit code
*/

void showhelpinfo(char *s)
{
  std::cout<<"RF24toTUN Usage Information"<<std::endl
  <<"Usage:   "<<s<<" [-option] [argument]"<<std::endl
  <<"option:  "<<"-a  RF24Network address to use: (00) -DEFAULT-  Address specified in Octal format ie: 01, 011"<<std::endl
  <<"         "<<"-t  Configure as TUN -Requires RF24Mesh- or TAP -DEFAULT- "<<std::endl
  <<"         "<<"-m  Use RF24Mesh for address resolution (TUN) and assignment(TUN/TAP)"<<std::endl
  <<"                 -Default if TUN is enabled-"<<std::endl
  <<"         "<<"-i  Config RF24Mesh nodeID (253) -DEFAULT- for non-master nodes "<<std::endl
  <<"                 Note: Last octet of IP must = nodeID"<<std::endl
  <<"         "<<"-d  Config RF24 radio datarate 1(1MBPS) 2(2MBPS) 250(250KBPS) "<<std::endl
  <<"         "<<"-h  show help information"<<std::endl
  <<std::endl<<"### Examples: ###"<<std::endl
  <<"Master Node w/TAP: ./rf24totun -a00 "<<std::endl
  <<"Child Node w/TAP: ./rf24totun -a01 "<<std::endl
  <<"Master Node w/TUN w/RF24Mesh: ./rf24totun -t "<<std::endl
  <<"Child Node w/TUN w/RF24Mesh NodeId 22: ./rf24totun -t -i22 "<<std::endl;
}

int main(int argc, char **argv) {

	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = terminate;
	sigaction(SIGTERM, &action, NULL);
	

	int tmp;
	uint8_t rate;
	dataRate = RF24_1MBPS;
	while ((tmp=getopt(argc,argv,"tma:i:d:"))!=-1)
    switch (tmp)
      {  
         case 't': config_TUN = 1;
		           mesh_enabled = 1;		 
				   #if !defined(USE_RF24MESH)
				     printf("*** Recompile with make MESH=1 option to enable RF24Mesh ***\n");
				     return 0;
				   #endif				   
		           break;
         case 'm': mesh_enabled = 1;
                   #if !defined(USE_RF24MESH)
				     printf("*** Recompile with make MESH=1 option to enable RF24Mesh ***\n");
				     return 0;
				   #endif
				   break;
         case 'a': thisNodeAddr = strtol(optarg,NULL,8);
				   if(!network.is_valid_address(thisNodeAddr)){
					 printf("Invalid address specified\n");
					 return 0;
				   }
  				   break;
		 case 'i': mesh_nodeID = atoi(optarg);
                   #if !defined(USE_RF24MESH)
				     printf("*** Recompile with make MESH=1 option to enable RF24Mesh ***\n");
					 return 0;
				   #endif
				   break;
		 case 'd': rate = atoi(optarg);
				   switch (rate){
				     case 1: dataRate = RF24_1MBPS; break;
					 case 2: dataRate = RF24_2MBPS; break;
					 case 250: dataRate = RF24_250KBPS; break;
					 default: printf("Invalid data rate not set\n"); dataRate = RF24_1MBPS; break;
				   }
				   break;
         case '?': showhelpinfo(argv[0]); return 0; break;
		 default : printf("Default opt\n"); break;
      }
	  
    std::atexit(on_exit);

    configureAndSetUpTunDevice(thisNodeAddr);
    configureAndSetUpRadio();

    //start threads
    tunRxThread.reset(new boost::thread(tunRxThreadFunction));
    tunTxThread.reset(new boost::thread(tunTxThreadFunction));
    radioRxTxThread.reset(new boost::thread(radioRxTxThreadFunction));

    joinThreads();

    return 0;
}