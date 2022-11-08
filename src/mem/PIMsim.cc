/*
 * Copyright (c) 2013 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/PIMsim.hh"

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/Drain.hh"
#include "debug/PIMsim.hh"
#include "sim/system.hh"

#include "debug/PIMAccess.hh"

namespace gem5
{

namespace memory
{

PIMsim::PIMsim(const Params &p) :
    AbstractMemory(p),
    port(name() + ".port", *this),
    read_cb(std::bind(&PIMsim::readComplete,
                      this, 0, std::placeholders::_1, std::placeholders::_2)),
    write_cb(std::bind(&PIMsim::writeComplete,
                       this, 0, std::placeholders::_1)),
    wrapper(p.configFile, p.filePath, read_cb, write_cb),
    retryReq(false), retryResp(false), startTick(0),
    nbrOutstandingReads(0), nbrOutstandingWrites(0),
    sendResponseEvent([this]{ sendResponse(); }, name()),
    tickEvent([this]{ tick(); }, name())
{
    DPRINTF(PIMsim,
            "Instantiated PIMsim with clock %d ns and queue size %d\n",
            wrapper.clockPeriod(), wrapper.queueSize());

    pkt_Q.clear();    // >> mmm <<
    pkt_cnt.clear();  // >> mmm <<

    // Register a callback to compensate for the destructor not
    // being called. The callback prints the PIMsim stats.
    registerExitCallback([this]() { wrapper.printStats(); });
}

void
PIMsim::init()
{
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("PIMsim %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }

    if (system()->cacheLineSize() != wrapper.burstSize())
        fatal("PIMsim burst size %d does not match cache line size %d\n",
              wrapper.burstSize(), system()->cacheLineSize());
    wrapper.init(pmemAddr - range.start(), range.size());
}

void
PIMsim::startup()
{
    startTick = curTick();

    // kick off the clock ticks
    schedule(tickEvent, clockEdge());
}

void
PIMsim::resetStats() {
    wrapper.resetStats();
}

void
PIMsim::sendResponse()
{
    assert(!retryResp);
    assert(!responseQueue.empty());

    DPRINTF(PIMsim, "Attempting to send response\n");

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        responseQueue.pop_front();

        DPRINTF(PIMsim, "Have %d read, %d write, %d responses outstanding\n",
                nbrOutstandingReads, nbrOutstandingWrites,
                responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();
    } else {
        retryResp = true;

        DPRINTF(PIMsim, "Waiting for response retry\n");

        assert(!sendResponseEvent.scheduled());
    }
}

unsigned int
PIMsim::nbrOutstanding() const
{
    return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
}

void
PIMsim::tick()
{
    // Only tick when it's timing mode
    if (system()->isTimingMode()) {
        wrapper.tick();

        // is the connected port waiting forDRAMsim3 a retry, if so check the
        // state and send a retry if conditions have changed
        if (retryReq && nbrOutstanding() < wrapper.queueSize()) {
            retryReq = false;
            port.sendRetryReq();
        }
    }

    schedule(tickEvent, curTick() + wrapper.clockPeriod() * SimClock::Int::ns);
}

Tick
PIMsim::recvAtomic(PacketPtr pkt)
{
    access(pkt);

    // 50 ns is just an arbitrary value at this point
    return pkt->cacheResponding() ? 0 : 50000;
}

void
PIMsim::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    functionalAccess(pkt);

    // potentially update the packets in our response queue as well
    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

void
PIMsim::push_packet(PacketPtr pkt)  // >> mmm <<
{
    Addr pkt_addr = pkt->getAddr();
    bool pkt_isWrite = pkt->isWrite();
    uint8_t *pkt_DataPtr = new uint8_t[pkt->getSize()];

    if (pkt_isWrite)
        memcpy(pkt_DataPtr, pkt->getConstPtr<uint8_t>(), pkt->getSize());

    pkt_Q[pkt_addr] = std::pair<bool, uint8_t*>(pkt_isWrite, pkt_DataPtr);

    Addr pkt_addr_key = pkt_addr - pkt_addr % (system()->cacheLineSize());
    if (pkt_cnt.find(pkt_addr_key) == pkt_cnt.end()) {
        pkt_cnt[pkt_addr_key] = 0;
    }
    pkt_cnt[pkt_addr_key]++;
}


bool
PIMsim::recvTimingReq(PacketPtr pkt)
{
    // if a cache is responding, sink the packet without further action
    if (pkt->cacheResponding()) {
        pendingDelete.reset(pkt);
        return true;
    }

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq)
        return false;

    // if we cannot accept we need to send a retry once progress can
    // be made
    // bool can_accept = nbrOutstanding() < wrapper.queueSize();
    
    bool can_accept = wrapper.canAccept(pkt->getAddr(), pkt->isWrite());

    // keep track of the transaction
    if (pkt->isRead()) {
        if (can_accept) {
            outstandingReads[pkt->getAddr()].push(pkt);

            // we count a transaction as outstanding until it has left the
            // queue in the controller, and the response has been sent
            // back, note that this will differ for reads and writes
            ++nbrOutstandingReads;
        }
    } else if (pkt->isWrite()) {
        if (can_accept) {
            outstandingWrites[pkt->getAddr()].push(pkt);

            ++nbrOutstandingWrites;

            // perform the access for writes
            accessAndRespond(pkt);
        }
    } else {
        // keep it simple and just respond if necessary
        accessAndRespond(pkt);
        return true;
    }

    if (can_accept) {
        // we should never have a situation when we think there is space,
        // and there isn't
        // assert(wrapper.canAccept(pkt->getAddr(), pkt->isWrite()));
        // allocate memory for data
        #if 0    // >> org <<
        uint8_t * DataPtr = new uint8_t[wrapper.burstSize()];

        if (pkt->isWrite())
        {
            // copy data from pkt
            pkt->writeData(DataPtr);
        }
        wrapper.enqueue(pkt->getAddr(), pkt->isWrite(), DataPtr);
        #endif

        #if 0    // >> mmm << ver0 (sequencial)
        if (pkt->getAddr() >= 0x140000000 && pkt->getAddr() < 0x240000000) {
            DPRINTF(PIMAccess, "<mmm> pushing pkt\n");
            push_packet(pkt);
            DPRINTF(PIMAccess, "<mmm> gathering pkt\n");

            if (pkt_Q.size() == 4) {
                DPRINTF(PIMAccess, "<mmm> gathered!\n");
                uint8_t * DataPtr = new uint8_t[wrapper.burstSize()];
                uint64_t addr_32B = pkt->getAddr() - pkt->getAddr() % (system()->cacheLineSize());

                if (pkt_Q[addr_32B].first) {
                    for (int i=0; i<4; i++) {
                        DPRINTF(PIMAccess, "<mmm> write %d\n", i);
                        //memcpy(DataPtr+8*i, pkt_Q[i]->getConstPtr<uint8_t>(), 8);
                        memcpy(DataPtr+8*i, pkt_Q[addr_32B + 8*i].second, 8);
                    }
                }
                DPRINTF(PIMAccess, "<mmm> enqueue...\n");
                wrapper.enqueue(addr_32B, pkt_Q[addr_32B].first, DataPtr);
                DPRINTF(PIMAccess, "<mmm> ended!\n");
                pkt_Q.clear();
            }
        } else {
            
            uint8_t * DataPtr = new uint8_t[wrapper.burstSize()];
            
            if (pkt->isWrite()) {
                // copy data from pkt
                pkt->writeData(DataPtr);
            }
            wrapper.enqueue(pkt->getAddr(), pkt->isWrite(), DataPtr);
        }
        #endif

        #if 0    // >> mmm << ver1 (mixed)
        if (pkt->getAddr() >= 0x140000000 && pkt->getAddr() < 0x240000000) {
            DPRINTF(PIMAccess, "<mmm> pushing pkt\n");
            push_packet(pkt);
            DPRINTF(PIMAccess, "<mmm> gathering pkt\n");
            
            uint64_t addr_32B = pkt->getAddr() - pkt->getAddr() % (system()->cacheLineSize());
            if (pkt_cnt[addr_32B] == 4) {
                DPRINTF(PIMAccess, "<mmm> 32B gathered!\n");
                uint8_t *DataPtr = new uint8_t[wrapper.burstSize()];

                if (pkt_Q[addr_32B].first) {
                    for (int i=0; i<4; i++) {
                        DPRINTF(PIMAccess, "<mmm> write %d\n", i);
                        memcpy(DataPtr+8*i, pkt_Q[addr_32B + 8*i].second, 8);
                    }
                }

                DPRINTF(PIMAccess, "<mmm> 32B --> PIMsim ing...\n");
                wrapper.enqueue(addr_32B, pkt_Q[addr_32B].first, DataPtr);
                DPRINTF(PIMAccess, "<mmm> 32B --> PIMsim ended!\n");
                
                for (int i=0; i<4; i++)
                    pkt_Q.erase(addr_32B + 8*i);
                pkt_cnt[addr_32B] = 0;
            }
        } else {
            uint8_t * DataPtr = new uint8_t[wrapper.burstSize()];

            if (pkt->isWrite()) {
                // copy data from pkt
                pkt->writeData(DataPtr);
            }
            wrapper.enqueue(pkt->getAddr(), pkt->isWrite(), DataPtr);
        }
        #endif

        #if 1    // >> mmm << ver2 (mixed + modified read)
        if (pkt->getAddr() >= 0x140000000 && pkt->getAddr() < 0x240000000) {
            DPRINTF(PIMAccess, "<mmm> pushing pkt\n");
            push_packet(pkt);
            DPRINTF(PIMAccess, "<mmm> gathering pkt\n");
            
            uint64_t addr_32B = pkt->getAddr() - pkt->getAddr() % (system()->cacheLineSize());

            if (pkt_Q[addr_32B].first) {  // Write
                if (pkt_cnt[addr_32B] == 4) {
                    DPRINTF(PIMAccess, "<mmm> 32B WR gathered!\n");
                    uint8_t *DataPtr = new uint8_t[wrapper.burstSize()];

                    for (int i=0; i<4; i++) {
                        memcpy(DataPtr+8*i, pkt_Q[addr_32B + 8*i].second, 8);
                    }

                    DPRINTF(PIMAccess, "<mmm> 32B WR --> PIMsim ing...\n");
                    wrapper.enqueue(addr_32B, pkt_Q[addr_32B].first, DataPtr);
                    DPRINTF(PIMAccess, "<mmm> 32B WR --> PIMsim ended!\n");
                    
                    for (int i=0; i<4; i++)
                        pkt_Q.erase(addr_32B + 8*i);
                    pkt_cnt[addr_32B] = 0;
                }
            } else {  // Read
                if (pkt_cnt[addr_32B] == 1) {
                    DPRINTF(PIMAccess, "<mmm> 32B RD Pre-pass!\n");
                    uint8_t *DataPtr = new uint8_t[wrapper.burstSize()];

                    DPRINTF(PIMAccess, "<mmm> 32B RD --> PIMsim ing...\n");
                    wrapper.enqueue(addr_32B, pkt_Q[addr_32B].first, DataPtr);
                    DPRINTF(PIMAccess, "<mmm> 32B RD --> PIMsim ended!\n");
                } else if (pkt_cnt[addr_32B] == 4) {
                    readComplete(0, pkt->getAddr(), (uint8_t*)(nullptr));
                    pkt_cnt[addr_32B] = 0;
                } else {
                    readComplete(0, pkt->getAddr(), (uint8_t*)(nullptr));
                }
                pkt_Q.erase(pkt->getAddr());
            }
        } else {
            uint8_t * DataPtr = new uint8_t[wrapper.burstSize()];

            if (pkt->isWrite()) {
                // copy data from pkt
                pkt->writeData(DataPtr);
            }
            wrapper.enqueue(pkt->getAddr(), pkt->isWrite(), DataPtr);
        }
        #endif

        DPRINTF(PIMsim, "Enqueueing address %lld\n", pkt->getAddr());

        // @todo what about the granularity here, implicit assumption that
        // a transaction matches the burst size of the memory (which we
        // cannot determine without parsing the ini file ourselves)

        return true;
    } else {
        retryReq = true;
        return false;
    }

}

void
PIMsim::recvRespRetry()
{
    DPRINTF(PIMsim, "Retrying\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}
#if TRACING_ON
static inline void
tracePacket(System *sys, const char *label, PacketPtr pkt)
{
    int size = pkt->getSize();
    if (pkt->getAddr() >= 0x140000000) {
        if (size == 1 || size == 2 || size == 4 || size == 8) {
            ByteOrder byte_order = sys->getGuestByteOrder();
            DPRINTF(PIMAccess, "%s from %s of size %i on address %#x data "
                    "%#x %c\n", label, sys->getRequestorName(pkt->req->
                    requestorId()), size, pkt->getAddr(),
                    pkt->getUintX(byte_order),
                    pkt->req->isUncacheable() ? 'U' : 'C');
            return;
        }
        DPRINTF(PIMAccess, "%s from %s of size %i on address %#x %c\n",
                label, sys->getRequestorName(pkt->req->requestorId()),
                size, pkt->getAddr(), pkt->req->isUncacheable() ? 'U' : 'C');
        DDUMP(PIMAccess, pkt->getConstPtr<uint8_t>(), pkt->getSize());
    }
    if (size == 1 || size == 2 || size == 4 || size == 8) {
        ByteOrder byte_order = sys->getGuestByteOrder();
        DPRINTF(PIMsim, "%s from %s of size %i on address %#x data "
                "%#x %c\n", label, sys->getRequestorName(pkt->req->
                requestorId()), size, pkt->getAddr(),
                pkt->getUintX(byte_order),
                pkt->req->isUncacheable() ? 'U' : 'C');
        return;
    }
    DPRINTF(PIMsim, "%s from %s of size %i on address %#x %c\n",
            label, sys->getRequestorName(pkt->req->requestorId()),
            size, pkt->getAddr(), pkt->req->isUncacheable() ? 'U' : 'C');
    DDUMP(PIMsim, pkt->getConstPtr<uint8_t>(), pkt->getSize());
}

#   define TRACE_PACKET(A) tracePacket(system(), A, pkt)
#else
#   define TRACE_PACKET(A)
#endif

void
PIMsim::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(PIMsim, "Access for address %lld\n", pkt->getAddr());

    bool needsResponse = pkt->needsResponse();

    // do not use abstract_mem's access
    // access(pkt);
    if (pkt->cacheResponding()) {
        DPRINTF(PIMsim, "Cache responding to %#llx: not responding\n",
                pkt->getAddr());
    }

    if (pkt->cmd == MemCmd::CleanEvict || pkt->cmd == MemCmd::WritebackClean) {
        DPRINTF(PIMsim, "CleanEvict  on 0x%x: not responding\n",
                pkt->getAddr());
    }

    assert(pkt->getAddrRange().isSubset(range));

    if (pkt->cmd == MemCmd::SwapReq) {
        assert(false);
    } else if (pkt->isRead()) {
        assert(!pkt->isWrite());
        assert(!pkt->isLLSC());
        TRACE_PACKET(pkt->req->isInstFetch() ? "IFetch" : "Read");
        stats.numReads[pkt->req->requestorId()]++;
        stats.bytesRead[pkt->req->requestorId()] += pkt->getSize();
        if (pkt->req->isInstFetch())
            stats.bytesInstRead[pkt->req->requestorId()] += pkt->getSize();
    } else if (pkt->isInvalidate() || pkt->isClean()) {
        assert(!pkt->isWrite());
        // in a fastmem system invalidating and/or cleaning packets
        // can be seen due to cache maintenance requests

        // no need to do anything
    } else if (pkt->isWrite()) {
            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Write");
            stats.numWrites[pkt->req->requestorId()]++;
            stats.bytesWritten[pkt->req->requestorId()] += pkt->getSize();
    } else {
        panic("Unexpected packet %s", pkt->print());
    }

    if (pkt->needsResponse()) {
        pkt->makeResponse();
    }

    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // access already turned the packet into a response
        assert(pkt->isResponse());
        // Here we pay for xbar additional delay and to process the payload
        // of the packet.
        Tick time = curTick() + pkt->headerDelay + pkt->payloadDelay;
        // Reset the timings of the packet
        pkt->headerDelay = pkt->payloadDelay = 0;

        DPRINTF(PIMsim, "Queuing response for address %lld\n",
                pkt->getAddr());

        // queue it to be sent back
        responseQueue.push_back(pkt);

        // if we are not already waiting for a retry, or are scheduled
        // to send a response, schedule an event
        if (!retryResp && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, time);
    } else {
        // queue the packet for deletion
        pendingDelete.reset(pkt);
    }

}

void PIMsim::readComplete(unsigned id, uint64_t addr, uint8_t* dataPtr)
{
    DPRINTF(PIMsim, "Read to address %lld complete\n", addr);

    #if 0   // >> org <<
    // get the outstanding reads for the address in question
    auto p0 = outstandingReads.find(addr);
    // assert(p != outstandingReads.end());  >> mmm << turned it off

    // first in first out, which is not necessarily true, but it is
    // the best we can do at this point
    PacketPtr pkt = p->second.front();
    p->second.pop();

    if (p->second.empty())
        outstandingReads.erase(p);

    // no need to check for drain here as the next call will add a
    // response to the response queue straight away
    assert(nbrOutstandingReads != 0);
    --nbrOutstandingReads;

    // set Read data
    pkt->setData(dataPtr);
    //delete dataPtr
    delete dataPtr;

    // perform the actual memory access
    accessAndRespond(pkt);

    #endif

    #if 0   // >> mmm << ver1 (mixed)
    if (addr >= 0x140000000 && addr < 0x240000000) {
        DPRINTF(PIMAccess, "<mmm> read complete\n");
        for (int i=0; i<4; i++) {
            auto p = outstandingReads.find(addr + 8*i);
            PacketPtr pkt = p->second.front();
            p->second.pop();

            if (p->second.empty())
                outstandingReads.erase(p);
            --nbrOutstandingReads;

            pkt->setData(dataPtr + i*8);

            accessAndRespond(pkt);
        }
    } else {
        auto p = outstandingReads.find(addr);
        PacketPtr pkt = p->second.front();
        p->second.pop();

        if (p->second.empty())
            outstandingReads.erase(p);
        --nbrOutstandingReads;

        pkt->setData(dataPtr);

        accessAndRespond(pkt);
    }
    delete dataPtr;
    #endif

    #if 1   // >> mmm << ver2 (mixed + modified read)
    if (addr >= 0x140000000 && addr < 0x240000000) {
        DPRINTF(PIMAccess, "<mmm> read complete\n");

        auto p = outstandingReads.find(addr);
        PacketPtr pkt = p->second.front();
        p->second.pop();

        if (p->second.empty())
            outstandingReads.erase(p);
        --nbrOutstandingReads;

        Addr addr_32B_idx = addr % (system()->cacheLineSize());
        Addr addr_32B = addr - addr_32B_idx;
        if (pkt_cnt[addr_32B] == 1) {
            pkt->setData(dataPtr + addr_32B_idx);
            
            uint8_t *rd_DataPtr = new uint8_t[system()->cacheLineSize()];
            memcpy(rd_DataPtr, dataPtr, system()->cacheLineSize());

            pkt_rdQ[addr_32B] = rd_DataPtr;
        } else if (pkt_cnt[addr_32B] == 4) {
            pkt->setData(pkt_rdQ[addr_32B] + addr_32B_idx);
            pkt_rdQ.erase(addr_32B);
        } else {
            pkt->setData(pkt_rdQ[addr_32B] + addr_32B_idx);
        }
        accessAndRespond(pkt);
    } else {
        auto p = outstandingReads.find(addr);
        PacketPtr pkt = p->second.front();
        p->second.pop();

        if (p->second.empty())
            outstandingReads.erase(p);
        --nbrOutstandingReads;

        pkt->setData(dataPtr);

        accessAndRespond(pkt);
    }
    delete dataPtr;
    #endif
}

void PIMsim::writeComplete(unsigned id, uint64_t addr)
{
    DPRINTF(PIMsim, "Write to address %lld complete\n", addr);

    #if 0   // >> org <<
    // get the outstanding reads for the address in question
    auto p = outstandingWrites.find(addr);
    // assert(p != outstandingWrites.end());  // >> mmm << turned it off

    // we have already responded, and this is only to keep track of
    // what is outstanding
    p->second.pop();
    if (p->second.empty())
        outstandingWrites.erase(p);

    assert(nbrOutstandingWrites != 0);
    --nbrOutstandingWrites;

    if (nbrOutstanding() == 0)
        signalDrainDone();
    #endif

    #if 1   // >> mmm <<
    if (addr >= 0x140000000 && addr < 0x240000000) {
        DPRINTF(PIMAccess, "<mmm> write complete\n");
        for (int i=0; i<4; i++) {
            auto p = outstandingWrites.find(addr + 8*i);
            p->second.pop();
            if (p->second.empty())
                outstandingWrites.erase(p);

            --nbrOutstandingWrites;

            if (nbrOutstanding() == 0)
                signalDrainDone();
        }
    } else {
        auto p = outstandingWrites.find(addr);
        p->second.pop();
        if (p->second.empty())
            outstandingWrites.erase(p);

        --nbrOutstandingWrites;

        if (nbrOutstanding() == 0)
            signalDrainDone();
    }
    #endif
}

Port&
PIMsim::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return ClockedObject::getPort(if_name, idx);
    } else {
        return port;
    }
}

DrainState
PIMsim::drain()
{
    // check our outstanding reads and writes and if any they need to
    // drain
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

PIMsim::MemoryPort::MemoryPort(const std::string& _name,
                                 PIMsim& _memory)
    : ResponsePort(_name, &_memory), memory(_memory)
{ }

AddrRangeList
PIMsim::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(memory.getAddrRange());
    return ranges;
}

Tick
PIMsim::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return memory.recvAtomic(pkt);
}

void
PIMsim::MemoryPort::recvFunctional(PacketPtr pkt)
{
    memory.recvFunctional(pkt);
}

bool
PIMsim::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    // pass it to the memory controller
    return memory.recvTimingReq(pkt);
}

void
PIMsim::MemoryPort::recvRespRetry()
{
    memory.recvRespRetry();
}

} // namespace memory
} // namespace gem5
