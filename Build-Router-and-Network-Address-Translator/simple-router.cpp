/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/***
 * Copyright (c) 2017 Alexander Afanasyev
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */


#include <iostream>
#include <string>
#include "simple-router.hpp"
#include "core/utils.hpp"

#include <fstream>
#include <set>

namespace simple_router {

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THIS METHOD
void SimpleRouter::handlePacket(const Buffer& packet, const std::string& inIface, int nat_flag) {

    std::cerr << "Got packet of size " << packet.size() << " on interface " << inIface << std::endl;

    const Interface* iface = findIfaceByName(inIface);
    if (iface == nullptr) {
        std::cerr << "Received packet, but interface is unknown, ignoring" << std::endl;
        return;
    }

    std::cerr << "========= Routing Table =========" << std::endl;
    std::cerr << getRoutingTable() << std::endl;

    // Check the size of packet.
    if (sizeof(packet) < sizeof(ethernet_hdr)) {
        std::cerr << "The size of packet is smaller than the size of ethernet header" << std::endl;
        return;
    }

    // Read the ethernet header to find source and destination MAC addresses
    ethernet_hdr* ethHdrAdd = (ethernet_hdr*)packet.data();

    // Ignore Ethernet frames not destined to the router.
    std::string dAddPacket = macToString(packet);
    std::string mAddIface = macToString(iface->addr);
    std::string mAddBroad1 = "FF:FF:FF:FF:FF:FF";
    std::string mAddBroad2 = "ff:ff:ff:ff:ff:ff";

    if ((dAddPacket != mAddIface) && (dAddPacket != mAddBroad1) && (dAddPacket != mAddBroad2)) {
        std::cerr << "Ethernet frames not destined to the router" << std::endl;
        return;
    }

    uint16_t etherType = ethertype((const uint8_t *)packet.data());

    // Your route must ignore Ethernet frames other than APR and IPv4.
    // The case that etherType is IPv4.
    if (etherType == ethertype_ip) {

        // Verify checksum and length (cksum() function)
        ip_hdr* ipHdrAdd = (ip_hdr*)(packet.data() + sizeof(ethernet_hdr));
        if (packet.size() < sizeof(ethernet_hdr) + sizeof(ip_hdr)) {
            std::cerr << "The length of an IP packet is too short" << std::endl;
            return;
        }
        uint16_t ipChecksum = ipHdrAdd->ip_sum;
        ipHdrAdd->ip_sum = 0;
        if (cksum(ipHdrAdd, sizeof(ip_hdr)) != ipChecksum) {
            std::cerr << "IP checksum is wrong" << std::endl;
            return;
        }

        // Check destination IP
        uint32_t ipDstAdd = ipHdrAdd->ip_dst;
        for (std::set<Interface>::const_iterator inF = m_ifaces.begin(); inF != m_ifaces.end(); inF++) {

            // The case of being destined to the router (to one of the IP addresses of the router)
            if (inF->ip == ipDstAdd) {

                // Check the packet carries ICMP payload.
                if (ipHdrAdd->ip_p == ip_protocol_icmp) {

                    // If the packet carries ICMP payload, it should be properly dispatched.
                    Buffer macPacket(ETHER_ADDR_LEN);
                    if (memcpy(macPacket.data(), ethHdrAdd->ether_shost, ETHER_ADDR_LEN) == NULL) {
                        std::cerr << "Failed to copy source address of ethernet frame" << std::endl;
                        return;
                    }
                    m_arp.insertArpEntry(macPacket, ipHdrAdd->ip_src);

                    icmp_hdr *icmpHdrAdd = (icmp_hdr *) (packet.data() + sizeof(ethernet_hdr) + sizeof(ip_hdr));

                    // Verify ICMP checksum
                    if (icmpHdrAdd->icmp_type == 8) { // 8: echo message

                        // Note that ICMP checksum covers the ICMP data part as well as its header.
                        uint16_t icmpChecksum = icmpHdrAdd->icmp_sum;
                        icmpHdrAdd->icmp_sum = 0;
                        if (cksum(icmpHdrAdd, packet.size() - sizeof(ethernet_hdr) - sizeof(ip_hdr)) != icmpChecksum) {
                            std::cerr << "ICMP checksum is wrong" << std::endl;
                            return;
                        }

                        // Send ICMP echo reply
                        // Change the source & destination address of IP address
                        ipHdrAdd->ip_dst = ipHdrAdd->ip_src;
                        ipHdrAdd->ip_src = inF->ip;
                        ipHdrAdd->ip_sum = 0;
                        ipHdrAdd->ip_sum = cksum(ipHdrAdd, sizeof(ip_hdr));
                        // TTl in the IP header should be set as 64 when your router creates a new echo reply message.
                        ipHdrAdd->ip_ttl = 64;
                        // Change the source & destination address of MAC address
                        if (memcpy(ethHdrAdd->ether_dhost, ethHdrAdd->ether_shost, ETHER_ADDR_LEN) == NULL) {
                            std::cerr << "Failed to copy the source MAC address to the dest MAC address" << std::endl;
                            return;
                        }
                        if (memcpy(ethHdrAdd->ether_shost, iface->addr.data(), ETHER_ADDR_LEN) == NULL) {
                            std::cerr << "Failed to copy the interface MAC address to the source MAC address" << std::endl;
                            return;
                        }
                        // Change the type and checksum of icmp.
                        icmpHdrAdd->icmp_type = 0; // 0: echo reply message
                        icmpHdrAdd->icmp_sum = 0;
                        icmpHdrAdd->icmp_sum = cksum(icmpHdrAdd, sizeof(icmp_hdr));

                        std::cerr << "Echo reply message: START" << std::endl;
                        print_hdrs(packet);
                        std::cerr << "Echo reply message: FINISH" << std::endl;

                        // Send the echo reply message.
                        sendPacket(packet, iface->name);
                        return;
                    }

                    // NAT
                    // The case that NAT is enabled & ICMP Echo Reply & DestIP == External NAT interface
                    else if (nat_flag == 1 && icmpHdrAdd->icmp_type == 0 && m_natTable.lookup(icmpHdrAdd->icmp_id)->external_ip == ipDstAdd) {
                        // When NAT receives an ICMP echo reply, it must translate back from an external address
                        // to the original internal address
                        ipHdrAdd->ip_dst = m_natTable.lookup(icmpHdrAdd->icmp_id)->internal_ip;

                        // Echo requests sent to other IP address should be forwarded to the next hop address as usual.
                        // For each forwarded IPv4 packet, the router should decrement TTL, and recompute the checksum.
                        ipHdrAdd->ip_ttl -= 1;

                        // Your router should discard the packet if TTL equals 0 after decrementing it.
                        if (ipHdrAdd->ip_ttl <= 0) {
                            std::cerr << "TTl is equal to 0 or lower than 0";
                            return;
                        }

                        // Recompute the checksum.
                        uint16_t recomputedChksum = cksum(ipHdrAdd, sizeof(ip_hdr));
                        ipHdrAdd->ip_sum = recomputedChksum;

                        // Finding a next-hop IP address in the routing table
                        RoutingTableEntry foundRoutingEntry = m_routingTable.lookup(ipHdrAdd->ip_dst);
                        uint32_t nextHopGateway = foundRoutingEntry.gw;
                        const Interface* nextInF = findIfaceByName("sw0-eth3");

                        // When your router receives an IP packet to be forwarded to a next-hop IP address,
                        // it should check ARP cache if it contains the corresponding MAC address.
                        std::shared_ptr<ArpEntry> foundArpEntry = m_arp.lookup(nextHopGateway);

                        // If ARP entry found, the router should proceed with handling the IP packet.
                        if (foundArpEntry != nullptr) {
                            if (memcpy(ethHdrAdd->ether_dhost, foundArpEntry->mac.data(), ETHER_ADDR_LEN) == NULL) {
                                std::cerr << "Failed to copy the corresponding MAC address to the destination ethernet address" << std::endl;
                                return;
                            }
                            // The MAC address of interface for returned arp entry should be source address.
                            if (memcpy(ethHdrAdd->ether_shost, nextInF->addr.data(), ETHER_ADDR_LEN) == NULL) {
                                std::cerr << "Failed to copy the MAC address of interface to the source ethernet address" << std::endl;
                                return;
                            }

                            // Forwarding to a next-hop IP address.
                            sendPacket(packet, nextInF->name);
                        }
                            // If ARP entry not found, cache the packet and send ARP request.
                        else {
                            std::cerr << "Since ARP entry is not found, it'll queue received packet and send ARP request" << std::endl;
                            auto arpRequest = m_arp.queueRequest(nextHopGateway, packet, nextInF->name);
                            return;
                        }
                    }
                    else
                        return; // Discard packet.
                }
            }
        }
        // Otherwise, forward the packet.
        // If NAT is enabled & srclP==internal IP then translate addresses
        icmp_hdr* icmpHdrAdd = (icmp_hdr *) (packet.data() + sizeof(ethernet_hdr) + sizeof(ip_hdr));
        if (nat_flag == 1 && m_natTable.lookup(icmpHdrAdd->icmp_id) == nullptr) {
            m_natTable.insertNatEntry(icmpHdrAdd->icmp_id, ipHdrAdd->ip_src, findIfaceByName("sw0-eth4")->ip);
            ipHdrAdd->ip_src = findIfaceByName("sw0-eth4")->ip;
        }
        // If the mapping already exists then NAT should use the corresponding information.
        else if (nat_flag == 1 && ipHdrAdd->ip_src == m_natTable.lookup(icmpHdrAdd->icmp_id)->internal_ip) {

            std::cerr << "=============== NAT Entry ===============" << std::endl;
            std::cerr << m_natTable << std::endl;

            ipHdrAdd->ip_src = m_natTable.lookup(icmpHdrAdd->icmp_id)->external_ip;
            m_natTable.lookup(icmpHdrAdd->icmp_id)->timeUsed = steady_clock ::now();
        }
        // Echo requests sent to other IP address should be forwarded to the next hop address as usual.
        // For each forwarded IPv4 packet, the router should decrement TTL, and recompute the checksum.
        ipHdrAdd->ip_ttl -= 1;

        // Your router should discard the packet if TTL equals 0 after decrementing it.
        if (ipHdrAdd->ip_ttl <= 0) {
            std::cerr << "TTl is equal to 0 or lower than 0";
            return;
        }

        // Recompute the checksum.
        uint16_t recomputedChksum = cksum(ipHdrAdd, sizeof(ip_hdr));
        ipHdrAdd->ip_sum = recomputedChksum;

        // Finding a next-hop IP address in the routing table
        RoutingTableEntry foundRoutingEntry = m_routingTable.lookup(ipDstAdd);
        uint32_t nextHopGateway = foundRoutingEntry.gw;
        const Interface* nextInF = findIfaceByName(foundRoutingEntry.ifName);

        // When your router receives an IP packet to be forwarded to a next-hop IP address,
        // it should check ARP cache if it contains the corresponding MAC address.
        std::shared_ptr<ArpEntry> foundArpEntry = m_arp.lookup(nextHopGateway);

        // If ARP entry found, the router should proceed with handling the IP packet.
        if (foundArpEntry != nullptr) {
            if (memcpy(ethHdrAdd->ether_dhost, foundArpEntry->mac.data(), ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the corresponding MAC address to the destination ethernet address" << std::endl;
                return;
            }
            // The MAC address of interface for returned arp entry should be source address.
            if (memcpy(ethHdrAdd->ether_shost, nextInF->addr.data(), ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the MAC address of interface to the source ethernet address" << std::endl;
                return;
            }

            std::cerr << "IPv4 packet: START" << std::endl;
            print_hdrs(packet);
            std::cerr << "IPv4 packet: FINISH" << std::endl;

            // Forwarding to a next-hop IP address.
            sendPacket(packet, nextInF->name);
        }
        // If ARP entry not found, cache the packet and send ARP request.
        else {
            std::cerr << "Since ARP entry is not found, it'll queue received packet and send ARP request" << std::endl;
            auto arpRequest = m_arp.queueRequest(nextHopGateway, packet, nextInF->name);
            return;
        }
    }

    // The case that etherType is ARP Packets.
    else if (etherType == ethertype_arp) {

        arp_hdr* arpHdrAdd = (arp_hdr *) (packet.data() + sizeof(ethernet_hdr));

        // Your router must properly process incoming ARP requests and replies:
        // The case that ARP requests.
        if (ntohs(arpHdrAdd->arp_op) == arp_op_request) {
            Buffer resp(sizeof(ethernet_hdr) + sizeof(arp_hdr));

            // Must properly respond to ARP requests for MAC address
            // for the IP address of the corresponding network interface
            // Must ignore other ARP requests
            if (arpHdrAdd->arp_tip != iface->ip) {
                std::cerr << "The target IP address does not correspond to interface" << std::endl;
                return;
            }

            ethernet_hdr* respEthHdr = (ethernet_hdr*)resp.data();
            arp_hdr* respArpHdr = (arp_hdr*)(resp.data() + sizeof(ethernet_hdr));
            if (memcpy(respEthHdr->ether_dhost, ethHdrAdd->ether_shost, ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the source ethernet address to destination ethernet address of respond ethernet frame" << std::endl;
                return;
            }
            if (memcpy(respEthHdr->ether_shost, iface->addr.data(), ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the MAC address to source ethernet address of respond ethernet frame" << std::endl;
                return;
            }
            respEthHdr->ether_type = htons(ethertype_arp);  // Set a packet type ID to ARP

            respArpHdr->arp_hrd = htons(arp_hrd_ethernet);   // (Ethernet)
            respArpHdr->arp_pro = htons(ethertype_ip);   // (IPv4)
            respArpHdr->arp_hln = ETHER_ADDR_LEN;   // Length of hardware address
            respArpHdr->arp_pln = 4;   // The length of protocol address is always 4 (32 bits/8 = 4bytes).
            respArpHdr->arp_op = htons(arp_op_reply); // ARP reply
            if (memcpy(respArpHdr->arp_sha, iface->addr.data(), ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the MAC address of a sender" << std::endl;
                return;
            }
            respArpHdr->arp_sip = iface->ip;    // Sender IP address
            if (memcpy(respArpHdr->arp_tha, arpHdrAdd->arp_sha, ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the MAC address of a target" << std::endl;
                return;
            }
            respArpHdr->arp_tip = arpHdrAdd->arp_sip;   // Copy the target IP address.

            // Send ARP reply for the ARP request.
            sendPacket(resp, iface->name);

            std::cerr << "ARP reply message: START" << std::endl;
            print_hdrs(resp);
            std::cerr << "ARP reply message: FINISH" << std::endl;

        }
        // The case that ARP reply.
        else if (ntohs(arpHdrAdd->arp_op) == arp_op_reply) {
            Buffer repliedARP(ETHER_ADDR_LEN);

            // It should record IP-MAC mapping information in ARP cache
            // (Source IP / Source hardware address in the ARP reply).
            if (memcpy(repliedARP.data(), arpHdrAdd->arp_sha, ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy sender MAC address." << std::endl;
                return;
            }
            // Store IP/MAC info.
            auto correspondedPacket = m_arp.insertArpEntry(repliedARP, arpHdrAdd->arp_sip);
            if (correspondedPacket == nullptr) {
                std::cerr << "Failed to insert arp entry" << std::endl;
                return;
            }
            // Send cached packets out.
            for (std::list<PendingPacket>::const_iterator i = correspondedPacket->packets.begin(); i != correspondedPacket->packets.end(); i++) {

                Buffer singlePacket(i->packet);
                ethernet_hdr* singleEthHdr = (ethernet_hdr *)singlePacket.data();
                if (memcpy(singleEthHdr->ether_dhost, arpHdrAdd->arp_sha, ETHER_ADDR_LEN) == NULL) {
                    std::cerr << "Failed to copy the sender MAC address while sending cached packets out." << std::endl;
                    return;
                }
                if (memcpy(singleEthHdr->ether_shost, arpHdrAdd->arp_tha, ETHER_ADDR_LEN) == NULL) {
                    std::cerr << "Failed to copy the target MAC address while sending cached packets out." << std::endl;
                    return;
                }
                // Send corresponding enqueued single packet one by one.
                sendPacket(singlePacket, i->iface);

                std::cerr << "Enqueued packets: START" << std::endl;
                print_hdrs(packet);
                std::cerr << "Enqueued packets: FINISH" << std::endl;

                // Your task is to remove such entries.
                m_arp.removeRequest(correspondedPacket);
            }
        }
    }
    return;
}




//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.
SimpleRouter::SimpleRouter(): m_arp(*this), m_natTable(*this) {

}

void SimpleRouter::sendPacket(const Buffer& packet, const std::string& outIface) {
  m_pox->begin_sendPacket(packet, outIface);
}

bool SimpleRouter::loadRoutingTable(const std::string& rtConfig) {
  return m_routingTable.load(rtConfig);
}

void SimpleRouter::loadIfconfig(const std::string& ifconfig) {
  std::ifstream iff(ifconfig.c_str());
  std::string line;
  while (std::getline(iff, line)) {
    std::istringstream ifLine(line);
    std::string iface, ip;
    ifLine >> iface >> ip;

    in_addr ip_addr;
    if (inet_aton(ip.c_str(), &ip_addr) == 0) {
      throw std::runtime_error("Invalid IP address `" + ip + "` for interface `" + iface + "`");
    }

    m_ifNameToIpMap[iface] = ip_addr.s_addr;
  }
}

void SimpleRouter::printIfaces(std::ostream& os) {
  if (m_ifaces.empty()) {
    os << " Interface list empty " << std::endl;
    return;
  }

  for (const auto& iface : m_ifaces) {
    os << iface << "\n";
  }
  os.flush();
}

const Interface* SimpleRouter::findIfaceByIp(uint32_t ip) const {
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [ip] (const Interface& iface) {
      return iface.ip == ip;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

const Interface* SimpleRouter::findIfaceByMac(const Buffer& mac) const {
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [mac] (const Interface& iface) {
      return iface.addr == mac;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

void SimpleRouter::reset(const pox::Ifaces& ports) {
  std::cerr << "Resetting SimpleRouter with " << ports.size() << " ports" << std::endl;

  m_arp.clear();
  m_ifaces.clear();

  for (const auto& iface : ports) {
    auto ip = m_ifNameToIpMap.find(iface.name);
    if (ip == m_ifNameToIpMap.end()) {
      std::cerr << "IP_CONFIG missing information about interface `" + iface.name + "`. Skipping it" << std::endl;
      continue;
    }

    m_ifaces.insert(Interface(iface.name, iface.mac, ip->second));
  }

  printIfaces(std::cerr);
}

const Interface* SimpleRouter::findIfaceByName(const std::string& name) const {
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [name] (const Interface& iface) {
      return iface.name == name;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}


} // namespace simple_router {
