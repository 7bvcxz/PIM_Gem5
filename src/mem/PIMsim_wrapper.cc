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
 *
 */

#include <cassert>

/**
 * When building the debug binary, we need to undo the command-line
 * definition of DEBUG not to clash with DRAMsim3 print macros that
 * are included for no obvious reason.
 */
#ifdef DEBUG
#undef DEBUG
#endif

#include "mem/PIMsim_wrapper.hh"

#include <fstream>

#include "PIMsim/src/dramsim3.h"
#include "base/compiler.hh"
#include "base/logging.hh"

namespace gem5
{

namespace memory
{

PIMsimWrapper::PIMsimWrapper(const std::string& config_file,
                                 const std::string& working_dir,
                                 std::function<void(uint64_t,uint8_t*)>
                                 read_cb,
                                 std::function<void(uint64_t)> write_cb) :
    PIMsim(dramsim3::GetMemorySystem(config_file, working_dir,
                                       read_cb, write_cb)),
    _clockPeriod(0.0), _queueSize(0), _burstSize(0)
{
    // there is no way of getting PIMsim to tell us what frequency
    // it is assuming, so we have to extract it ourselves
    _clockPeriod = PIMsim->GetTCK();

    if (!_clockPeriod)
        fatal("PIMsim wrapper failed to get clock\n");

    // we also need to know what transaction queue size PIMsim is
    // using so we can stall when responses are blocked
    _queueSize = PIMsim->GetQueueSize();

    if (!_queueSize)
        fatal("PIMsim wrapper failed to get queue size\n");


   // finally, get the data bus bits and burst length so we can add a
   // sanity check for the burst size
   unsigned int dataBusBits = PIMsim->GetBusBits();
   unsigned int burstLength = PIMsim->GetBurstLength();

   if (!dataBusBits || !burstLength)
       fatal("PIMsim wrapper failed to get burst size\n");

   _burstSize = dataBusBits * burstLength / 8;
}

PIMsimWrapper::~PIMsimWrapper()
{
    delete PIMsim;
}


void
PIMsimWrapper::printStats()
{
    PIMsim->PrintStats();
}

void
PIMsimWrapper::resetStats()
{
    PIMsim->ResetStats();
}

// void
// PIMsimWrapper::setCallbacks(std::function<void(uint64_t)> read_complete,
//                               std::function<void(uint64_t)> write_complete)
// {
//     PIMsim->RegisterCallbacks(read_complete, write_complete);
// }

bool
PIMsimWrapper::canAccept(uint64_t addr, bool is_write) const
{
    return PIMsim->WillAcceptTransaction(addr, is_write);
}

void
PIMsimWrapper::enqueue(uint64_t addr, bool is_write, uint8_t * DataPtr)
{
    M5_VAR_USED bool success = PIMsim->AddTransaction(addr,
                                                is_write, DataPtr);
    assert(success);
}

double
PIMsimWrapper::clockPeriod() const
{
    return _clockPeriod;
}

unsigned int
PIMsimWrapper::queueSize() const
{
    return _queueSize;
}

unsigned int
PIMsimWrapper::burstSize() const
{
    return _burstSize;
}

void
PIMsimWrapper::tick()
{
    PIMsim->ClockTick();
}

void
PIMsimWrapper::init(uint8_t* pmemAddr, uint64_t size)
{
    PIMsim->init(pmemAddr,size,_burstSize);
}

} // namespace memory
} // namespace gem5
