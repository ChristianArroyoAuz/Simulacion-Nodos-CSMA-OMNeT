//
// Copyright (C) 2013 Matti Schnurbusch (original code)
// Copyright (C) 2015 Michael Kirsche   (clean-up, ported for INET 2.x)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//
#pragma once

#ifndef IEEE802154Phy_H_
#define IEEE802154Phy_H_

#include <omnetpp.h>
#include <string.h>
#include <stdlib.h>
#include "PPDU_m.h"
#include "PhyPIB.h"
#include "MPDU_m.h"
#include "IEEE802154Enum.h"

#define phyEV (ev.isDisabled()||!phyDebug) ? EV : EV << "[802154_PHY]: "    // switchable debug output

class IEEE802154Phy : public cSimpleModule
{

    public:
        IEEE802154Phy();
        virtual ~IEEE802154Phy();

    protected:
        /** @brief Debug output switch for the IEEE 802.15.4 PHY module */
        bool phyDebug = false;

        virtual ppdu *generatePPDU(cMessage *psdu, bool ackFlag);
        virtual void initialize();
        virtual void handleMessage(cMessage *msg);
        void sendTrxConf(phyState status);
        void tokenizePages();
        void sendPhyPIB(int attr, int index);
        void setPhyPIB(GetConfirm *IEEE802154PhyPIBSet);
        void performCCA(unsigned short mode);
        void performED();

    private:
        phyState trxState;
        // Map to associate the strings with the enum values (cp. IEEE802154Enum.h)
        std::map<std::string, PIBMsgTypes> mappedMsgTypes;
        // Par inside of a msg
        //cMsgPar param;    // XXX parameter not needed anymore !?
        PhyPIB pib;

        //std::string tokens;      // XXX parameter not needed anymore !?
        unsigned int suppPages[3];
};

#endif /* IEEE802154Phy_H_ */
