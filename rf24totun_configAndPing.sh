#!/bin/bash

#
# The MIT License (MIT)
#
# Copyright (c) 2014 Rei <devel@reixd.net>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

USAGE="Usage:
Set the IP for this node and ping the other node.
  $0 \$nodeIP \$nodeIP

This is node1 and it will ping node2 three times
  $0 1 2
  
Optionally set the octal RF24Network address for non-mesh
nodes.
  $0 1 2 01  
"

if [[ -z "${1##*[!0-9]*}" ]] || [[ -z "${2##*[!0-9]*}" ]]; then
        echo -e "$USAGE"
		exit
fi

ADDRESS=${3}
if [[ -z "${ADDRESS##*[!0-9]*}" ]]; then
	ADDRESS=0
fi

INTERFACE="tun_nrf24"
#MYIP="10.10.2.${1}/16"
#PINGIP="10.10.2.${2}"
MYIP="192.168.1.${1}/24"
PINGIP="192.168.1.${2}"
MTU=1500

function setIP() {
	sleep 4s && \
	if [ -d /proc/sys/net/ipv4/conf/${INTERFACE} ]; then
		ip link set ${INTERFACE} up  > /dev/null 2>&1
		ip addr add ${MYIP} dev ${INTERFACE}  > /dev/null 2>&1
		ip link set dev ${INTERFACE} mtu ${MTU} > /dev/null 2>&1
		ip addr show dev ${INTERFACE} > /dev/null 2>&1
		return 0
	else
		return 1
	fi
}

function pingOther() {
	sleep 6s && \
	ping -c3 -I ${INTERFACE} ${PINGIP}
}

setIP && pingOther &


#### Examples for running RF24toTUN once the interface is configured ####
# Run sudo ./rf24totun -h  for help

## Master Node w/TAP, mesh disabled, user specified RF24Network address (Master is 00)
/usr/local/bin/rf24totun -a${ADDRESS}

## Master Node w/TAP, mesh enabled for address assignment only, ARP for resolution
#/usr/local/bin/rf24totun -m

## Child Node w/TAP, mesh disabled, RF24Network address 012
#/usr/local/bin/rf24totun -a012

## Master node w/TUN, mesh enabled
#/usr/local/bin/rf24totun -t

## Child node w/TUN, mesh enabled, nodeID 22, using IP address <x>.<y>.<z>.22
#/usr/local/bin/rf24totun -t -i22


##IMPORTANT: Extra configuration for TAP devices - Increase the number of ARP resolution attempts from the default of 3
sysctl -w net.ipv4.neigh.tun_nrf24.mcast_solicit=10
#Optionally increase the delay between ARP requests
sysctl -w net.ipv4.neigh.tun_nrf24.retrans_time_ms=1333
#Optionally increase the default base_reachable_time_ms from 30000ms
sysctl -w net.ipv4.neigh.tun_nrf24.base_reachable_time_ms=90000

