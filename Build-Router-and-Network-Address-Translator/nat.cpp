#include "nat.hpp"
#include "core/utils.hpp"
#include "core/interface.hpp"
#include "simple-router.hpp"

#include <algorithm>
#include <iostream>

namespace simple_router {

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THESE METHODS
void NatTable::checkNatTable() {

    for (std::map<uint16_t, std::shared_ptr<NatEntry>>::iterator i = m_natTable.begin(); i != m_natTable.end(); i++) {
        time_point currentTime = steady_clock::now();
        std::chrono::duration<double> timeSpan = std::chrono::duration_cast<std::chrono::duration<double>>(currentTime - i->second->timeUsed);
        if (timeSpan >= SR_NAT_TO)
            m_natTable.erase(i);
    }
}

std::shared_ptr<NatEntry> NatTable::lookup(uint16_t id) {

    if (m_natTable.count(id) != 0) {
        return m_natTable.find(id)->second;
    }
    else
        return nullptr;
}


void NatTable::insertNatEntry(uint16_t id, uint32_t in_ip, uint32_t ex_ip) {

    auto newNatEntry = std::shared_ptr<NatEntry>(new NatEntry());
    newNatEntry->internal_ip = in_ip;
    newNatEntry->external_ip = ex_ip;
    newNatEntry->timeUsed = steady_clock::now();
    newNatEntry->isValid = true;
    m_natTable.insert({id, newNatEntry});
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.

NatTable::NatTable(SimpleRouter& router)
  : m_router(router)
  , m_shouldStop(false)
  , m_tickerThread(std::bind(&NatTable::ticker, this))
{
}

NatTable::~NatTable()
{
  m_shouldStop = true;
  m_tickerThread.join();
}


void
NatTable::clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  m_natTable.clear();
}

void
NatTable::ticker()
{
  while (!m_shouldStop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
      std::lock_guard<std::mutex> lock(m_mutex);

      auto now = steady_clock::now();

      std::map<uint16_t, std::shared_ptr<NatEntry>>::iterator entryIt;
      for (entryIt = m_natTable.begin(); entryIt != m_natTable.end(); entryIt++ ) {
        if (entryIt->second->isValid && (now - entryIt->second->timeUsed > SR_ARPCACHE_TO)) {
          entryIt->second->isValid = false;
        }
      }

      checkNatTable();
    }
  }
}

std::ostream&
operator<<(std::ostream& os, const NatTable& table)
{
  std::lock_guard<std::mutex> lock(table.m_mutex);

  os << "\nID            Internal IP         External IP             AGE               VALID\n"
     << "-----------------------------------------------------------------------------------\n";

  auto now = steady_clock::now();

  for (auto const& entryIt : table.m_natTable) {
    os << entryIt.first << "            "
       << ipToString(entryIt.second->internal_ip) << "         "
       << ipToString(entryIt.second->external_ip) << "         "
       << std::chrono::duration_cast<seconds>((now - entryIt.second->timeUsed)).count() << " seconds         "
       << entryIt.second->isValid
       << "\n";
  }
  os << std::endl;
  return os;
}

} // namespace simple_router
