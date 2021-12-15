1. Name: Jinbean Park 
   UID: 805330751


2. The high level design of your implementation

1) simple-router.cpp
- handlePacket()
 - findIfaceByName()
 - getRoutingTable()
 - packet.data()
 - macToString()
 - cksum()
 - m_arp.insertArpEntry()
 - sendPacket()
 - m_natTable.lookup()
 - m_routingTable.lookup()
 - m_arp.queueRequest
 - m_natTable.insertNatEntry()
 - m_arp.removeRequest()

2) arp-cache.cpp
- periodCheckArpRequestAndCacheEntries()
- lookup()
- queueRequest()
- removeRequest()
- insertArpEntry()
- clear()
- ticker()

3) nat.cpp
- checkNatTable()
- lookup()
- insertNatEntry()
- clear()
- ticker()

4) routing-table.cpp
- lookup()
- load()
- addEntry()

5) Interface.cpp
- Interface()



3. The problems you ran into and how you solved the problems
=> The first problem I encountered was a minor problem, which destination MAC address of data packet can be not FF:FF:FF:FF:FF:FF but ff:ff:ff:ff:ff:ff. The second problem was about checking whether the packet IPv4 packet is destined to the router. When I tried resolving this problem, I could not understand how it is possible, so I posted the question about how to find every interface of the router. Thankfully, TA Pradeep let me know that we can just iterate through the interfaces using m_ifaces. The third problem was calculating the ICMP checksum. At first, I tried calculating the checksum by only covering the ICMP data part, but it did not work. I checked the function of cksum and read the spec of project 2, and then I came to notice that I missed the condition that ICMP checksum covers the ICMP data part as well as its header. For the NAT, it does not make sense for me to find the IP address of interfaces on NAT, so I posted a question about that on Piazza, and Rob let me know that we can hard-code those interfaces to know whether a packet is internal or external. The second problem was confusing about handling the IPv4 in the NAT environment. In the slide, there is no requirement about the case that NAT handles packet that is not ICMP packet, so I posted the question about this on Piazza, and Eric let me know that if we receive an IP packet that is "NOT" ICMP with the IP destination as one of the router's interfaces, we should drop it, and this applies whether NAT is enabled or not. Eric said the outline in the slide is slightly confusing. I spent much time understanding the outline of the Handle Packet Procedure. Lastly, In the NAT part, when the router sends back the echo reply packet from server to client, I sent the translated echo reply packet on interface "sw0-eth4", not "sw0-eth3". As a result, I spent so much time finding out the reason why the echo reply packet was not sent to the client even if the source and destination address of the translated packet is correct. I finally posted the question about this on Piazza, and Bob explained to me that the echo reply packet should be going out on sw0-eth3, so I was able to find out the issue and fix it successfully.