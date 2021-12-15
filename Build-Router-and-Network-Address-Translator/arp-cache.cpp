/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
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

#include "arp-cache.hpp"
#include "core/utils.hpp"
#include "core/interface.hpp"
#include "simple-router.hpp"

#include <algorithm>
#include <iostream>

namespace simple_router {

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THIS METHOD
void ArpCache::periodicCheckArpRequestsAndCacheEntries() {

    std::list<std::shared_ptr<ArpRequest>>::iterator i;
    i = m_arpRequests.begin();
    auto curTime = steady_clock ::now();
    while (i != m_arpRequests.end()) {
        // After re-transmitting an ARP request 5 times, it should stop re-transmitting
        if ((*i)->nTimesSent >= MAX_SENT_TIME) {
            i = m_arpRequests.erase(i);
            continue;
        }
        // The router should send an ARP request about once a second until an ARP reply comes back
        else {
            Buffer arpRequest(sizeof(ethernet_hdr) + sizeof(arp_hdr));
            ethernet_hdr* ethHeader = (ethernet_hdr*)arpRequest.data();
            arp_hdr* arpHeader = (arp_hdr*)(arpRequest.data() + sizeof(ethernet_hdr));

            const Interface* inF = m_router.findIfaceByName((*i)->packets.front().iface);
            if (memcpy(ethHeader->ether_shost, inF->addr.data(), ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy a MAC address of the interface" << std::endl;
                return;
            }
            if (memcpy(ethHeader->ether_dhost, BroadcastEtherAddr, ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy a broadcast ethernet address" << std::endl;
                return;
            }
            ethHeader->ether_type = htons(ethertype_arp);

            arpHeader->arp_hrd = htons(arp_hrd_ethernet);   // (Ethernet)
            arpHeader->arp_pro = htons(ethertype_ip);   // (IPv4)
            arpHeader->arp_hln = ETHER_ADDR_LEN;   // Length of hardware address
            arpHeader->arp_pln = 4;   // The length of protocol address is always 4 (32 bits/8 = 4bytes).
            arpHeader->arp_op = htons(arp_op_request); // ARP request
            if (memcpy(arpHeader->arp_sha, inF->addr.data(), ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the MAC address of a sender" << std::endl;
                return;
            }
            arpHeader->arp_sip = inF->ip;    // Sender IP address
            if (memcpy(arpHeader->arp_tha, BroadcastEtherAddr, ETHER_ADDR_LEN) == NULL) {
                std::cerr << "Failed to copy the MAC address of a target" << std::endl;
                return;
            }
            arpHeader->arp_tip = (*i)->ip;   // Copy the target IP address.

            // Send ARP request
            m_router.sendPacket(arpRequest, inF->name);

            // Increasing the number of requesting times.
            (*i)->nTimesSent++;
            // For checking time out 30 sec
            (*i)->timeSent = curTime;   // The current value of the clock
        }
        i++;
    }

    // Remove the pending request, and any packets that are queued for the transmission.
    std::list<std::shared_ptr<ArpEntry>>::iterator j;
    j = m_cacheEntries.begin();
    while (j != m_cacheEntries.end()) {
        if (!(*j)->isValid) {
            j = m_cacheEntries.erase(j);
            continue;
        }
        else
            j++;
    }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.

ArpCache::ArpCache(SimpleRouter& router)
  : m_router(router)
  , m_shouldStop(false)
  , m_tickerThread(std::bind(&ArpCache::ticker, this))
{
}

ArpCache::~ArpCache()
{
  m_shouldStop = true;
  m_tickerThread.join();
}

std::shared_ptr<ArpEntry>
ArpCache::lookup(uint32_t ip)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& entry : m_cacheEntries) {
    if (entry->isValid && entry->ip == ip) {
      return entry;
    }
  }

  return nullptr;
}

std::shared_ptr<ArpRequest>
ArpCache::queueRequest(uint32_t ip, const Buffer& packet, const std::string& iface)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto request = std::find_if(m_arpRequests.begin(), m_arpRequests.end(),
                           [ip] (const std::shared_ptr<ArpRequest>& request) {
                             return (request->ip == ip);
                           });

  if (request == m_arpRequests.end()) {
    request = m_arpRequests.insert(m_arpRequests.end(), std::make_shared<ArpRequest>(ip));
  }

  (*request)->packets.push_back({packet, iface});
  return *request;
}

void
ArpCache::removeRequest(const std::shared_ptr<ArpRequest>& entry)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_arpRequests.remove(entry);
}

std::shared_ptr<ArpRequest>
ArpCache::insertArpEntry(const Buffer& mac, uint32_t ip)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto entry = std::make_shared<ArpEntry>();
  entry->mac = mac;
  entry->ip = ip;
  entry->timeAdded = steady_clock::now();
  entry->isValid = true;
  m_cacheEntries.push_back(entry);

  auto request = std::find_if(m_arpRequests.begin(), m_arpRequests.end(),
                           [ip] (const std::shared_ptr<ArpRequest>& request) {
                             return (request->ip == ip);
                           });
  if (request != m_arpRequests.end()) {
    return *request;
  }
  else {
    return nullptr;
  }
}

void
ArpCache::clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  m_cacheEntries.clear();
  m_arpRequests.clear();
}

void
ArpCache::ticker()
{
  while (!m_shouldStop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
      std::lock_guard<std::mutex> lock(m_mutex);

      auto now = steady_clock::now();

      for (auto& entry : m_cacheEntries) {
        if (entry->isValid && (now - entry->timeAdded > SR_ARPCACHE_TO)) {
          entry->isValid = false;
        }
      }

      periodicCheckArpRequestsAndCacheEntries();
    }
  }
}

std::ostream&
operator<<(std::ostream& os, const ArpCache& cache)
{
  std::lock_guard<std::mutex> lock(cache.m_mutex);

  os << "\nMAC            IP         AGE                       VALID\n"
     << "-----------------------------------------------------------\n";

  auto now = steady_clock::now();
  for (const auto& entry : cache.m_cacheEntries) {

    os << macToString(entry->mac) << "   "
       << ipToString(entry->ip) << "   "
       << std::chrono::duration_cast<seconds>((now - entry->timeAdded)).count() << " seconds   "
       << entry->isValid
       << "\n";
  }
  os << std::endl;
  return os;
}

} // namespace simple_router
