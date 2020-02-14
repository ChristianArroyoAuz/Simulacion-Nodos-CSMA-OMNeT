//
// Copyright (C) 2008 Feng Chen         (original IEEE 802.15.4 MAC model)
// Copyright (C) 2013 Matti Schnurbusch (adaptation and extension of Feng Chen's model)
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

#include "IEEE802154Mac.h"

Define_Module(IEEE802154Mac);

void IEEE802154Mac::initialize(int stage)
{

    cSimpleModule::initialize(stage);
    if (stage == 0)
    {
        if (hasPar("macDebug"))
            macDebug = par("macDebug").boolValue();
        else
            macDebug = false;

        macEV << "Initializing Stage 0 \n";
        syncLoss = false;
        mpib = MacPIB();
        inTxSD_txSDTimer = false;
        scanning = false;
        scanStep = 0;
        bool secuOn = par("SecurityEnabled");
        mlmeRxEnable = false;
        txBuffSlot = 0;
        rxBuffSlot = 0;
        isCoordinator = false;
        associated = false;
        startNow = par("startWithoutStartReq");
        isFFD = par("isFFD");
        mpib.setMacSecurityEnabled(secuOn);
        mpib.setMacAssociatedPANCoord(false);
        mpib.setMacBeaconOrder(15);  // it will change if we receive a Beacon
        mpib.setMacAckWaitDuration(aUnitBackoffPeriod + aTurnaroundTime + ppib.getSHR() + (6 - ppib.getSymbols()));

        mpib.setMacRxOnWhenIdle(true);  // if not, we wont receive any Messages during CFP / CAP
        bool tempMode = par("promiscuousMode");
        mpib.setMacPromiscuousMode(tempMode);
        nb = NotificationBoardAccess().get();
        nb->subscribe(this, NF_RADIO_CHANNEL_CHANGED);

        mlmeReset = false;
        counter = 0;
        waitGTSConf = false;
        fcf = new macFrameControlField();
        myPANiD = 0xffffU;

        const char *addressString = par("macAddr");
        if (myMacAddr.isUnspecified())
        {
            macEV << "MAC myMacAddr is Unspecified...\n";
            if (!strcmp(addressString, "auto"))
            {
                // assign automatic address
                myMacAddr = MACAddressExt::generateAutoAddress();
                macEV << "Assigning generateAutoAddress=" << myMacAddr << endl;
                // change module parameter from "auto" to the auto-generated address
                par("macAddr").setStringValue(myMacAddr.str().c_str());
            }
            else
            {
                myMacAddr.setAddress(addressString);
                macEV << "Assigning User settings (macAddr Par in omnetpp.ini) =" << myMacAddr << endl;
            }
        }

        WATCH(myMacAddr);

        sequ = 0;
        trxState = false;
        headerSize = 0;
        //bitrate = 10;

        mappedMsgTypes["PLME-SET-TRX-STATE.confirm"] = SETTRXSTATE;
        mappedMsgTypes["PLME-GET.confirm"] = GET;
        mappedMsgTypes["PLME-SET.confirm"] = SET;
        mappedMsgTypes["PLME-CCA.confirm"] = CCA;
        mappedMsgTypes["PLME-ED.confirm"] = ED;
        mappedMsgTypes["PD-DATA.confirm"] = CONF;

        // all Request Message types which could come in from the upper Layer
        mappedMlmeRequestTypes["MLME-ASSOCIATE.request"] = MLMEASSOCIATE;
        mappedMlmeRequestTypes["MLME-DISASSOCIATE.request"] = MLMEDISASSOCIATE;
        mappedMlmeRequestTypes["MLME-GET.request"] = MLMEGET;
        mappedMlmeRequestTypes["MLME-GTS.request"] = MLMEGTS;
        mappedMlmeRequestTypes["MLME.RESET.request"] = MLMERESET;
        mappedMlmeRequestTypes["MLME-RX-ENABLE.request"] = MLMERXENABLE;
        mappedMlmeRequestTypes["MLME-SCAN.request"] = MLMESCAN;
        mappedMlmeRequestTypes["MLME-START.request"] = MLMESTART;
        mappedMlmeRequestTypes["MLME-SYNC-LOSS.request"] = MLMESYNC;
        mappedMlmeRequestTypes["MLME-POLL.request"] = MLMEPOLL;
        mappedMlmeRequestTypes["MLME-SET.request"] = MLMESET;
        mappedMlmeRequestTypes["MLME-ASSOCIATE.response"] = MLMEASSOCIATERESP;
        mappedMlmeRequestTypes["MLME-ORPHAN.response"] = MLMEORPHANRESP;

        // initialize timers
        backoffTimer = new cMessage("backoffTimer", MAC_BACKOFF_TIMER);
        deferCCATimer = new cMessage("deferCCATimer", MAC_DEFER_CCA_TIMER);
        bcnRxTimer = new cMessage("bcnRxTimer", MAC_BCN_RX_TIMER);
        bcnTxTimer = new cMessage("bcnTxTimer", MAC_BCN_TX_TIMER);
        ackTimeoutTimer = new cMessage("ackTimeoutTimer", MAC_ACK_TIMEOUT_TIMER);
        txAckBoundTimer = new cMessage("txAckBoundTimer", MAC_TX_ACK_BOUND_TIMER);
        txCmdDataBoundTimer = new cMessage("txCmdDataBoundTimer", MAC_TX_CMD_DATA_BOUND_TIMER);
        ifsTimer = new cMessage("ifsTimer", MAC_IFS_TIMER);
        txSDTimer = new cMessage("txSDTimer", MAC_TX_SD_TIMER);
        rxSDTimer = new cMessage("rxSDTimer", MAC_RX_SD_TIMER);
        finalCAPTimer = new cMessage("finalCAPTimer", MAC_FINAL_CAP_TIMER);
        scanTimer = new cMessage("scanDurationTimer", MAC_SCAN_TIMER);
        gtsTimer = new cMessage("gtsTimer", MAC_GTS_TIMER);

        if (true == startNow)
        {
            isCoordinator = par("isPANCoordinator");
            unsigned short tmpBo = par("BeaconOrder");
            mpib.setMacBeaconOrder(tmpBo);
            unsigned short tmpSo = par("SuperframeOrder");
            mpib.setMacSuperframeOrder(tmpSo);
        }
        dataTransMode = (Ieee802154TxOption) 0;
        panCoorName = par("panCoordinatorName").stdstringValue();
        isRecvGTS = par("isRecvGts");
        gtsPayload = par("gtsPayload");
        ack4Gts = par("ackForGts");

        // for beacon
        rxBO = 15;
        rxSO = 15;
        beaconWaitingTx = false;
        bcnLossCounter = 0;
        scanPANDescriptorList = new PAN_ELE[26];  // maximum size for our scanning result lists
        scanEnergyDetectList = new unsigned int[26];

        // for timer
        lastTime_bcnRxTimer = 0;
        inTxSD_txSDTimer = false;
        inRxSD_rxSDTimer = false;
        index_gtsTimer = 0;

        // for transmission
        inTransmission = false;
        waitBcnCmdAck = false;
        waitBcnCmdUpperAck = false;
        waitDataAck = false;
        waitGTSAck = false;
        numBcnCmdRetry = 0;
        numBcnCmdUpperRetry = 0;
        numDataRetry = 0;
        numGTSRetry = 0;

        // device capability
        capability.alterPANCoor = true;//ALTERNAR CORDINADOR**********************************
        capability.FFD = true;
        //capability.mainsPower     = false;
        capability.recvOnWhenIdle = mpib.getMacRxOnWhenIdle();
        capability.secuCapable = false;
        capability.alloShortAddr = true;
        capability.hostName = getParentModule()->getParentModule()->getParentModule()->getFullName();

        // GTS variables for PAN coordinator
        gtsCount = 0;
        for (int i = 0; i < 7; i++)
        {
            gtsList[i].devShortAddr = myMacAddr.getShortAddr();
            gtsList[i].startSlot = 0;
            gtsList[i].length = 0;
            gtsList[i].isRecvGTS = false;
            gtsList[i].isTxPending = false;
            gtsList[i].Type = false;
        }
        tmp_finalCap = aNumSuperframeSlots - 1;  // 15 if no GTS*************que signica???
        indexCurrGts = 99;

        // GTS variables for devices
        gtsLength = 0;
        gtsStartSlot = 0;
        gtsTransDuration = 0;

        /* for indirect transmission
         txPaFields.numShortAddr = 0;
         txPaFields.numExtendedAddr  = 0;
         rxPaFields.numShortAddr = 0;
         rxPaFields.numExtendedAddr = 0;
         */

        // pkt counter
        numUpperPkt = 0;
        numUpperPktLost = 0;
        numCollision = 0;
        numLostBcn = 0;
        numTxBcnPkt = 0;
        numTxDataSucc = 0;
        numTxDataFail = 0;
        numTxGTSSucc = 0;
        numTxGTSFail = 0;
        numTxAckPkt = 0;
        numRxBcnPkt = 0;
        numRxDataPkt = 0;
        numRxGTSPkt = 0;
        numRxAckPkt = 0;///Aqui esta el numero de ASK*********************

        numTxAckInactive = 0;

    }
    else if (stage == 1)
    {
        macEV << "Initializing Stage 1 \n";

        WATCH(bPeriod);
        WATCH(inTxSD_txSDTimer);
        WATCH(inRxSD_rxSDTimer);
        WATCH(numUpperPkt);
        WATCH(numUpperPktLost);
        WATCH(numCollision);
        WATCH(numLostBcn);
        WATCH(numTxBcnPkt);
        WATCH(numTxDataSucc);
        WATCH(numTxDataFail);
        WATCH(numTxGTSSucc);
        WATCH(numTxGTSFail);
        WATCH(numTxAckPkt);
        WATCH(numRxBcnPkt);
        WATCH(numRxDataPkt);
        WATCH(numRxGTSPkt);
        WATCH(numRxAckPkt);
        WATCH(numTxAckInactive);
        WATCH(isCoordinator);
        WATCH(associated);
        WATCH(scanning);

    }
    else if (stage == 2)
    {
        macEV << "Initializing Stage 2 \n";

        bcnRxTime = 0;
        if (true == startNow)
        {
            macEV << "startNow==true -> Starting up immediately \n";
            // lets start things
            if (true == isCoordinator)
            {
                if (false == isFFD)
                {
                    error("[MAC]: you want to start up a PAN Coordinator who is not an FFD!!!");
                }
                mpib.setMacCoordExtendedAddress(myMacAddr);
                mpib.setMacShortAddress(myMacAddr.getShortAddr());  // simply use MAC short address
                mpib.setMacPANId(myMacAddr.getInt());  // simply use MAC extended address
                mpib.setMacAssociationPermit(true);
                if (mpib.getMacBeaconOrder() < 15)
                {
                    // use superframe -> define slot duration and start beacon timer
                    txSfSlotDuration = aBaseSlotDuration * (1 << mpib.getMacSuperframeOrder());
                    startBcnTxTimer(true, simTime());  // send out first beacon ASAP
                }
                else if (mpib.getMacBeaconOrder() == 15)
                {
                    // don't use superframe (BO = 15) -> simply turn on receiver
                    genSetTrxState(phy_RX_ON);
                }
                else
                {
                    error("[MAC]: undefined Beacon Order found during startup");
                }
            }  // if == isCoordinator
            else
            {
                mpib.setMacPANId(0xffff);   // set broadcast PAN ID
                genSetTrxState(phy_RX_ON);
            }  // if != isCoordinator
        }
        else
        {
            macEV << "Waiting for MLME-START primitive from upper layer \n";
        }
    }

}

void IEEE802154Mac::receiveChangeNotification(int category, const cPolymorphic *details)
{
    Enter_Method_Silent
    ();
    //printNotificationBanner(category, details);

    switch (category)
    {
        case NF_RADIO_CHANNEL_CHANGED: {
            //ppib.setCurrChann(check_and_cast<RadioState *>(const_cast<cPolymorphic *>(details))->getChannelNumber());  // remove constness before dynamic_cast
            ppib.setCurrChann(check_and_cast<const RadioState *>(details)->getChannelNumber());
            phy_bitrate = getRate('b');
            phy_symbolrate = getRate('s');
            bPeriod = aUnitBackoffPeriod / phy_symbolrate;
            break;
        }
            // TODO: check and add other *Change Notifications*
            /* case NF_CHANNELS_SUPPORTED_CHANGED:
             ppib.phyChannelsSupported = check_and_cast<const Ieee802154RadioState *>(details)->getPhyChannelsSupported();
             break;

             case NF_TRANSMIT_POWER_CHANGED:
             ppib.phyTransmitPower = check_and_cast<const Ieee802154RadioState *>(details)->getPhyTransmitPower();
             break;

             case NF_CCA_MODE_CHANGED:
             ppib.phyCCAMode = check_and_cast<const Ieee802154RadioState *>(details)->getPhyCCAMode();
             break;
             */
        default: {
            break;
        }
    }  // switch
}

void IEEE802154Mac::setDefMpib()
{
    mpib = MacPIB();
    bool secuOn = par("SecurityEnabled");
    mpib.setMacSecurityEnabled(secuOn);
    mpib.setMacAckWaitDuration(aUnitBackoffPeriod + aTurnaroundTime + ppib.getSHR() + (6 - ppib.getSymbols()));
    unsigned short tmpBo = par("Beaconorder");
    mpib.setMacBeaconOrder(tmpBo);
    unsigned short tmpSo = par("Superframeorder");
    mpib.setMacSuperframeOrder(tmpSo);

    if (isCoordinator)
    {
        mpib.setMacShortAddress(myMacAddr.getShortAddr());  // simply use MAC extended address
        mpib.setMacPANId(myMacAddr.getInt());  // simply use MAC extended address
        mpib.setMacAssociationPermit(true);
        txSfSlotDuration = aBaseSlotDuration * (1 << mpib.getMacSuperframeOrder());
        startBcnTxTimer(true, simTime());  // send out first beacon ASAP
    }
    else
    {
        genSetTrxState(phy_RX_ON);
    }

    return;
}

void IEEE802154Mac::handleMessage(cMessage *msg)
{
    macEV << "Got Message " << msg->getName() << endl;
    if (msg->isSelfMessage())
    {
        handleSelfMsg(msg);
    }
    else if ((msg->arrivedOn("MCPS_SAP")) || (msg->arrivedOn("MLME_SAP")))
    {
        handleUpperMsg(msg);
    }
    else
    {
        handleLowerMsg(msg);
    }
    return;
}

void IEEE802154Mac::handleUpperMsg(cMessage *msg)
{
    // Data Msg
    if (dynamic_cast<StartRequest*>(msg))
    {
        macEV << "StartRequest arrived \n";

        StartRequest* startMsg = check_and_cast<StartRequest*>(msg);
        isCoordinator = startMsg->getPANCoordinator();
        // inform PHY
        GetConfirm* setChann = new GetConfirm("SET");
        setChann->setPIBattr(currentChannel);
        setChann->setValue(startMsg->getLogicalChannel());
        send(setChann, "outPLME");
        mpib.setMacBeaconOrder(startMsg->getBeaconOrder());
        mpib.setMacSuperframeOrder(startMsg->getSuperframeOrder());
        mpib.setMacBattLifeExt(startMsg->getBatteryLifeExtension());
        mpib.setMacPANId(startMsg->getPANId());  // use MAC extended address
        mpib.setMacAssociationPermit(true);
        if (isCoordinator)
        {
            par("isPANCoordinator").setBoolValue(isCoordinator);

            myMacAddr.genShortAddr();

            mpib.setMacCoordExtendedAddress(myMacAddr);
            mpib.setMacShortAddress(myMacAddr.getShortAddr());  // use MAC extended address

            txSfSlotDuration = aBaseSlotDuration * (1 << mpib.getMacSuperframeOrder());
            startBcnTxTimer(true, startMsg->getStartTime());  // send out first beacon ASAP
            genStartConf(mac_SUCCESS);
        }
        else
        {
            genSetTrxState(phy_RX_ON);
            genStartConf(mac_SUCCESS);
        }

        delete (startMsg);  // XXX solving undisposed object error startMsg

        // send a get request just that the buffer knows to forward next packet
        cMessage* buffMsg = new cMessage("Buffer-get-Elem");
        buffMsg->setKind(99);
        send(buffMsg, "outMCPS");

        return;
    }

    // Check if we are already processing a Msg
    if (taskP.taskStatus(TP_MCPS_DATA_REQUEST) || scanning)
    {
        macEV << "A " << msg->getName() << " (#" << numUpperPkt << ") received from upper layer, but dropped due to concurrent msg processing \n";
        numUpperPktLost++;
        delete msg;

        cMessage* buffMsg = new cMessage("Buffer-get-Elem");
        buffMsg->setKind(99);
        send(buffMsg, "outMCPS");
        return;
    }

    // Check if the packet has valid size
    if (dynamic_cast<cPacket *>(msg) != NULL)
    {
        cPacket *mcpsData = (cPacket*) msg;
        if (mcpsData->getByteLength() > aMaxMACFrameSize)
        {
            macEV << "A " << msg->getName() << " received from upper layer, but dropped it due to oversize \n";
            sendMCPSDataConf(mac_FRAME_TOO_LONG, msg->getId());
            delete msg;
            return;
        }
    }

    if (msg->arrivedOn("MCPS_SAP"))
    {
        // this must be a mcps-data.request
        mcpsDataReq* dataReq = check_and_cast<mcpsDataReq*>(msg);
        macEV << "MCPS-DATA.request arrived on MCPS_SAP for " << dataReq->getDstAddr().str() << endl;

        // set missing parameter and generate MPDU Data request
        mpdu* data = new mpdu("DATA.indication");
        data->setSrcPANid(mpib.getMacPANId());
        data->setSrc(myMacAddr);
        data->setByteLength(dataReq->getMsduLength());
        data->setDestPANid(mpib.getMacPANId());
        data->setDest(dataReq->getDstAddr());
        data->encapsulate(dataReq->decapsulate());
        data->setSqnr(dataReq->getMsduHandle());

        Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
        taskP.taskStatus(task) = true;
        switch (dataReq->getTxOptions())
        {
            case 0:  // direct CAP noAck
            {
                taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                data->setFcf(fcf->genFCF(Data, false, false, false, false, addrLong, 1, addrLong));
                data->setIsGTS(false);
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                ASSERT(txData == NULL);
                txData = data;
                csmacaEntry('d');
                break;
            }

            case 1:  // direct CAP ACK
            {
                taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                data->setFcf(fcf->genFCF(Data, false, false, true, false, addrLong, 1, addrLong));
                data->setIsGTS(false);
                waitDataAck = true;
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                ASSERT(txData == NULL);
                txData = data;
                csmacaEntry('d');
                break;
            }

            case 2:  // direct GTS noACK
            {
                data->setFcf(fcf->genFCF(Data, false, false, false, false, addrLong, 1, addrLong));
                taskP.mcps_data_request_TxOption = GTS_TRANS;
                data->setIsGTS(true);
                waitDataAck = false;
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                ASSERT(txGTS == NULL);
                txGTS = data;
                csmacaEntry('d');
                break;
            }

            case 3:  // direct GTS ACK
            {
                taskP.mcps_data_request_TxOption = GTS_TRANS;
                data->setFcf(fcf->genFCF(Data, false, false, true, false, addrLong, 1, addrLong));
                data->setIsGTS(true);
                waitDataAck = true;
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                ASSERT(txGTS == NULL);
                txGTS = data;
                csmacaEntry('d');
                break;
            }

            case 4:  // indirect noACK
            {
                taskP.mcps_data_request_TxOption = DIRECT_TRANS;  // it's still indirect
                data->setFcf(fcf->genFCF(Data, false, false, false, false, addrLong, 1, addrLong));
                data->setIsGTS(false);
                data->setIsIndirect(true);
                waitDataAck = false;
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                ASSERT(txData == NULL);
                txData = data;
                csmacaEntry('d');
                break;
            }

            case 5:  // indirect ACK
            {
                taskP.mcps_data_request_TxOption = DIRECT_TRANS;  // it's still indirect ...
                data->setFcf(fcf->genFCF(Data, false, false, true, false, addrLong, 1, addrLong));
                data->setIsGTS(false);
                data->setIsIndirect(true);
                waitDataAck = true;
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                ASSERT(txData == NULL);
                txData = data;
                csmacaEntry('d');
                break;
            }

            default:
            {
                error("[MAC]: got invalid TXoption in MCPS-DATA.request");
                break;
            }
        }  // switch

        delete(msg);        // fix for undisposed object message (incoming messages from MCPS_SAP)
        return;
    }  // if (msg->arrivedOn("MCPS_SAP"))
    else
    {
        // Has to be a MLME request Message from higher layer
        switch (mappedMlmeRequestTypes[msg->getName()])
        {
            case MLMEASSOCIATE:
            {
                if (isCoordinator)
                {
                    macEV << msg->getName() << "is dropped - PAN Coordinator won't associate... \n";
                    delete msg;
                    return;
                }
                else if (mpib.getMacAssociatedPANCoord())
                {
                    macEV << msg->getName() << "is dropped - Device is already associated - dissociate first... \n";
                    delete msg;
                    return;
                }
                else if (!isFFD)
                {
                    macEV << msg->getName() << "is dropped - RFD won't associate...\n";
                    delete msg;
                    return;
                }
                else
                {  // Update PHY and MAC Attributes

                    if (dynamic_cast<AssociationRequest *>(msg))
                    {
                        AssociationRequest* frame = check_and_cast<AssociationRequest *>(msg);
                        AssoCmdreq* AssoCommand = new AssoCmdreq("MLME-ASSOCIATE.request");
                        MACAddressExt dest;
                        switch (frame->getCoordAddrMode())
                        {
                            case addrShort: {
                                mpib.setMacCoordShortAddress(frame->getCoordAddress().getShortAddr());
                                dest.setShortAddr(mpib.getMacCoordShortAddress());
                                AssoCommand->setDest(dest);
                                AssoCommand->setFcf(fcf->genFCF(Command, mpib.getMacSecurityEnabled(), false, true, true, addrShort, 01, addrLong));
                                break;
                            }
                            case addrLong: {
                                mpib.setMacCoordExtendedAddress(frame->getCoordAddress());
                                AssoCommand->setDest(mpib.getMacCoordExtendedAddress());
                                AssoCommand->setFcf(fcf->genFCF(Command, mpib.getMacSecurityEnabled(), false, true, true, addrLong, 01, addrLong));
                                break;
                            }
                            default: {
                                macEV << msg->getName() << "is dropped - has unsupported Addressing Mode...\n";
                                delete msg;
                                return;
                            }
                        }
                        mpib.setMacPANId(frame->getCoordPANId().getShortAddr());
                        GetConfirm* phyPibSet = new GetConfirm("SET");
                        phyPibSet->setPIBattr((currentPage));
                        phyPibSet->setValue(frame->getChannelPage());
                        send(phyPibSet->dup(), "outPLME");
                        phyPibSet->setPIBattr(currentChannel);
                        phyPibSet->setValue(frame->getLogicalChannel());
                        send(phyPibSet, "outPLME");

                        ack4Asso = true;

                        // need to touch payload since LLC doesn't know my MAC Address Payload
                        DevCapability devCap = frame->getCapabilityInformation();
                        devCap.addr = myMacAddr;
                        AssoCommand->setCapabilityInformation(devCap);
                        AssoCommand->setCmdType(Ieee802154_ASSOCIATION_REQUEST);

                        // MHR
                        AssoCommand->setSqnr(mpib.getMacDSN());
                        sequ = mpib.getMacDSN();
                        sequ++;
                        mpib.setMacDSN(sequ);
                        AssoCommand->setDestPANid(mpib.getMacPANId());
                        AssoCommand->setSrc(myMacAddr);

                        if (mpib.getMacSecurityEnabled())
                        {
                            Ash assoAsh;
                            assoAsh.FrameCount = 1;
                            assoAsh.KeyIdentifier.KeyIndex = frame->getKeyIndex();
                            assoAsh.KeyIdentifier.KeySource = frame->getKeySource();
                            assoAsh.secu.KeyIdMode = frame->getKeyIdMode();
                            assoAsh.secu.Seculevel = frame->getSecurityLevel();
                            AssoCommand->setAsh(assoAsh);

                        }

                        mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
                        holdMe->encapsulate(AssoCommand);
                        holdMe->setDest(AssoCommand->getDest());
                        holdMe->setSrc(myMacAddr);
                        holdMe->setSrcPANid(mpib.getMacPANId());
                        holdMe->setFcf(fcf->genFCF(Command, false, false, false, false, addrLong, 0, addrLong));

                        Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                        taskP.taskStatus(task) = true;

                        // Associate Requests always direct to Coordinator
                        taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                        taskP.taskStep(task)++;
                        strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                        ASSERT(txData == NULL);
                        txData = holdMe;
                        csmacaEntry('d');

                        delete(msg);    // fix for undisposed object message
                        return;
                    }
                }
                break;
            }  // case MLMEASSOCIATE

            case MLMEASSOCIATERESP:
            {
                if (isCoordinator)
                {
                    AssociationResponse* assoResp = check_and_cast<AssociationResponse*>(msg);
                    // generate response command Msg
                    AssoCmdresp* assoCmdResp = new AssoCmdresp("");

                    assoCmdResp->setFcf(fcf->genFCF(Command, mpib.getMacSecurityEnabled(), false, false, mpib.getMacPANId(), addrLong, 0, addrLong));
                    int tempsqn = mpib.getMacDSN();
                    assoCmdResp->setSqnr(tempsqn);
                    tempsqn++;
                    mpib.setMacDSN(tempsqn);
                    assoCmdResp->setDest(assoResp->getAddr());
                    assoCmdResp->setSrcPANid(mpib.getMacPANId());
                    assoCmdResp->setSrc(myMacAddr);

                    if (mpib.getMacSecurityEnabled())
                    {
                        Ash ash;
                        ash.secu.KeyIdMode = assoResp->getKeyIdMode();
                        ash.secu.Seculevel = assoResp->getSecurityLevel();
                        ash.KeyIdentifier.KeyIndex = assoResp->getKeyIndex();
                        ash.KeyIdentifier.KeySource = assoResp->getKeySource();
                        ash.FrameCount = 1;
                        assoCmdResp->setAsh(ash);
                    }

                    assoCmdResp->setCmdType(Ieee802154_ASSOCIATION_RESPONSE);
                    assoCmdResp->setShortAddress(assoResp->getAddr().getShortAddr());
                    assoCmdResp->setStatus(assoResp->getStatus());

                    Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                    taskP.taskStatus(task) = true;
                    mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
                    holdMe->encapsulate(assoCmdResp);
                    holdMe->setDest(assoCmdResp->getDest());
                    holdMe->setSrc(assoCmdResp->getSrc());
                    holdMe->setSrcPANid(assoCmdResp->getSrcPANid());
                    holdMe->setFcf(assoCmdResp->getFcf());

                    switch (dataTransMode)
                    {
                        case DIRECT_TRANS:
                        case INDIRECT_TRANS: {
                            taskP.taskStep(task)++;
                            strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                            ASSERT(txData == NULL);
                            txData = holdMe;
                            csmacaEntry('d');
                            break;
                        }

                        case GTS_TRANS: {
                            taskP.taskStep(task)++;
                            // waiting for GTS arriving, callback from handleGtsTimer()
                            strcpy(taskP.taskFrFunc(task), "handleGtsTimer");
                            ASSERT(txData == NULL);
                            txData = holdMe;
                            numGTSRetry = 0;

                            // if I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                            // if I'm the device, should try to transmit if now is in my GTS
                            // refer to 802.15.4-2006 Spec. 7.5.7.3
                            if (index_gtsTimer == 99)
                            {
                                ASSERT(gtsTimer->isScheduled());
                                // first check if the requested transaction can be completed before the end of current GTS
                                if (gtsCanProceed())
                                {
                                    // directly hand over to FSM, which will go to next step, state parameters are ignored
                                    FSM_MCPS_DATA_request();
                                }
                            }
                            break;
                        }

                        default: {
                            error("[IEEE802154MAC]: undefined txOption: %d!", dataTransMode);
                        }
                    }  // switch (dataTransMode)
                }  // if isCoordinator
                else
                {
                    macEV << "Device received 'Association Response' from Higher Layer - How could that happen?\n";
                }

                delete(msg);    // fix for undisposed object message
                return;
            }  // case MLMEASSOCIATERESP

            case MLMEDISASSOCIATE:
            {
                DisAssociation* disAss = check_and_cast<DisAssociation*>(msg);
                tmpDisAss = disAss;
                if (disAss->getDevicePANId() == mpib.getMacPANId())
                {
                    if (isCoordinator)
                    {
                        genDisAssoCmd(disAss, disAss->getTxIndirect());
                    }
                    else
                    {
                        if (((disAss->getDeviceAddrMode() == addrLong) && (disAss->getDeviceAddress() == mpib.getMacCoordExtendedAddress()))
                         || ((disAss->getDeviceAddrMode() == addrShort) && disAss->getDeviceAddress().getShortAddr() == mpib.getMacCoordShortAddress()))
                        {
                            genDisAssoCmd(disAss, false);
                        }

                        else
                        {
                            genMLMEDisasConf(mac_INVALID_PARAMETER);
                        }
                    }
                }
                else
                {
                    genMLMEDisasConf(mac_INVALID_PARAMETER);
                }
                break;
            }  // case MLMEDISASSOCIATE

            case MLMEGET:
            {
                GetRequest* GETrequ = check_and_cast<GetRequest*>(msg);
                send(GETrequ, "outPLME");
                break;
            }  // case MLMEGET

            case MLMEGTS:
            {
                if (isCoordinator)
                {
                    macEV << "Coordinator got an MLME GTS Request dropping it!\n";
                }
                else
                {
                    waitGTSConf = true;
                    GTSMessage* GTSrequ = check_and_cast<GTSMessage*>(msg);
                    GTSCmd* gtsRequCmd = new GTSCmd("GTS-request-command");
                    if (associated)
                    {
                        gtsRequCmd->setFcf(fcf->genFCF(Command, mpib.getMacSecurityEnabled(), false, true, false, addrShort, 0, addrShort));
                    }
                    else
                    {
                        genGTSConf(GTSrequ->getGTSCharacteristics(), mac_NO_SHORT_ADDRESS);
                        delete msg;
                        delete GTSrequ;
                        delete gtsRequCmd;
                        return;
                    }
                    int sqnr = mpib.getMacDSN();
                    gtsRequCmd->setSqnr(sqnr);
                    sqnr++;
                    mpib.setMacDSN(sqnr);
                    gtsRequCmd->setDestPANid(mpib.getMacPANId());
                    gtsRequCmd->setDest(mpib.getMacCoordExtendedAddress());
                    gtsRequCmd->setSrcPANid(mpib.getMacPANId());
                    gtsRequCmd->setSrc(myMacAddr);
                    if (mpib.getMacSecurityEnabled())
                    {
                        Ash ash;
                        ash.FrameCount = 1;
                        ash.KeyIdentifier.KeyIndex = GTSrequ->getKeyIndex();
                        ash.KeyIdentifier.KeySource = GTSrequ->getKeySource();
                        ash.secu.KeyIdMode = GTSrequ->getKeyIdMode();
                        ash.secu.Seculevel = GTSrequ->getSecurityLevel();
                        gtsRequCmd->setAsh(ash);
                    }
                    GTSDescriptor desc;
                    desc.devShortAddr = myMacAddr.getShortAddr();
                    desc.Type = true;
                    desc.length = GTSrequ->getGTSCharacteristics().length;
                    gtsRequCmd->setGTSCharacteristics(desc);
                    gtsRequCmd->setFcs(0);
                    Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                    taskP.taskStatus(task) = true;
                    mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
                    holdMe->encapsulate(gtsRequCmd);
                    holdMe->setDest(gtsRequCmd->getDest());
                    holdMe->setSrc(gtsRequCmd->getSrc());
                    holdMe->setSrcPANid(gtsRequCmd->getSrcPANid());
                    holdMe->setFcf(gtsRequCmd->getFcf());

                    switch (dataTransMode)
                    {
                        case DIRECT_TRANS:
                        case INDIRECT_TRANS: {
                            taskP.taskStep(task)++;strcpy
                            (taskP.taskFrFunc(task), "handle_PD_DATA_request");
                            ASSERT(txData == NULL);
                            txData = holdMe;
                            csmacaEntry('d');
                            break;
                        }

                        case GTS_TRANS: {
                            taskP.taskStep(task)++;
                            // waiting for GTS arriving, callback from handleGtsTimer()
                            strcpy
                            (taskP.taskFrFunc(task), "handleGtsTimer");
                            ASSERT(txData == NULL);
                            txData = holdMe;
                            numGTSRetry = 0;

                            // if I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                            // if I'm the device, should try to transmit if now is in my GTS
                            // refer to Spec. 7.5.7.3
                            if (index_gtsTimer == 99)
                            {
                                ASSERT(gtsTimer->isScheduled());
                                // first check if the requested transaction can be completed before the end of current GTS
                                if (gtsCanProceed())
                                {
                                    // directly hand over to FSM, which will go to next step, state parameters are ignored
                                    FSM_MCPS_DATA_request();
                                }
                            }
                            break;
                        }

                        default:
                            error("[MAC]: undefined txOption: %d!", dataTransMode);
                    }  // switch (dataTransMode)
                }
                break;
            }  // case MLMEGTS

            case MLMERESET:
            {
                MLMEReset* resetRequ = check_and_cast<MLMEReset*>(msg);
                if (resetRequ->getSetDefaultPIB())
                {
                    genSetTrxState(phy_FORCE_TRX_OFF);
                    mlmeReset = true;
                }
                else
                {
                    macEV << "Got a Reset.request with status false drop \n";
                    delete msg;
                    return;
                }

                break;
            }  // case MLMERESET

            case MLMERXENABLE:
            {
                RxEnableRequest* rxReq = check_and_cast<RxEnableRequest*>(msg);
                if (dataTransMode == INDIRECT_TRANS)
                {  // ignore defer

                }
                else
                {
                    // calculate time before receiver is enabled + time for which the receiver is enabled > than the superframe duration
                    if (rxReq->getRxOnTime() + rxReq->getRxOnDuration() > aBaseSuperframeDuration * pow(2, mpib.getMacBeaconOrder()))
                    {
                        RxEnableConfirm* conf = new RxEnableConfirm("MLME-RX-ENABLE.confirm");
                        conf->setStatus(rx_ON_TIME_TOO_LONG);
                        send(conf, "outMLME");
                        delete(msg);    // fix for undisposed object message
                        return;
                    }
                    //else if (((simtime_t)(rxReq->getRxOnTime() - aTurnaroundTime)) < (bcnRxTime - simTime())) // TODO adapt to symbols to simtime_t conversion
                    else if ((rxReq->getRxOnTime() - aTurnaroundTime) < (bcnRxTime - simTime())) // TODO adapt to symbols to simtime_t conversion
                    {
                        // there shouldn't be anything transmitting or receiving
                        genSetTrxState(phy_RX_ON);
                        mlmeRxEnable = true;
                        startRxOnDurationTimer(rxReq->getRxOnDuration());
                        delete(msg);    // fix for undisposed object message
                        return;
                    }
                    else if (rxReq->getDeferPermit())
                    {
                        startdeferRxEnableTimer(rxReq->getRxOnTime() - aTurnaroundTime);
                        deferRxEnable = rxReq;
                        delete(msg);    // fix for undisposed object message
                        return;
                    }
                    else
                    {
                        RxEnableConfirm* conf = new RxEnableConfirm("MLME-RX-ENABLE.confirm");
                        conf->setStatus(rx_PAST_TIME);
                        send(conf, "outMLME");
                        delete (msg);
                        return;
                    }
                }
                break;
            }  // case MLMERXENABLE

            case MLMESCAN:
            {
                ScanRequest* scanReq = check_and_cast<ScanRequest*>(msg);

                if (scanning)
                {
                    // send a confirm for upper layer without changing our status variables
                    ScanConfirm* scanConf = new ScanConfirm("MLME-SCAN.confirm");
                    scanConf->setStatus(scan_SCAN_IN_PROGRESS);
                    scanConf->setScanType(scanReq->getScanType());
                    scanConf->setChannelPage(scanReq->getChannelPage());
                    scanConf->setUnscannedChannels(134217727);  // 27 channels unscanned
                    scanConf->setResultListSize(0);
                    send(scanConf, "outMLME");
                }
                else
                {
                    scanning = true;
                    scanType = (ScanType) scanReq->getScanType();
                    if (!isFFD)
                    {
                        if (scanType == scan_ORPHAN)
                        {
                            macEV << "RFD do not support an orphan scan \n";
                            ScanConfirm* scanConf = new ScanConfirm("MLME-SCAN.confirm");
                            scanConf->setStatus(scan_INVALID_PARAMETER);
                            scanConf->setScanType(scanReq->getScanType());
                            scanConf->setChannelPage(scanReq->getChannelPage());
                            scanConf->setUnscannedChannels(134217727);  // 27 channels unscanned
                            scanConf->setResultListSize(0);
                            send(scanConf, "outMLME");
                        }
                    }
                    scanDuration = scanReq->getScanDuration();
                    scanChannels = scanReq->getScanChannels();
                    channelPage = scanReq->getChannelPage();
                    scanSteps = scanChannels;
                    scanCount = 0;
                    scanResultListSize = 0;
                    doScan();
                }

                delete (scanReq);    // XXX fix for undisposed object: (ScanRequest) net.IEEE802154Nodes[0].NIC.MAC.IEEE802154Mac.MLME-SCAN.request
                break;
            }  // case MLMESCAN

            case MLMESET:
            {
                GetConfirm* SetReq = check_and_cast<GetConfirm*>(msg);
                send(SetReq, "outPLME");
                break;
            }  // case MLMESET

            case MLMESYNC:
            {
                SyncRequest* syncMsg = check_and_cast<SyncRequest*>(msg);
                syncLoss = true;

                GetConfirm* SetReq = new GetConfirm("PLME-SET.request");
                SetReq->setPIBattr(currentChannel);
                SetReq->setValue(syncMsg->getLogicalChannel());
                send(SetReq, "outPLME");

                // TODO check necessary operations to support sync loss

                break;
            }  // case MLMESYNC

            case MLMEPOLL:
            {
                int sqnr;
                PollRequest* pollMsg = check_and_cast<PollRequest*>(msg);
                CmdFrame* dataReq = new CmdFrame("MCPS-DATA.request");
                dataReq->setFcf(fcf->genFCF(Command, false, false, false, false, addrLong, 0, addrLong));
                dataReq->setSrc(myMacAddr);
                dataReq->setSrcPANid(mpib.getMacPANId());
                dataReq->setDest(pollMsg->getCoordAddress());
                sqnr = mpib.getMacDSN();
                dataReq->setSqnr(sqnr);
                sqnr++;
                mpib.setMacDSN(sqnr);
                dataReq->setCmdType(Ieee802154_DATA_REQUEST);

                mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
                holdMe->encapsulate(dataReq);
                holdMe->setDest(dataReq->getDest());
                holdMe->setSrc(dataReq->getSrc());
                holdMe->setSrcPANid(dataReq->getSrcPANid());
                holdMe->setFcf(dataReq->getFcf());
                txData = holdMe;
                Poll = true;
                csmacaEntry('d');
                break;
            }  // case MLMEPOLL

            case MLMEORPHANRESP:
            {
                OrphanResponse* oR = check_and_cast<OrphanResponse*>(msg);
                mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
                holdMe->encapsulate(oR);
                holdMe->setDest(MACAddressExt::BROADCAST_ADDRESS);
                holdMe->setSrc(myMacAddr);
                holdMe->setSrcPANid(mpib.getMacPANId());
                holdMe->setFcf(fcf->genFCF(Command, false, false, false, false, addrLong, 0, addrLong));
                txData = holdMe;
                csmacaEntry('d');
                break;
            }  // case MLMEORPHANRESP

            default:
            {
                error("MAC has unknown MLME - Message Type from Higher Layer !!");
                break;
            }  // default
        }  // switch
    }  // else
}

void IEEE802154Mac::handleLowerMsg(cMessage *msg)
{
    if (dynamic_cast<GetConfirm *>(msg))
    {
        if (scanning)
        {
            doScan();
            delete (msg);    // XXX fix for undisposed object: (GetConfirm) net.IEEE802154Nodes[0].NIC.MAC.IEEE802154Mac.PLME-SET.confirm
        }
        else
        {
            send(msg, "outMLME");
        }
        return;
    }

    if (dynamic_cast<CmdFrame*>(msg))
    {
        CmdFrame* bcnReq = check_and_cast<CmdFrame*>(msg);
        if (bcnReq->getCmdType() == Ieee802154_BEACON_REQUEST)
        {  // must be a beacon request
            if (isCoordinator)
            {
                macEV << "Coordinator is answering with a Beacon frame for this Device \n";

                //--- construct a beacon ---
                beaconFrame* tmpBcn = new beaconFrame();
                tmpBcn->setName("Ieee802154BEACONReply");

                tmpBcn->setSqnr((mpib.getMacBSN() + 1));
                tmpBcn->setSrc(myMacAddr);
                tmpBcn->setDestPANid(0xffff);  // ignored upon reception
                tmpBcn->setDest(bcnReq->getSrc());  // ignored upon reception
                tmpBcn->setSrcPANid(mpib.getMacPANId());

                // construct Superframe specification
                txSfSpec.BO = mpib.getMacBeaconOrder();
                txSfSpec.BI = aBaseSuperframeDuration * (1 << mpib.getMacBeaconOrder());
                txSfSpec.SO = mpib.getMacSuperframeOrder();
                txSfSpec.SD = aBaseSuperframeDuration * (1 << mpib.getMacSuperframeOrder());
                txSfSpec.battLifeExt = mpib.getMacBattLifeExt();
                txSfSpec.panCoor = isCoordinator;
                txSfSpec.assoPmt = mpib.getMacAssociationPermit();

                // this parameter may vary each time when new GTS slots were allocated in last superframe
                txSfSpec.finalCap = tmp_finalCap;
                tmpBcn->setSfSpec(txSfSpec);
                if (txBcnCmd != NULL)
                {  // will be released in PD_DATA_confirm
                    macEV << "Coordinator is already trying to answer on BEACON.request - putting this BEACON to queue \n";
                    txBuff.add(tmpBcn);
                    return;
                }
                txBcnCmd = tmpBcn;
                // Beacon is send direct as a usual data packet
                Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                taskP.taskStatus(task) = true;
                taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");

                csmacaEntry('c');

                return;
            }
            else
            {
                macEV << "Not a Coordinator - dropping Msg \n";
                delete msg;
                return;
            }
        }

    }
    else if ((!msg->isPacket()) && msg->getKind() > CONF)
    {
        handle_PLME_SET_TRX_STATE_confirm((phyState) msg->getKind());
        delete (msg);
        return;
    }
    else if (dynamic_cast<AckFrame *>(msg))
    {
        handleAck(msg);
        return;
    }
    else if (mappedMsgTypes[msg->getName()] == CONF)
    {
        handle_PD_DATA_confirm((phyState) msg->getKind());
        delete (msg);
        return;
    }
    else if (mappedMsgTypes[msg->getName()] == CCA)
    {
        handle_PLME_CCA_confirm((phyState) msg->getKind());
        delete (msg);
        return;
    }
    else if (dynamic_cast<edConf*>(msg))
    {
        edConf* edConfirmation = check_and_cast<edConf*>(msg);
        if (scanning)
        {
            scanEnergyDetectList[scanResultListSize] = edConfirmation->getEnergyLevel();
            scanResultListSize++;
        }
        else
        {
            send(edConfirmation, "outMLME");
        }
        return;
    }
    else if (dynamic_cast<OrphanIndication*>(msg))
    {
        if (isCoordinator)
        {
            send(msg, "outMLME");
        }
        else
        {
            macEV << "Dropping Orphan notify since we are NOT a Coordinator \n";
            delete msg;
            return;
        }
    }
    else if (dynamic_cast<OrphanResponse*>(msg))
    {
        if (isCoordinator)
        {
            macEV << "Dropping Orphan response since we are a Coordinator \n";
            delete msg;
            return;
        }
        else
        {
            send(msg, "outMLME");
        }
    }

    mpdu* frame = check_and_cast<mpdu *>(msg);
    if (!frame)
    {
        macEV << "Message from PHY (" << msg->getClassName() << ")" << msg->getName() << " is not any known subclass, drop it \n";
        delete frame;
        return;
    }

    unsigned short frmCtrl = frame->getFcf();
    frameType frmType;
    frmType = (frameType) ((frmCtrl & ftMask) >> 13);

    macEV << (frmType == Beacon ? "Beacon" : (frmType == Data ? "Data" : (frmType == Ack ? "Ack" : "Command" ) ) )
          << " frame received from the PHY layer, performing filtering now... \n";
    // perform MAC frame filtering
    if (filter(frame))
    {
        macEV << "The received frame is filtered, drop frame \n";
        delete frame;
        return;
    }

    // check timing for GTS (debug)
    if (frmType == Data && frame->getIsGTS())
    {
        if (isCoordinator)
        {
            // check if I'm supposed to receive the data from this device in this GTS
            if (indexCurrGts == 99 || gtsList[indexCurrGts].isRecvGTS || gtsList[indexCurrGts].devShortAddr != frame->getSrc().getShortAddr())
                error("[GTS]: timing error, PAN coordinator is not supposed to receive this DATA packet at this time!");
        }
        else
        {
            if (index_gtsTimer != 99 || !isRecvGTS || frame->getSrc().getShortAddr() != mpib.getMacCoordShortAddress())
                error("[GTS]: timing error, the device is not supposed to receive this DATA packet at this time!");
        }
    }

    macEV << "Checking if the received frame requires an ACK \n";
    //bool noAck = true;      // XXX shifted inside the following check to reduce variable scope

    // send an acknowledgment if needed (no matter if this is a duplicated packet or not)
    if ((frmType == Data) || (frmType == Command))
    {
        if ((frmCtrl & 1024) >> arequShift)  // acknowledgment required
        {
            bool noAck = false; // XXX fix to reduce variable scope
            // MAC layer can process only one command (RX or TX) at a time
            if (frmType == Command)
            {
                if ((rxCmd) || (txBcnCmd))
                {
                    noAck = true;
                }
            }
            if (!noAck)
            {
                macEV << "Yes, ACK required, constructing the ACK frame \n";
                genACK(frame, false);
                // stop CSMA-CA if it is pending (it will be restored after the transmission of ACK)
                if (backoffStatus == 99)
                {
                    macEV << "CSMA-CA is pending, stop it, it will resume after sending ACK \n";
                    backoffStatus = 0;
                    csmacaCancel();
                }
                macEV << "Prepare to send the ACK, ask PHY layer to turn on the transmitter first \n";
                genSetTrxState(phy_TX_ON);
            }
        }
        else  // no ACK required
        {
            if (frame->getIsGTS())  // received in GTS
            {
                if (isCoordinator)
                {
                    // the device may transmit more packets in this GTS, turn on radio
                    genSetTrxState(phy_RX_ON);
                }
                else
                {
                    // PAN coordinator can transmit only one packets to me in my GTS, turn off radio now
                    genSetTrxState(phy_TRX_OFF);
                }
            }
            else
                resetTRX();
        }
    }

    // drop new received command packet if MAC is currently processing a command packet
    if (frmType == Command)
    {
        if ((rxCmd) || (txBcnCmd))
        {
            macEV << "Received CMD frame is dropped, because MAC is currently processing a MAC CMD \n";
            delete frame;
            return;
        }
    }

    // drop new received data packet if MAC is current processing last received data packet
    if (frmType == Data)
    {
        if (rxData)
        {
            macEV << "Received DATA frame is dropped, because MAC is currently processing the last received DATA frame \n";
            delete frame;
            return;
        }
    }

    switch (frmType)
    {
        case Beacon: {
            macEV << "Continue with processing received Beacon frame \n";
            handleBeacon(frame);
            break;
        }

        case Data: {
            macEV << "Continue with processing received DATA packet \n";
            handleData(frame);
            break;
        }

        case Command: {
            macEV << "Continue with processing received CMD packet \n";
            handleCommand(frame);
            break;
        }

        default: {
            error("[IEEE802154MAC]: undefined MAC frame type: %d \n", frmType);
            return;
        }
    }

}

void IEEE802154Mac::handleSelfMsg(cMessage* msg)
{
    switch (msg->getKind())
    {
        case MAC_SCAN_TIMER:
            handleScanTimer();
            break;

        case MAC_BACKOFF_TIMER:
            handleBackoffTimer();
            break;

        case MAC_DEFER_CCA_TIMER:
            handleDeferCCATimer();
            break;

        case MAC_BCN_RX_TIMER:
            handleBcnRxTimer();
            break;

        case MAC_BCN_TX_TIMER:
            handleBcnTxTimer();
            break;

        case MAC_ACK_TIMEOUT_TIMER:
            handleAckTimeoutTimer();
            break;

        case MAC_TX_ACK_BOUND_TIMER:
            handleTxAckBoundTimer();
            break;

        case MAC_TX_CMD_DATA_BOUND_TIMER:
            handleTxCmdDataBoundTimer();
            break;

        case MAC_IFS_TIMER:
            handleIfsTimer();
            break;

        case MAC_TX_SD_TIMER:
        case MAC_RX_SD_TIMER:
            handleSDTimer();
            break;

        case MAC_FINAL_CAP_TIMER:
            handleFinalCapTimer();
            break;

        case MAC_GTS_TIMER:
            handleGtsTimer();
            break;

        default:
            error("[MAC]: unknown MAC timer type!");
            break;
    }
    return;
}

void IEEE802154Mac::handleBeacon(mpdu *frame)
{
    macEV << "Starting processing received Beacon frame \n";
    beaconFrame *bcnFrame = check_and_cast<beaconFrame *>(frame);
    //bool pending;
    simtime_t now, duration, tmpf, w_time;
    //int i, dataFrmLength;
    now = simTime();
    if (scanning)
    {
        PAN_ELE bcn;
        bcn.CoordAddrMode = (bcnFrame->getFcf() & 3);
        bcn.CoordAddress = bcnFrame->getSrc();
        bcn.CoordPANId = bcnFrame->getSrcPANid();
        bcn.GTSPermit = (bcnFrame->getGtsListArraySize() > 0 ? true : false);
        bcn.LinkQuality = frame->getKind();
        bcn.LogicalChannel = scanCount;
        bcn.SecurityFailure = false;
        bcn.SecurityUse = ((bcnFrame->getFcf() & 4096) >> 12);
        scanPANDescriptorList[scanResultListSize] = bcn;
        scanResultListSize++;
        macEV << "Beacon frame was processed while scanning the channel - scanResultList was updated \n";
    }
    else
    {
        unsigned char ifs;  // XXX fix to reduce variable scope
        unsigned short frmCtrl = frame->getFcf();   // XXX fix to reduce variable scope
        // update beacon parameters
        rxSfSpec = bcnFrame->getSfSpec();
        rxBO = rxSfSpec.BO;
        rxSO = rxSfSpec.SO;
        rxSfSlotDuration = aBaseSlotDuration * (1 << rxSO);

        // calculate the time when the first bit of the beacon was received
        duration = calDuration(frame);
        bcnRxTime = now - duration;
        schedBcnRxTime = bcnRxTime; // important: this value is calculated in <csmacaStart()>, if later a CSMA-CA is pending for this beacon and backoff will resume without calling <csmacaStart()>
        // (see <csmacaTrxBeacon()>) , therefore this value will not be updated, but <csmacaCanProceed()> and other functions will use it and need to be updated here
        macEV << "The first bit of this beacon was received by PHY layer at " << bcnRxTime << endl;

        // calculate <rxBcnDuration>
        if (bcnFrame->getByteLength() <= aMaxSIFSFrameSize)
        {
            ifs = aMinSIFSPeriod;
        }
        else
        {
            ifs = aMinLIFSPeriod;
        }

        tmpf = duration * phy_symbolrate;
        tmpf += ifs;
        rxBcnDuration = (unsigned short) (tmpf.dbl() / aUnitBackoffPeriod);
        if (fmod(tmpf, aUnitBackoffPeriod) > 0.0)
        {
            rxBcnDuration++;
        }

        // update PAN descriptor
        rxPanDescriptor.CoordAddrMode = (frmCtrl & 3);
        rxPanDescriptor.CoordPANId = frame->getSrcPANid();

        // mpib.setMacPANId(frame->getSrcPANid());
        rxPanDescriptor.CoordAddress = frame->getSrc();
        rxPanDescriptor.LogicalChannel = ppib.getCurrChann();

        // start rxSDTimer
        startRxSDTimer();
        startBcnRxTimer();

        // reset lost beacon counter
        bcnLossCounter = 0;

        // can start my GTS timer only after receiving the second beacon
        if (gtsStartSlot != 0)
        {
            tmpf = bcnRxTime + gtsStartSlot * rxSfSlotDuration / phy_symbolrate;
            w_time = tmpf - now;
            // should turn on radio 'aTurnaroundTime symbols' before GTS starts, if I received a GTS
            if (isRecvGTS)
            {
                w_time = w_time - aTurnaroundTime / phy_symbolrate;
            }
            macEV << "Schedule for my GTS with start slot #" << (int) gtsStartSlot << "\n";
            startGtsTimer(w_time);

            if (waitGTSConf)
            {
                int i = 0;      // XXX fix to reduce variable scope

                while (bcnFrame->getGtsList(i).devShortAddr != myMacAddr.getShortAddr() && i < 7)
                {
                    i++;
                }
                if (i < 7)
                {
                    genGTSConf(bcnFrame->getGtsList(i), mac_SUCCESS);
                }
            }
            // if my GTS is not the first one in the CFP, should turn radio off at the end of CAP using finalCAPTimer
            if (gtsStartSlot != rxSfSpec.finalCap + 1)
            {
                ASSERT(gtsStartSlot > rxSfSpec.finalCap);
                tmpf = bcnRxTime + (rxSfSpec.finalCap + 1) * rxSfSlotDuration / phy_symbolrate;
                w_time = tmpf - now;
                macEV << "My GTS is not the first one in the CFP, schedule a timer to turn off radio at the end of CAP \n";
                startFinalCapTimer(w_time);
            }
        }

        dispatch(phy_SUCCESS, __FUNCTION__);

        //CSMA-CA may be waiting for the new beacon
        if (backoffStatus == 99)
        {
            csmacaTrxBeacon('r');
        }
        genBeaconInd(frame);        // inform higher layer
        resetTRX();
    }
    delete bcnFrame;
    numRxBcnPkt++;
    return;
}

void IEEE802154Mac::handleData(mpdu* frame)
{
    unsigned short frmCtrl = frame->getFcf();
    // pass the data packet to upper layer
    // (we need some time to process the packet -- so delay SIFS/LIFS symbols from now or after finishing sending the ACK)
    // (refer to Figure 68 of the 802.15.4-2006 Spec. for details of SIFS/LIFS)
    ASSERT(rxData == NULL);
    rxData = frame;

    if (!((frmCtrl & 1024) >> 10))
    {
        bool isSIFS = false;    // XXX fix to reduce variable scope

        if (frame->getByteLength() <= aMaxSIFSFrameSize)
        {
            isSIFS = true;
        }
        startIfsTimer(isSIFS);
    }
    return;
}

void IEEE802154Mac::handleAck(cMessage* frame)
{
    AckFrame* ack = check_and_cast<AckFrame*>(frame);
    macEV << "Got ACK for #>" << (unsigned int) ack->getSqnr() << endl;
    if (Poll)
    {
        if (ack->getFcf() & 2048 >> fpShift)
        {
            genPollConf(mac_SUCCESS);

        }
        else
        {
            genPollConf(mac_NO_DATA);
        }
    }
    delete (frame);
    return;
}

void IEEE802154Mac::handleCommand(mpdu* frame)
{
    // all commands in this environment are encapsulated in MPDU
    CmdFrame* cmdFrame = check_and_cast<CmdFrame*>(frame->decapsulate());
    switch (cmdFrame->getCmdType())
    {
        case Ieee802154_ASSOCIATION_RESPONSE: {
            if (isCoordinator)
            {
                // Discard
                macEV << "Coordinator got a Associate Response dropping it \n";
                delete frame;
                return;
            }
            else
            {
                genACK(frame, false);
                AssociationConfirm* assoConf = new AssociationConfirm("MLME-ASSOCIATE.confirm");
                AssoCmdresp* aresp = check_and_cast<AssoCmdresp *>(cmdFrame);
                assoConf->setStatus(aresp->getStatus());

                if (mpib.getMacSecurityEnabled())
                {
                    assoConf->setSecurityLevel(aresp->getAsh().secu.Seculevel);
                    assoConf->setKeyIdMode(aresp->getAsh().secu.KeyIdMode);
                    assoConf->setKeySource(aresp->getAsh().KeyIdentifier.KeySource);
                    assoConf->setKeyIndex(aresp->getAsh().KeyIdentifier.KeyIndex);
                }

                if (aresp->getStatus() == Success)
                {
                    macEV << "Associate Successful \n";
                    mpib.setMacAssociatedPANCoord(true);
                    mpib.setMacPANId(aresp->getSrcPANid());
                    mpib.setMacCoordExtendedAddress(aresp->getSrc());
                    mpib.setMacCoordShortAddress(aresp->getSrc().getShortAddr());
                    associated = true;
                    assoConf->setAddr(aresp->getDest());
                }

                send(assoConf, "outMLME");

                return;
            }
            break;
        }  // case Ieee802154_ASSOCIATION_RESPONSE

        case Ieee802154_ASSOCIATION_REQUEST: {
            ASSERT(rxCmd == NULL);
            rxCmd = cmdFrame;
            if (isCoordinator)
            {
                AssoCmdreq* tmpAssoReq = check_and_cast<AssoCmdreq*>(cmdFrame);
                Association* assoInd = new Association("MLME-Associate.indication");
                assoInd->setAddr(tmpAssoReq->getSrc());
                assoInd->setCapabilityInformation(tmpAssoReq->getCapabilityInformation());

                rxCmd = NULL;
                send(assoInd, "outMLME");
                // we associate with everyone
                genAssoResp(Success, tmpAssoReq);
                return;

            }
            else
            {
                macEV << "Device is dropping Association Request frame since we are NOT a coordinator \n";
                rxCmd = NULL;
                delete (frame);
            }
            break;
        }  // case Ieee802154_ASSOCIATION_REQUEST

        case Ieee802154_DISASSOCIATION_NOTIFICATION: {
            ASSERT(rxCmd == NULL);
            rxCmd = cmdFrame;
            DisAssoCmd* tmpDisCmd = check_and_cast<DisAssoCmd*>(cmdFrame);
            genACK(frame, false);

            DisAssociation* assoInd = new DisAssociation("MLME-DISASSOCIATE.indication");
            assoInd->setDeviceAddress(tmpDisCmd->getSrc());
            assoInd->setDisassociateReason(tmpDisCmd->getDisassociateReason());
            assoInd->setSecurityLevel(tmpDisCmd->getAsh().secu.Seculevel);
            assoInd->setKeyIdMode(tmpDisCmd->getAsh().secu.KeyIdMode);
            assoInd->setKeySource(tmpDisCmd->getAsh().KeyIdentifier.KeySource);
            assoInd->setKeyIndex(tmpDisCmd->getAsh().KeyIdentifier.KeyIndex);
            send(assoInd, "outMLME");
            delete (tmpDisCmd);
            delete (frame);
            break;
        }  // case Ieee802154_DISASSOCIATION_NOTIFICATION

        case Ieee802154_DATA_REQUEST: {
            genACK(frame, false);
            break;
        }  // case Ieee802154_DATA_REQUEST

        case Ieee802154_PANID_CONFLICT_NOTIFICATION:
        case Ieee802154_BEACON_REQUEST:
        case Ieee802154_COORDINATOR_REALIGNMENT: {
            send(rxCmd, "outMLME");
            break;
        }  // case

        case Ieee802154_GTS_REQUEST: {
            if (isCoordinator)
            {
                // ack and indication
                GTSCmd* gtsRequ = check_and_cast<GTSCmd*>(frame);
                genACK(frame, false);
                // add GTS to GTS descriptor
                gts_request_cmd(gtsRequ->getGTSCharacteristics().devShortAddr, gtsRequ->getGTSCharacteristics().length, true);
                // gen indication
                GTSIndication* gtsInd = new GTSIndication("MLME-GTS.indication");
                gtsInd->setGTSCharacteristics(gtsRequ->getGTSCharacteristics());
                gtsInd->setDeviceAddr(gtsRequ->getSrc().getShortAddr());

                if (mpib.getMacSecurityEnabled())
                {
                    gtsInd->setKeyIdMode(gtsRequ->getAsh().secu.KeyIdMode);
                    gtsInd->setKeyIndex(gtsRequ->getAsh().KeyIdentifier.KeyIndex);
                    gtsInd->setKeySource(gtsRequ->getAsh().KeyIdentifier.KeySource);
                    gtsInd->setSecurityLevel(gtsRequ->getAsh().secu.Seculevel);
                }
                send(gtsInd, "outMLME");
            }

            else
            {
                macEV << "Device is dropping received GTS Request Command \n";
                delete frame;
                return;
            }
            break;
        }  // case Ieee802154_GTS_REQUEST

        case Ieee802154_POLL_REQUEST: {
            if (findRxMsg(frame->getSrc()))
            {
                genACK(frame, true);
            }
            else
                genACK(frame, false);

            break;
        }  // case Ieee802154_POLL_REQUEST

        default: {
            macEV << "Got unknown Command type in a Command Frame - dropping frame \n";
            delete cmdFrame;
            delete (frame);
            return;
        }  // default
    }  // switch

    return;
}

void IEEE802154Mac::handle_PLME_SET_TRX_STATE_confirm(phyState status)
{
    macEV << "A PLME_SET_TRX_STATE_confirm with phyState " << phyStateToString(status) << " received from PHY, the requested state is " << phyStateToString(trx_state_req) << endl;
    simtime_t delay;

    if (mlmeReset)
    {
        setDefMpib();
        genMLMEResetConf();
        mlmeReset = false;
    }

    if (mlmeRxEnable)
    {
        RxEnableConfirm* conf = new RxEnableConfirm("MLME-RX-ENABLE.confirmation");
        if (status == phy_RX_ON)
        {
            conf->setStatus(rx_SUCCESS);
        }
        else
        {
            conf->setStatus(rx_PAST_TIME);
        }
        send(conf, "outMLME");
        mlmeRxEnable = false;
    }

    if (scanning && txBcnCmd == NULL)
    {
        if (status == phy_SUCCESS)
        {
            doScan();
        }
        else
        {  //retry
            scanSteps = 0;
            doScan();
        }
        return;
    }

    if (status == phy_SUCCESS)
    {
        status = trx_state_req;
    }

    if (backoffStatus == 99)
    {
        if (trx_state_req == phy_RX_ON)
        {
            if (taskP.taskStatus(TP_RX_ON_CSMACA))
            {
                taskP.taskStatus(TP_RX_ON_CSMACA) = false;
                csmaca_handle_RX_ON_confirm(status);
            }
        }
    }

    if (status != phy_TX_ON)
    {
        return;
    }

    // transmit the packet
    if (beaconWaitingTx)  // periodically TX beacon
    {
        // to synchronize better, we don't transmit the beacon here
    }
    else if (txAck)
    {
        // although no CSMA-CA is required for the transmission of ACK,
        // but we still need to locate the Backoff period boundary if we are beacon-enabled
        // (refer to 802.15.4-2006 Spec. page 189, Sec, 7.5.6.4.2 Acknowledgment)
        if ((mpib.getMacBeaconOrder() == 15) && (rxBO == 15))  // non-beacon enabled
        {
            delay = 0.0;
        }
        else if (rxData->getIsGTS())
        {
            delay = 0.0;
        }
        else  // beacon enabled
        {
            // here we use the hidden destination address that we already set in ACK on purpose
            delay = csmacaLocateBoundary((rxData->getSrc().getShortAddr() == mpib.getMacCoordShortAddress()), 0.0);
            ASSERT(delay < bPeriod);
        }

        if (delay == 0.0)
        {
            handleTxAckBoundTimer();
        }
        else
        {
            startTxAckBoundTimer(delay);
        }
    }
    else if (txGTS)
    {
        // send data frame in GTS here, no delay
        txPkt = txGTS;
        sendDown(check_and_cast<mpdu *>(txGTS->dup()));
    }
    else  // TX Command or data
    {
        if ((mpib.getMacBeaconOrder() == 15) && (rxBO == 15))  // non-beacon enabled
        {
            delay = 0.0;
        }
        else
        {
            delay = csmacaLocateBoundary(toParent(txCsmaca), 0.0);
        }

        if (delay == 0.0)
        {
            handleTxCmdDataBoundTimer();  // transmit immediately
        }
        else
        {
            startTxCmdDataBoundTimer(delay);
        }
    }
}

void IEEE802154Mac::handle_PLME_CCA_confirm(phyState status)
{
    if (taskP.taskStatus(TP_CCA_CSMACA))
    {
        taskP.taskStatus(TP_CCA_CSMACA) = false;

        // the following from CsmaCA802_15_4::CCA_confirm
        if (status == phy_IDLE)  // idle channel
        {
            if (!beaconEnabled)  // non-beacon, unslotted
            {
                tmpCsmaca = NULL;
                csmacaCallBack(phy_IDLE);  // unslotted successfully, callback
                return;
            }
            else  // beacon-enabled, slotted
            {
                CW--;  // Case C1, beacon-enabled, slotted
                if (CW == 0)  // Case D1
                {
                    // timing condition may not still hold -- check again
                    if (csmacaCanProceed(0.0, true))  // Case E1
                    {
                        tmpCsmaca = 0;
                        csmacaCallBack(phy_IDLE);  // slotted CSMA-CA successfully
                        return;
                    }
                    else  // Case E2: postpone until next beacon sent or received
                    {
                        CW = 2;
                        bPeriodsLeft = 0;
                        csmacaWaitNextBeacon = true;  // Debugged
                    }
                }
                else
                {
                    handleBackoffTimer();  // Case D2: perform CCA again, this function sends a CCA_request
                }
            }
        }
        else  // busy channel
        {
            if (beaconEnabled)
            {
                CW = 2;  // Case C2
            }

            NB++;

            if (NB > mpib.getMacMaxCSMABackoffs())  // Case F1
            {
                tmpCsmaca = 0;
                csmacaCallBack(phy_BUSY);
                return;
            }
            else  // Case F2: backoff again
            {
                BE++;
                if (BE > aMaxBE)
                    BE = aMaxBE;  // aMaxBE defined in Ieee802154Const.h
                csmacaStart(false, tmpCsmaca, waitDataAck);  // not the first time
            }
        }
    }
}

void IEEE802154Mac::handle_PD_DATA_confirm(phyState status)
{
    inTransmission = false;

    if (scanning)
    {
        dispatch(status, __FUNCTION__);
        doScan();
        return;
    }

    if (backoffStatus == 1)
    {
        backoffStatus = 0;
    }

    if (status == phy_SUCCESS)
    {
        dispatch(status, __FUNCTION__);
    }
    // sending packet at PHY layer failed
    else if (txPkt == txBeacon)
    {
        beaconWaitingTx = false;
        delete txBeacon;
        txBeacon = NULL;
    }
    else
    {
        error("[MAC]: transmission failed");
    }
}

bool IEEE802154Mac::filter(mpdu* pdu)
{
    unsigned short frmCtrl = pdu->getFcf();
    frameType frmType = (frameType) ((frmCtrl & ftMask) >> 13);
    AddrMode addressMode = (AddrMode) ((frmCtrl & 48) >> 4);

    // First check flag set by PHY layer, COLLISION or RX_DURING_CCA
    if (pdu->getKind() == phy_COLLISION)
    {
        macEV << "Frame corrupted due to collision, dropped \n";
        numCollision++;
        return true;
    }
    else if (pdu->getKind() == phy_RX_DURING_CCA)
    {
        macEV << "Frame corrupted due to being received during CCA, dropped \n";
        return true;
    }
    // check if this Msg is send indirect and we are coordinator
    if (pdu->getIsIndirect())
    {
        if (isCoordinator)
        {
            if (pdu->getIsIndirect())
            {
                if (pdu->getDest().getInt() != myMacAddr.getInt())
                {
                    rxBuff.addAt(rxBuffSlot, pdu);
                    rxBuffSlot++;
                    macEV << "Frame added to Buffer \n";
                    return true;
                }
            }
        }
        else
        {
            return true;
        }
    }

    // perform further filtering only if the PAN is currently not in promiscuous mode
    if (!mpib.getMacPromiscuousMode())
    {
        macEV << "Further Filtering for Packet Type: ";
        // check packet type
        if ((frmType != Beacon) && (frmType != Data) && (frmType != Ack) && (frmType != Command))
        {
            error ("[MAC]: unknown frame type!");
            //return true;
        }
        if (frmType == Beacon)
        {
            // check source PAN ID for beacon frame
            if ((mpib.getMacPANId() != 0xffff)  // associated
            && (pdu->getSrcPANid() != mpib.getMacPANId()))  // PAN ID did not match
            {
                macEV << "frmType == Beacon & PAN ID did not match --> filtered here \n";
                return true;
            }
        }
        else
        {
            // check destination PAN ID (beacon has no destination address fields)
            if ((addressMode == addrShort) || (addressMode == addrLong))
            {
                if ((pdu->getDestPANid() != 0xffff)  // PAN ID does not match for other packets
                && (pdu->getDestPANid() != mpib.getMacPANId()) && (mpib.getMacAssociatedPANCoord()))
                {
                    macEV << "frmType != Beacon & has PAN ID --> filtered here \n";
                    return true;
                }
            }

            // check destination address
            if ((addressMode == addrShort))  // short address
            {
                if (!(pdu->getDest().isBroadcast()) && (pdu->getDest().getShortAddr() != mpib.getMacShortAddress()))
                {
                    macEV << "frmType != Beacon & has Short Addr --> filtered here \n";
                    return true;
                }
            }
            else if ((addressMode == addrLong))  // extended address
            {
                if (!(pdu->getDest().equals(myMacAddr)))
                {
                    macEV << "frmType != Beacon & has Long Addr --> filtered here \n";
                    return true;
                }
            }
        }

        if (frmType == Ack && pdu->getSqnr() != mpib.getMacDSN())
        {
            macEV << "frmType == Ack & Sqnr != MacDSN --> filtered here \n";
            // if dsr address does not match
            return true;
        }

        // check for Data/Command frame only with source address:: destined for PAN coordinator
        // TODO temporary solution, consider only star topology
        if ((frmType == Data) || (frmType == Command))
        {
            if (addressMode == none)  // destination address fields not included
            {
                if (!isCoordinator) {
                    // not a PAN coordinator
                    macEV << "frmType == Data || Command & addressMode == none & no Coordinator --> filtered here \n";
                    return true;
                }
            }
            macEV << "frmType == Data || Command --> not filtered \n";
        }
    }
    return false;
}

void IEEE802154Mac::sendMCPSDataConf(MACenum status, long id)
{
    // send a confirm for upper Layers
    mcpsDataConf* dataConf;
    dataConf = new mcpsDataConf("MCPS-Data.confirm");
    switch (status)
    {
        case mac_SUCCESS: {
            dataConf->setStatus(SUCCESS);
            break;
        }
        case mac_NO_ACK: {
            dataConf->setStatus(NO_ACK);
            break;
        }
        case mac_INVALID_ADDRESS: {
            dataConf->setStatus(INVALID_ADDRESS);
            break;
        }
        case mac_INVALID_GTS: {
            dataConf->setStatus(INVALID_GTS);
            break;
        }
        case mac_FRAME_TOO_LONG: {
            dataConf->setStatus(FRAME_TOO_LONG);
            break;
        }
        case mac_COUNTER_ERROR: {
            dataConf->setStatus(COUNTER_ERROR);
            break;
        }
        case mac_UNSUPPORTED_SECURITY: {
            dataConf->setStatus(UNSUPPORTED_SECURITY);
            break;
        }
        case mac_UNAVAILABLE_KEY: {
            dataConf->setStatus(UNAVAILABLE_KEY);
            break;
        }
        case mac_INVALID_PARAMETER: {
            dataConf->setStatus(INVALID_PARAMETER);
            break;
        }
        default: {
            dataConf->setStatus(INVALID_PARAMETER);
        }
    }

    //dataConf->setMsduHandle(dataConf->getId());     // XXX why not the ID from the caller method ???
    dataConf->setMsduHandle(id);
    dataConf->setTimestamp(simTime());
    send(dataConf, "outMCPS");
    return;
}

void IEEE802154Mac::sendMCPSDataIndication(mpdu* rxData)
{
    mcpsDataInd* dataInd = new mcpsDataInd("MCPS-DATA.indication");
    dataInd->setSrcAddrMode(rxData->getFcf() & 3);
    dataInd->setSrcPANId(rxData->getSrcPANid());
    dataInd->setSrcAddr(rxData->getSrc());
    dataInd->setDstAddrMode(rxData->getFcf() & 48 >> 4);
    dataInd->setDstPANId(rxData->getDestPANid());
    dataInd->setDstAddr(rxData->getDest());
    dataInd->encapsulate(rxData->decapsulate());
    dataInd->setMpduLinkQuality(rxData->getKind());  // extract LQI from msg kind - non-standard compliant solution
    dataInd->setDSN(rxData->getSqnr());

    if (mpib.getMacSecurityEnabled())
    {
        dataInd->setKeySource(rxData->getAsh().KeyIdentifier.KeySource);
        dataInd->setSecurityLevel(rxData->getAsh().secu.Seculevel);
        dataInd->setKeyIdMode(rxData->getAsh().secu.KeyIdMode);
        dataInd->setKeyIndex(rxData->getAsh().KeyIdentifier.KeyIndex);
    }

    dataInd->setMsduLength(dataInd->getByteLength());

    if (mpib.getMacTimestampSupported())
        dataInd->setTimestamp(simTime());

    send(dataInd, "outMCPS");
    return;
}

// for indirect data transfer we need a receive buffer
// Method for finding packet for a Device
mpdu* IEEE802154Mac::findRxMsg(MACAddressExt dest)
{
    mpdu* buffMsg = new mpdu();
    for (int i = rxBuff.size() - rxBuffSlot; i < rxBuff.size(); i++)
    {
        buffMsg = check_and_cast<mpdu*>(rxBuff.get(i));
        if (buffMsg->getDest() == dest)
        {
            rxBuff.remove(buffMsg);
            rxBuffSlot--;
            return buffMsg;     // XXX fix for unreachable code - shifted return below rxBuffSlot--
        }
    }

    return NULL;
}

// for indirect data transfer we need a transmit buffer
mpdu* IEEE802154Mac::getTxMsg()
{
    //mpdu* first = new mpdu();
    //first = check_and_cast<mpdu*>(txBuff.get(txBuff.size() - txBuffSlot));
    //XXX removed additional assignment
    mpdu* first = check_and_cast<mpdu*>(txBuff.get(txBuff.size() - txBuffSlot));
    txBuff.remove(txBuff.size() - txBuffSlot);
    txBuffSlot--;
    return first;
}

void IEEE802154Mac::genSetTrxState(phyState state)
{
    cMessage* setTrx = new cMessage("SET-TRX-STATE");
    setTrx->setKind(state);
    trx_state_req = state;
    macEV << "Sending SET-TRX-STATE.request to PLME with Transceiver_State=" << phyStateToString(state) << endl;
    send(setTrx, "outPLME");
    return;
}

void IEEE802154Mac::genMLMEResetConf()
{
    MLMEReset* conf = new MLMEReset("MLME-Reset.confirm");
    conf->setSetDefaultPIB(true);
    macEV << "Sending MLME-Reset.confirm to MLME \n";
    send(conf, "outMLME");
    return;
}

void IEEE802154Mac::genCCARequest()
{
    cMessage* ccaReq = new cMessage("CCA");
    macEV << "Sending CCARequest to PLME \n";
    send(ccaReq, "outPLME");
    return;
}

void IEEE802154Mac::genACK(mpdu* frame, bool fp)
{
    AckFrame* ack = new AckFrame("Acknowledgment");
    ack->setSqnr(frame->getSqnr());
    ack->setFcf(fcf->genFCF(Ack, mpib.getMacSecurityEnabled(), false, fp, mpib.getMacPANId(), (AddrMode) (frame->getFcf() && 3), 0, (AddrMode) ((frame->getFcf() && 48) >> 4)));
    ack->setFcs(0);
    //rxData = frame;
    ASSERT(!txAck);  // It's impossible to receive the second packet before the ACK has been sent out!
    txAck = ack;
    return;
}

void IEEE802154Mac::genMLMEDisasConf(MACenum status)
{
    DisAssociation* disAssConf = new DisAssociation("MLME-DISASSOCIATE.confirm");
    disAssConf->setStatus(status);
    disAssConf->setDeviceAddrMode(tmpDisAss->getDeviceAddrMode());
    disAssConf->setDevicePANId(tmpDisAss->getDevicePANId());
    disAssConf->setDeviceAddress(tmpDisAss->getDeviceAddress());
    send(disAssConf, "outMLME");

    associated = false;
    return;
}

void IEEE802154Mac::genAssoResp(MlmeAssociationStatus status, AssoCmdreq* tmpAssoReq)
{
    AssoCmdresp* assoResp = new AssoCmdresp("MLME-ASSOCIATE.response");
    // XXX security ignored at the moment
    MACAddressExt devAddr = tmpAssoReq->getCapabilityInformation().addr;
    devAddr.genShortAddr();
    macEV << "Generating ASSOCIATE response for: " << devAddr << " with new short address: " << devAddr.getShortAddr() << endl;
    assoResp->setDest(devAddr);
    assoResp->setShortAddress(devAddr.getShortAddr());
    assoResp->setSrc(myMacAddr);
    assoResp->setCmdType(Ieee802154_ASSOCIATION_RESPONSE);
    assoResp->setStatus(status);
    assoResp->setSrcPANid(mpib.getMacPANId());

    mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
    holdMe->encapsulate(assoResp);
    holdMe->setDest(assoResp->getDest());
    holdMe->setSrc(myMacAddr);
    holdMe->setSrcPANid(mpib.getMacPANId());
    holdMe->setFcf(fcf->genFCF(Command, false, false, false, false, addrLong, 0, addrShort));

    if (txData != NULL)
    {
        macEV << "Right now processing other Data Associate response will be transmitted later \n";
        txBuff.addAt(txBuffSlot, holdMe);
        txBuffSlot++;
    }
    else
    {
        Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
        taskP.taskStatus(task) = true;

        switch (dataTransMode)
        {
            case DIRECT_TRANS:
            case INDIRECT_TRANS: {
                taskP.taskStep(task)++;strcpy
                (taskP.taskFrFunc(task), "handle_PD_DATA_request");
                ASSERT(txData == NULL);
                txData = holdMe;
                csmacaEntry('d');
                break;
            }  // CASES DIRECT_TRANS & INDIRECT_TRANS

            case GTS_TRANS: {
                taskP.taskStep(task)++;
                // waiting for GTS arriving, callback from handleGtsTimer()
                strcpy
                (taskP.taskFrFunc(task), "handleGtsTimer");
                ASSERT(txData == NULL);
                txData = holdMe;
                numGTSRetry = 0;

                // if I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                // if I'm the device, should try to transmit if now is in my GTS
                // refer to Spec. 7.5.7.3
                if (index_gtsTimer == 99)
                {
                    ASSERT(gtsTimer->isScheduled());
                    // first check if the requested transaction can be completed before the end of current GTS
                    if (gtsCanProceed())
                    {
                        // directly hand over to FSM, which will go to next step, state parameters are ignored
                        FSM_MCPS_DATA_request();
                    }
                }
                break;
            }  // case GTS_TRANS
        }  // switch
    }

    return;
}

void IEEE802154Mac::genDisAssoCmd(DisAssociation* disAss, bool direct)
{
    DisAssoCmd* disCmd = new DisAssoCmd("Dissociation notification command");
    if (disAss->getDeviceAddrMode() == addrShort)
    {
        disCmd->setFcf(fcf->genFCF(Command, mpib.getMacSecurityEnabled(), false, true, false, addrShort, 0, addrShort));

        int sqnr = mpib.getMacDSN();
        disCmd->setSqnr(mpib.getMacDSN());
        sqnr++;
        mpib.setMacDSN(sqnr);
        disCmd->setDestPANid(disAss->getDevicePANId());
        disCmd->setDest(disAss->getDeviceAddress());
        disCmd->setSrcPANid(mpib.getMacPANId());
        disCmd->setSrc(myMacAddr);

        if (mpib.getMacSecurityEnabled())
        {
            Ash ash;
            ash.secu.KeyIdMode = disAss->getKeyIdMode();
            ash.secu.Seculevel = disAss->getSecurityLevel();
            ash.FrameCount = 1;
            ash.KeyIdentifier.KeyIndex = disAss->getKeyIndex();
            ash.KeyIdentifier.KeySource = disAss->getKeySource();
            disCmd->setAsh(ash);
        }

        disCmd->setCmdType(Ieee802154_DISASSOCIATION_NOTIFICATION);
        disCmd->setDisassociateReason(disAss->getDisassociateReason());
        mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
        holdMe->encapsulate(disCmd);
        holdMe->setDest(disCmd->getDest());
        holdMe->setSrc(disCmd->getSrc());
        holdMe->setSrcPANid(disCmd->getSrcPANid());
        holdMe->setFcf(disCmd->getFcf());

        Ieee802154MacTaskType task = TP_MLME_DISASSOCIATE_REQUEST;
        taskP.taskStatus(task) = true;
        taskP.mcps_data_request_TxOption = dataTransMode;

        switch (dataTransMode)
        {
            case DIRECT_TRANS:
            case INDIRECT_TRANS: {
                taskP.taskStep(task)++;strcpy
                (taskP.taskFrFunc(task), "csmacaDisAssociation");
                ASSERT(txData == NULL);
                txData = holdMe;
                csmacaEntry('d');
                break;
            }  // cases DIRECT_TRANS & INDIRECT_TRANS

            case GTS_TRANS: {
                taskP.taskStep(task)++;
                // waiting for GTS arriving, callback from handleGtsTimer()
                strcpy
                (taskP.taskFrFunc(task), "handleGtsTimer");
                ASSERT(txData == NULL);
                txGTS = holdMe;
                numGTSRetry = 0;

                // If I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                // If I'm the device, should try to transmit if now is in my GTS
                // refer to 802.15.4-2006 Spec. Section 7.5.7.3
                if (index_gtsTimer == 99)
                {
                    ASSERT(gtsTimer->isScheduled());
                    // first check if the requested transaction can be completed before the end of current GTS
                    if (gtsCanProceed())
                    {
                        // directly hand over to FSM, which will go to next step, state parameters are ignored
                        FSM_MCPS_DATA_request();
                    }
                }
                break;
            }  // case GTS_TRANS

            default: {
                error("[IEEE802154MAC]: undefined txOption: %d!", dataTransMode);
                return;
            }  // default
        }  // switch

    }
    return;
}

// TODO FCS calculation
unsigned short IEEE802154Mac::calcFCS(mpdu* pdu, cMessage *payload, bool calcFlag, int headerSize)
{
    // First take the header then traffic ...

    cPacket *traffic = dynamic_cast<cPacket *>(payload);
    macEV << payload->getControlInfo() << endl;
    int end = 0;

    int x = pdu->getByteLength() - 1;  // Byte length of Payload + Header
    //  int y = traffic->getByteLength() - 1;
    int polynomial = 0x1021;
    unsigned char *ptr;
    unsigned short crc = 0;

    macEV << "PDU ByteLength = " << pdu->getByteLength() << endl;

    ptr = (unsigned char *) pdu;

    ptr = ptr + x;

    // If we calculate the FCS we don't want the last 2 Bytes of header ...
    end = x - (headerSize);

    // if we check it though we want
    if (calcFlag)
        end = x - (headerSize + 1);

    while (x > end)
    {
        macEV << "\n Content of ptr=" << (unsigned short) *ptr;
        for (int i = 0; i < 8; i++)
        {
            bool bit = ((*ptr >> (7 - i) & 1) == 1);
            bool c15 = ((crc >> 15 & 1) == 1);
            crc <<= 1;
            if (c15 ^ bit)
                crc ^= polynomial;
        }
        ptr--;
        x--;
    }

    macEV << "\n\n Content of Traffic ------";
    x = traffic->getByteLength();
    ptr = (unsigned char *) traffic;
    ptr += x;
    while (x > 0)
    {
        macEV << "\n Content of ptr=" << (unsigned short) *ptr;
        ptr--;
        x--;
    }
    crc &= 0xffff;
    macEV << "\n Calculated CRC=" << crc << endl;

    return crc;
}

void IEEE802154Mac::genBeaconInd(mpdu* frame)
{
    beaconFrame* bFrame = check_and_cast<beaconFrame*>(frame);
    beaconNotify* bNotify = new beaconNotify("MLME-BEACON-NOTIFY.indication");

    bNotify->setBSN(mpib.getMacBSN());
    bNotify->setPANDescriptor(rxPanDescriptor);
    bNotify->setPendAddrSpec(bFrame->getPaFields());

    if (bFrame->getEncapsulatedPacket() != NULL)
    {
        bNotify->setSduLength(bFrame->getEncapsulatedPacket()->getByteLength());
        bNotify->encapsulate(bFrame->decapsulate());
    }
    send(bNotify, "outMLME");
}

void IEEE802154Mac::genGTSConf(GTSDescriptor gts, MACenum status)
{
    GTSConfirm* gConf = new GTSConfirm("MLME-GTS.confirm");
    gConf->setGts(gts);
    gConf->setStatus(status);
    send(gConf, "outMLME");
    waitGTSConf = false;
    return;
}

void IEEE802154Mac::genScanConf(ScanStatus status)
{
    ScanConfirm* scanConf = new ScanConfirm("MLME-SCAN.confirm");
    scanConf->setStatus(status);
    scanConf->setScanType(scanType);
    scanConf->setChannelPage(channelPage);
    scanConf->setUnscannedChannels(~scanChannels);
    scanConf->setResultListSize(scanResultListSize);

    if (scanType == scan_ED)
    {
        scanConf->setEnergyDetectListArraySize(scanResultListSize);
        for (int i = 0; i < scanResultListSize; i++)
        {
            scanConf->setEnergyDetectList(i, scanEnergyDetectList[i]);
        }
    }

    if (scanType == scan_ACTIVE || scanType == scan_PASSIVE)
    {
        scanConf->setPANDescriptorListArraySize(scanResultListSize);
        for (int i = 0; i < scanResultListSize; i++)
        {
            scanConf->setPANDescriptorList(i, scanPANDescriptorList[i]);
        }
    }

    send(scanConf, "outMLME");
    scanCount = 0;
    scanning = false;
}

void IEEE802154Mac::genPollConf(MACenum status)
{
    PollConfirm* pollConf = new PollConfirm("MLME-POLL.confirmation");
    pollConf->setStatus(status);
    send(pollConf, "outMLME");
}

void IEEE802154Mac::genStartConf(MACenum status)
{
    StartConfirm* startConf = new StartConfirm("MLME-START.confirmation");
    startConf->setStatus(status);
    send(startConf, "outMLME");
}

void IEEE802154Mac::genOrphanInd()
{
    // we ignore Notification and send Indication instead
    Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
    OrphanIndication* orphInd = new OrphanIndication("MLME-ORPHAN.indication");
    orphInd->setOrphanAddress(myMacAddr);
    mpdu* holdMe = new mpdu("MLME-COMMAND.inside");
    holdMe->encapsulate(orphInd);
    holdMe->setDest(mpib.getMacCoordExtendedAddress());
    holdMe->setSrc(myMacAddr);
    holdMe->setSrcPANid(mpib.getMacPANId());
    holdMe->setFcf(fcf->genFCF(Command, false, false, false, false, addrLong, 0, addrLong));
    holdMe->setIsGTS(false);
    taskP.taskStep(task)++;
    strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
    ASSERT(txData == NULL);
    txData = holdMe;
    csmacaEntry('d');
}

void IEEE802154Mac::reqMsgBuffer()
{
    cMessage* bmsg = new cMessage("getElem");
    bmsg->setKind(99);
    send(bmsg, "outMCPS");
}

void IEEE802154Mac::sendDown(mpdu* frame)
{
    inTransmission = true;  // cleared by PD_DATA_confirm
    send(frame, "outPD");  // send a duplication
    macEV << "Sending frame " << frame->getName() << " (" << frame->getByteLength() << " Bytes) to PHY layer \n";
    macEV << "The estimated transmission time is " << calDuration(frame) << " sec \n";
}

void IEEE802154Mac::doScan()
{
    switch (scanType)
    {  // Step 1 set TRX state 2 set channel 3
        case scan_ED: {
            if (scanStep == 0)
            {
                macEV << "ED channel scan \n";
                genSetTrxState(phy_RX_ON);
                scanStep++;
                return;
            }
            else if (scanStep == 1)
            {
                while (!((scanSteps >> scanCount) & 1))
                {  // look up next channel to Scan
                    scanCount++;
                }

                if (scanCount > 26)
                {
                    macEV << "ED channel scan done - sending results to higher layer \n";
                    genScanConf(scan_SUCCESS);
                    scanStep = 0;
                    scanning = false;
                    reqMsgBuffer();

                    return;
                }

                // send a set Msg ... GetConfirm primitive also used to set PIB
                GetConfirm* setMsg = new GetConfirm("SET");
                setMsg->setPIBattr(currentChannel);
                //setMsg->setPIBind(); // not needed
                setMsg->setValue(scanCount);
                send(setMsg, "outPLME");
                scanStep++;
                scanCount++;
                return;
            }
            else if (scanStep == 2)
            {  // send ED scan and set timer ...

                cMessage* edMsg = new cMessage("ED");
                send(edMsg, "outPLME");
                simtime_t wtime = (aBaseSuperframeDuration * (pow(2, scanDuration) + 1) / phy_symbolrate);
                startScanTimer(wtime);
                scanStep = 1;
                return;
            }
            break;
        }  // case scan_ED:

        case scan_ACTIVE: {
            macEV << "Active channel scan \n";
            if (scanStep == 0)
            {
                // set channel
                while (!((scanSteps >> scanCount) & 1))
                {  // look up next channel to Scan
                    scanCount++;
                }
                if (scanCount > 26)
                {
                    macEV << "Active channel scan done sending results to upper layer \n";
                    genScanConf(scan_SUCCESS);
                    scanStep = 0;
                    scanning = false;
                    reqMsgBuffer();
                    return;
                }
                // send a SET Msg to change to the channel for scanning ... GetConfirm primitive also used to set PIB
                GetConfirm* setMsg = new GetConfirm("SET");
                setMsg->setPIBattr(currentChannel);
                //setMsg->setPIBind(); // not needed
                setMsg->setValue(scanCount + 1);
                send(setMsg, "outPLME");
                scanCount++;
                scanStep++;
                return;
            }
            else if (scanStep == 1)
            {
                genSetTrxState(phy_TX_ON);
                scanStep++;
                return;
            }
            else if (scanStep == 2)
            {
                // send beacon request
                CmdFrame* beaconReq = new CmdFrame("MLME-Beacon.request");
                beaconReq->setCmdType(Ieee802154_BEACON_REQUEST);
                beaconReq->setSrc(myMacAddr);
                beaconReq->setDest(MACAddressExt::BROADCAST_ADDRESS);
                beaconReq->setDestPANid(0xffff);
                beaconReq->setFcf(0);  // ignored upon reception

                // send this direct to the coordinator
                txBcnCmd = beaconReq;
                // Beacon is send direct as a usual data packet
                Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                taskP.taskStatus(task) = true;
                taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                csmacaEntry('c');
                scanStep++;
                return;
            }
            else if (scanStep == 3)
            {
                // turn rx on
                genSetTrxState(phy_RX_ON);
                scanStep++;
                return;
            }
            else if (scanStep == 4)
            {
                // start timer
                simtime_t wtime = (aBaseSuperframeDuration * (pow(2, scanDuration) + 1) / phy_symbolrate);
                startScanTimer(wtime);
                scanStep = 0;
                return;
            }

            break;
        }  // case scan_ACTIVE

        case scan_PASSIVE: {
            macEV << "Passive channel scan \n";
            if (scanStep == 0)
            {
                while (!((scanSteps >> scanCount) & 1))
                {
                    // look up next channel to Scan
                    scanCount++;
                }
                if (scanCount > 26)
                {
                    macEV << "Passive channel scan done - sending results to higher layer \n";
                    genScanConf(scan_SUCCESS);
                    scanStep = 0;
                    scanning = false;
                    reqMsgBuffer();
                    return;
                }
                // send a set Msg ... GetConfirm primitive also used to set PIB
                GetConfirm* setMsg = new GetConfirm("SET");
                setMsg->setPIBattr(currentChannel);
                //setMsg->setPIBind(); // not needed
                setMsg->setValue(scanCount);
                send(setMsg, "outPLME");
                scanStep++;
                scanCount++;
                return;
            }
            else if (scanStep == 1)
            {
                genSetTrxState(phy_RX_ON);
                scanStep++;
                return;
            }
            else if (scanStep == 2)
            {
                // start timer
                simtime_t wtime = (aBaseSuperframeDuration * (pow(2, scanDuration) + 1) / phy_symbolrate);
                startScanTimer(wtime);
                scanStep = 0;
                return;
            }
            break;
        }  // case scan_PASSIVE

        case scan_ORPHAN: {
            macEV << "Orphan channel scan for channel# " << scanCount << endl;
            if (scanStep == 0)
            {
                // set Channel
                while (!((scanSteps >> scanCount) & 1))
                {
                    // look up next channel to Scan
                    scanCount++;
                }

                if (scanCount > 26)
                {
                    macEV << "Passive channel scan done sending results to upper layer \n";
                    genScanConf(scan_SUCCESS);
                    scanStep = 0;
                    scanning = false;
                    reqMsgBuffer();
                    return;
                }

                // send a set Msg ... GetConfirm primitive also used to set PIB
                GetConfirm* setMsg = new GetConfirm("SET");
                setMsg->setPIBattr(currentChannel);
                //setMsg->setPIBind(); // not needed
                setMsg->setValue(scanCount);
                send(setMsg, "outPLME");
                scanStep++;
                scanCount++;
                return;
            }
            else if (scanStep == 1)
            {
                // set TRX state
                genSetTrxState(phy_TX_ON);
                scanStep++;
                return;
            }
            else if (scanStep == 2)
            {
                // send orphan indication
                // TODO guess its better to do usual CSMA-CA??
                genOrphanInd();
                scanStep++;
                return;
            }
            else if (scanStep == 3)
            {
                //set TRX state
                genSetTrxState(phy_RX_ON);
                scanStep = 0;
                return;
            }
            break;
        }  // case scan_ORPHAN

        default: {
            error("[IEEE802154MAC]: unknown scanType received - stopping scan!");
            scanning = false;
            break;
        }  // default
    }  // switch
}

double IEEE802154Mac::getRate(char bitOrSymbol)
{
    double rate;
    int temp = ppib.getCurrChann();

    if (temp == 0)
    {
        if (bitOrSymbol == 'b')
            rate = BR_868M;
        else
            rate = SR_868M;
    }
    else if (temp <= 10)
    {
        if (bitOrSymbol == 'b')
            rate = BR_915M;
        else
            rate = SR_915M;
    }
    else
    {
        if (bitOrSymbol == 'b')
            rate = BR_2_4G;
        else
            rate = SR_2_4G;
    }
    return (rate * 1000);
}

//-------------------------------------------------------------------------------/
//############################# <CSMA/CA Functions> #############################/
//+++++++++++ taken and adjusted from Feng Chen's IEEE 802.15.4 Model +++++++++++/
//+++++++ https://www7.informatik.uni-erlangen.de/~fengchen/omnet/802154/ +++++++/
//-------------------------------------------------------------------------------/
void IEEE802154Mac::csmacaEntry(char pktType)
{
    if (pktType == 'c')  // txBcnCmd
    {
        waitBcnCmdAck = false;  // beacon packet not yet transmitted
        numBcnCmdRetry = 0;
        if (backoffStatus == 99)  // backoff for data packet
        {
            backoffStatus = 0;
            csmacaCancel();
        }
        csmacaResume();
    }
    else if (pktType == 'u')  // txBcnCmdUpper
    {
        waitBcnCmdUpperAck = false;  // command packet not yet transmitted
        numBcnCmdUpperRetry = 0;
        if ((backoffStatus == 99) && (txCsmaca != txBcnCmd))  // backoff for data packet
        {
            backoffStatus = 0;
            csmacaCancel();
        }
        csmacaResume();

    }
    else  if (pktType == 'd')  // txData
    {
        waitDataAck = false;  // data packet not yet transmitted
        numDataRetry = 0;
        csmacaResume();
    }
    else
    {
        error("MAC.csmaEntry: unknown packet type!");
    }
}

void IEEE802154Mac::csmacaResume()
{
    if ((backoffStatus != 99)  // not during backoff
    && (!inTransmission))
    {
        bool ackReq;    // XXX fix to reduce scope of variable

        // not during transmission
        if ((txBcnCmd) && (!waitBcnCmdAck))
        {
            backoffStatus = 99;
            strcpy(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), "csmacaCallBack");  // the transmission may be interrupted and need to backoff again
            taskP.taskStep(TP_MCPS_DATA_REQUEST) = 1;  // also set the step
            // look up at bit 10 if it is set
            ackReq = ((txBcnCmd->getFcf() & 1024));
            txCsmaca = txBcnCmd;
            csmacaStart(true, txBcnCmd, ackReq);
            return;
        }
        else if ((txBcnCmdUpper) && (!waitBcnCmdUpperAck))
        {
            strcpy(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), "csmacaCallBack");  // the transmission may be interrupted and need to backoff again
            taskP.taskStep(TP_MCPS_DATA_REQUEST) = 1;  // also set the step
            backoffStatus = 99;
            ackReq = (txBcnCmdUpper->getFcf() & 1024);
            txCsmaca = txBcnCmdUpper;
            csmacaStart(true, txBcnCmdUpper, ackReq);
            return;
        }
        else if ((txData) && (!waitDataAck))
        {
            strcpy(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), "csmacaCallBack");  // the transmission may be interrupted and need to backoff again
            taskP.taskStep(TP_MCPS_DATA_REQUEST) = 1;  // also set the step

            backoffStatus = 99;
            ackReq = (txData->getFcf() & 1024);
            txCsmaca = txData;
            csmacaStart(true, txData, ackReq);
            return;
        }
    }
}

void IEEE802154Mac::csmacaStart(bool firsttime, mpdu* frame, bool ackReq)
{
    bool backoff;
    simtime_t wtime, rxBI;

    if (txAck)
    {
        backoffStatus = 0;
        tmpCsmaca = 0;
        return;
    }

    ASSERT(!backoffTimer->isScheduled());

    if (firsttime)
    {
        macEV << "[CSMA]: Starting CSMA-CA for " << frame->getName() << ":#" << (unsigned int) frame->getSqnr() << endl;
        beaconEnabled = ((mpib.getMacBeaconOrder() != 15) || (rxBO != 15));
        csmacaReset(beaconEnabled);
        ASSERT(tmpCsmaca == 0);
        tmpCsmaca = frame;
        csmacaAckReq = ackReq;
    }

    if (rxBO != 15)  // is receiving beacon
    {
        // set schedBcnRxTime: the latest time that the beacon should have arrived
        // schedBcnRxTime may not be bcnRxTime, when missing some beacons
        // or being in the middle of receiving a beacon
        schedBcnRxTime = bcnRxTime;
        rxBI = rxSfSpec.BI / phy_symbolrate;
        while (schedBcnRxTime + rxBI < simTime())
        {
            schedBcnRxTime += rxBI;
        }
    }

    wtime = intrand(1 << BE) * bPeriod;  // choose a number in [0, pow(2, BE)-1]
    macEV << "[CSMA]: Choosing random number of backoff periods: " << (wtime / bPeriod) << endl;
    macEV << "[CSMA]: Backoff time before adjusting: " << wtime * 1000 << " ms \n";
    wtime = csmacaAdjustTime(wtime);  // if scheduled backoff ends before CAP, 'wtime' should be adjusted
    macEV << "[CSMA]: Backoff time after adjusting: " << wtime * 1000 << " ms \n";
    backoff = true;

    if (beaconEnabled)
    {
        if (firsttime)
        {
            wtime = csmacaLocateBoundary(toParent(tmpCsmaca), wtime);
            macEV << "[CSMA]: Backoff time after locating boundary: " << wtime * 1000 << " ms \n";
        }

        if (!csmacaCanProceed(wtime, false))  // check if backoff can be applied now. If not, canProceed() will decide how to proceed.
        {
            backoff = false;
        }
    }
    if (backoff)
    {
        startBackoffTimer(wtime);
    }
}

void IEEE802154Mac::csmacaCancel()
{
    if (backoffTimer->isScheduled())
    {
        cancelEvent(backoffTimer);
    }
    else if (deferCCATimer->isScheduled())
    {
        cancelEvent(deferCCATimer);
    }
    else
    {
        taskP.taskStatus(TP_CCA_CSMACA) = false;
    }

    tmpCsmaca = 0;
}

void IEEE802154Mac::csmacaCallBack(phyState status)
{
    if (((!txBcnCmd) || (waitBcnCmdAck)) && ((!txBcnCmdUpper) || (waitBcnCmdUpperAck)) && ((!txData) || (waitDataAck)))
    {
        return;
    }

    backoffStatus = (status == phy_IDLE) ? 1 : 2;

    dispatch(status, __FUNCTION__);
}

void IEEE802154Mac::csmacaReset(bool bcnEnabled)
{
    if (bcnEnabled)
    {
        NB = 0;
        CW = 2;
        BE = mpib.getMacMinBE();
        if ((mpib.getMacBattLifeExt()) && (BE > 2))
        {
            BE = 2;
        }
    }
    else
    {
        NB = 0;
        BE = mpib.getMacMinBE();
    }
}

simtime_t IEEE802154Mac::csmacaAdjustTime(simtime_t wtime)
{
    // find the beginning point of CAP and adjust the scheduled time if it comes before CAP
    simtime_t neg;
    simtime_t tmpf;

    ASSERT(tmpCsmaca);
    if (!toParent(tmpCsmaca))  // as a coordinator
    {
        if (mpib.getMacBeaconOrder() != 15)
        {
            /* Linux floating number compatibility
             neg = (CURRENT_TIME + wtime - bcnTxTime) - mac->beaconPeriods * bPeriod;
             */
            {
                tmpf = txBcnDuration * bPeriod;
                tmpf = simTime() - tmpf;
                tmpf += wtime;
                neg = tmpf - mpib.getMacBeaconTxTime();
            }

            if (neg < 0.0)
            {
                wtime -= neg;
            }
            return wtime;
        }
        else
        {
            return wtime;
        }
    }
    else  // as a device
    {
        if (rxBO != 15)
        {
            /* Linux floating number compatibility
             neg = (CURRENT_TIME + wtime - bcnRxTime) - mac->beaconPeriods2 * bPeriod;
             */
            {
                tmpf = rxBcnDuration * bPeriod;
                tmpf = simTime() - tmpf;
                tmpf += wtime;
                neg = tmpf - schedBcnRxTime;
            }

            if (neg < 0.0)
            {
                wtime -= neg;
            }
            return wtime;
        }
        else
        {
            return wtime;
        }
    }
}

simtime_t IEEE802154Mac::csmacaLocateBoundary(bool toParent, simtime_t wtime)
{
    int align;
    simtime_t bcnTxRxTime, newtime, rxBI;

    if ((mpib.getMacBeaconOrder() == 15) && (rxBO == 15))
    {
        return wtime;
    }

    if (toParent)
    {
        align = (rxBO == 15) ? 1 : 2;
    }
    else
    {
        align = (mpib.getMacBeaconOrder() == 15) ? 2 : 1;
    }

    // we need to do this here, because this function may be called outside the CSMA-CA algorithm,
    // e.g. in handle_PLME_SET_TRX_STATE_confirm while preparing to send an ACK
    if (rxBO != 15)  // is receiving beacon
    {
        // set schedBcnRxTime: the latest time that the beacon should have arrived
        // schedBcnRxTime may not be bcnRxTime, when missing some beacons
        // or being in the middle of receiving a beacon
        schedBcnRxTime = bcnRxTime;
        rxBI = rxSfSpec.BI / phy_symbolrate;
        while (schedBcnRxTime + rxBI < simTime())
            schedBcnRxTime += rxBI;
    }

    bcnTxRxTime = (align == 1) ? mpib.getMacBeaconTxTime() : schedBcnRxTime;

    newtime = fmod(((simTime() + wtime) - bcnTxRxTime), bPeriod);

    if (newtime > 0.0)
    {
        macEV << "[CSMA]: Deviation to backoff boundary: " << (bPeriod - newtime) << " sec \n";
        newtime = wtime + (bPeriod - newtime);
    }
    else
    {
        newtime = wtime;
    }

    return newtime;
}

bool IEEE802154Mac::csmacaCanProceed(simtime_t wtime, bool afterCCA)
{
    bool ok;
    unsigned short t_bPeriods;
    unsigned short t_CAP;
    simtime_t tmpf, rxBI, t_fCAP, t_CCATime, t_IFS, t_transacTime;
    simtime_t now = simTime();
    csmacaWaitNextBeacon = false;
    t_transacTime = 0;
    wtime = csmacaLocateBoundary(toParent(tmpCsmaca), wtime);

    // check if CSMA-CA can proceed within the current superframe
    // in the case the node acts as both a coordinator and a device, both the superframes from and to this node should be taken into account)
    // Check 1: if now is in CAP
    // Check 2: if the number of backoff periods greater than the remaining number of backoff periods in the CAP
    // check 3: if the entire transmission can be finished before the end of current CAP

    macEV << "[CSMA]: Starting to evaluate whether CSMA-CA can proceed ... \n";
    if (!toParent(tmpCsmaca))  // as a coordinator
    {
        if (mpib.getMacBeaconOrder() != 15)  // beacon enabled as a coordinator
        {
            t_fCAP = mpib.getMacBeaconTxTime() + (txSfSpec.finalCap + 1) * txSfSlotDuration / phy_symbolrate;
            if (now >= t_fCAP)  // Check 1
            {
                csmacaWaitNextBeacon = true;
                bPeriodsLeft = 0;  // re-choose random backoff
                macEV << "[CSMA]: Cannot proceed because it's now NOT in CAP (outgoing), resume at the beginning of next CAP \n";
                return false;
            }
            else  // Check 2
            {
                // calculate number of Backoff periods in CAP except for txBcnDuration
                t_CAP = (txSfSpec.finalCap + 1) * txSfSlotDuration / aUnitBackoffPeriod - txBcnDuration;
                macEV << "[CSMA]: Total number of backoff periods in CAP (except for txBcnDuration): " << t_CAP << endl;

                // calculate number of Backoff periods from the first backoff in CAP to now+wtime
                tmpf = now + wtime;
                macEV << "[CSMA]: Now + wtime = " << tmpf << endl;
                tmpf -= mpib.getMacBeaconTxTime();
                macEV << "[CSMA]: wtime = " << wtime << "; mpib.getMacBeaconTxTime = " << mpib.getMacBeaconTxTime() << endl;
                tmpf = tmpf / bPeriod;
                macEV << "[CSMA]: tmpf = " << tmpf << "; bPeriod  = " << bPeriod << endl;
                t_bPeriods = (tmpf - txBcnDuration).dbl();
                macEV << "[CSMA]: t_bPeriods = " << t_bPeriods << "; txBcnDuration = " << (int) txBcnDuration << endl;

                tmpf = now + wtime;
                tmpf -= mpib.getMacBeaconTxTime();
                if (fmod(tmpf, bPeriod) > 0.0)
                {
                    t_bPeriods++;
                }

                // calculate the difference
                bPeriodsLeft = t_bPeriods - t_CAP;  // backoff periods left for next superframe
            }
        }
        else
        {
            // non-beacon as a coordinator, but beacon enabled as a device
            bPeriodsLeft = -1;
        }
    }
    else  // as a device
    {
        if (rxBO != 15)  // beacon enabled as a device
        {
            // we assume that no beacon will be lost
            /* If max beacon loss is reached, use un-slotted version
             rxBI = rxSfSpec.BI / phy_symbolrate;
             tmpf = (rxSfSpec.finalCap + 1) * (aBaseSlotDuration * (1 << rxSfSpec.SO))/phy_symbolrate;
             tmpf += bcnRxTime;
             tmpf += aMaxLostBeacons * rxBI;

             if (tmpf < now)
             bPeriodsLeft = -1;
             else
             {*/

            t_fCAP = schedBcnRxTime + (rxSfSpec.finalCap + 1) * rxSfSlotDuration / phy_symbolrate;
            if (now >= t_fCAP)  // Check 1
            {
                csmacaWaitNextBeacon = true;
                bPeriodsLeft = 0;  // re-choose random backoff
                macEV << "[CSMA]: Cannot proceed because it's now NOT in CAP (incoming), resume at the beginning of next CAP \n";
                return false;
            }
            else
            {
                t_CAP = (rxSfSpec.finalCap + 1) * rxSfSlotDuration / aUnitBackoffPeriod - rxBcnDuration;
                macEV << "[CSMA]: Total number of backoff periods in CAP (except for rxBcnDuration): " << t_CAP << endl;
                // calculate num of Backoff periods from the first backoff in CAP to now+wtime
                tmpf = now + wtime;
                tmpf -= schedBcnRxTime;
                tmpf = tmpf / bPeriod;
                macEV << "[CSMA]: tmpf = " << tmpf << "; bPeriod  = " << bPeriod << endl;

                // TODO include conversion of symbols (rxBcnDuration) to simtime_t
                t_bPeriods = (tmpf - rxBcnDuration).dbl();
                macEV << "[CSMA]: t_bPeriods = " << t_bPeriods << "; rxBcnDuration = " << (int) rxBcnDuration << endl;

                tmpf = now + wtime;
                tmpf -= schedBcnRxTime;
                if (fmod(tmpf, bPeriod) > 0.0)
                {
                    t_bPeriods++;
                }

                bPeriodsLeft = t_bPeriods - t_CAP;  // backoff periods left for next superframe
            }
        }
        else
        {
            // non-beacon as a device, but beacon enabled as a coordinator
            bPeriodsLeft = -1;
        }
    }

    macEV << "[CSMA]: bPeriodsLeft = " << bPeriodsLeft << endl;
    ok = true;
    if (bPeriodsLeft > 0)
    {
        ok = false;
    }
    else if (bPeriodsLeft == 0)
    {
        if ((!toParent(tmpCsmaca)) && (!txSfSpec.battLifeExt))
        {
            ok = false;
        }
        else if ((toParent(tmpCsmaca)) && (!rxSfSpec.battLifeExt))
        {
            ok = false;
        }
    }
    if (!ok)
    {
        EV << "[CSMA-CA]:cannot proceed because the chosen number of backoffs cannot finish before the end of current CAP, resume at beginning of next CAP \n";
        // if as a device, need to track next beacon if it is not being tracked
        if (rxBO != 15)
        {
            if (!bcnRxTimer->isScheduled())
            {
                startBcnRxTimer();
            }
        }
        csmacaWaitNextBeacon = true;
        return false;
    }

    // check 3
    // If backoff can finish before the end of CAP or sent in non-beacon,
    // calculate the time needed to finish the transaction and evaluate it
    t_CCATime = 8.0 / phy_symbolrate;
    if (calFrmByteLength(tmpCsmaca) <= aMaxSIFSFrameSize)
    {
        t_IFS = aMinSIFSPeriod;
    }
    else
    {
        t_IFS = aMinLIFSPeriod;
    }

    t_IFS = t_IFS / phy_symbolrate;

    if (!afterCCA)
    {
        t_transacTime = t_CCATime;  // first CCA time
        t_transacTime += csmacaLocateBoundary(toParent(tmpCsmaca), t_transacTime) - (t_transacTime);  // locate boundary for second CCA
        t_transacTime += t_CCATime;  // second CCA time
    }
    t_transacTime += csmacaLocateBoundary(toParent(tmpCsmaca), t_transacTime) - (t_transacTime);  // locate boundary for transmission
    t_transacTime += calDuration(tmpCsmaca);  // calculate packet transmission time

    if (csmacaAckReq)  // if ACK required
    {
        t_transacTime += mpib.getMacAckWaitDuration() / phy_symbolrate;  // ACK waiting time (this value does not include round trip propagation delay)
        //t_transacTime += 2*max_pDelay;    // FIXME round trip propagation delay (802.15.4 ignores this, but it should be there even though it is very small)
        t_transacTime += t_IFS;  // IFS time -- not only ensure that the sender can finish the transaction, but also the receiver
    }
    else  // no ACK required
    {
        t_transacTime += aTurnaroundTime / phy_symbolrate;  // transceiver turn-around time (receiver may need to do this to transmit next beacon)
        //t_transacTime += max_pDelay;      // FIXME one-way trip propagation delay (802.15.4 ignores this, but it should be there even though it is very small)
        t_transacTime += t_IFS;  // IFS time -- ensure that both Sender and(!) Receiver can finish the transaction
    }

    tmpf = now + wtime;
    tmpf += t_transacTime;
    macEV << "[CSMA]: The entire transmission is estimated to finish at " << tmpf << " sec \n";
    // calculate the time for the end of CAP
    if (!toParent(tmpCsmaca))  // sent as a coordinator
    {
        if (mpib.getMacBeaconOrder() != 15)  // sent in beacon-enabled, check the end of TX CAP
        {
            t_fCAP = getFinalCAP('t');
        }
        else
        {
            // TODO rxBO != 15, this case should be reconsidered, check the end of RX CAP
            t_fCAP = getFinalCAP('r');
        }
    }
    else  // sent as a device
    {
        if (rxBO != 15)  // sent in beacon-enabled, check the end of RX CAP
        {
            t_fCAP = getFinalCAP('r');
        }
        else
        {
            // TODO mpib.macBeaconOrder != 15, this case should be reconsidered, check the end of TX CAP
            t_fCAP = getFinalCAP('t');
        }
    }

    macEV << "[CSMA]: The current CAP will end at " << t_fCAP << " sec \n";
    // evaluate if the entire transmission process can be finished before end of current CAP
    if (tmpf > t_fCAP)
    {
        ok = false;
        macEV << "[CSMA]: Cannot proceed because the entire transmission cannot finish before the end of current CAP \n";
    }
    else
    {
        ok = true;
        macEV << "[CSMA]: Can proceed \n";
    }

    // check if have enough CAP to finish the transaction
    if (!ok)
    {
        bPeriodsLeft = 0;  // in the next superframe, apply a further backoff delay before evaluating once again
        // if as a device, need to track next beacon if it is not being tracked
        if (rxBO != 15)
        {
            if (!bcnRxTimer->isScheduled())
            {
                startBcnRxTimer();
            }
        }
        csmacaWaitNextBeacon = true;
        return false;
    }
    else
    {
        bPeriodsLeft = -1;
        return true;
    }
}

simtime_t IEEE802154Mac::getFinalCAP(char trxType)
{
    simtime_t txSlotDuration, rxSlotDuration, rxBI, txCAP, rxCAP;
    simtime_t now, oneDay, tmpf;

    now = simTime();
    oneDay = now + 24.0 * 3600;

    if ((mpib.getMacBeaconOrder() == 15) && (rxBO == 15))  // non-beacon enabled
    {
        return oneDay;  // transmission can always go ahead
    }

    txSlotDuration = txSfSlotDuration / phy_symbolrate;
    rxSlotDuration = rxSfSlotDuration / phy_symbolrate;
    rxBI = rxSfSpec.BI / phy_symbolrate;

    if (trxType == 't')  // check TX CAP
    {
        if (mpib.getMacBeaconOrder() != 15)  // beacon enabled
        {
            if (txSfSpec.battLifeExt)
            {
                error("[IEEE802154MAC]: this function is TBD");
                /*tmpf = (beaconPeriods + getBattLifeExtSlotNum()) * aUnitBackoffPeriod;
                 t_CAP = bcnTxTime + tmpf;*/
            }
            else
            {
                tmpf = (txSfSpec.finalCap + 1) * txSlotDuration;
                txCAP = mpib.getMacBeaconTxTime() + tmpf;
                macEV << "Last beacon was send at " << mpib.getMacBeaconTxTime() << " actual CAP is " << tmpf << endl;
            }
            return (txCAP >= now) ? txCAP : oneDay;
        }
        else
            return oneDay;
    }
    else  // check RX CAP
    {
        if (rxBO != 15)  // beacon enabled
        {
            if (rxSfSpec.battLifeExt)
            {
                error("[IEEE802154MAC]: this function is TBD");
                /*
                 tmpf = (beaconPeriods2 + getBattLifeExtSlotNum()) * aUnitBackoffPeriod;
                 t_CAP2 = bcnRxTime + tmpf;
                 */
            }
            else
            {
                tmpf = (rxSfSpec.finalCap + 1) * rxSlotDuration;
                macEV << "[getfinalcap]: now = " << now << endl;
                macEV << "[getfinalcap]: tmpf = " << tmpf << endl;
                rxCAP = bcnRxTime + tmpf;
                macEV << "[getfinalcap]: rxCAP = " << rxCAP << endl;
            }

            tmpf = aMaxLostBeacons * rxBI;
            // adjust if beacon loss occurs or during receiving beacon
            if ((rxCAP < now) && (rxCAP + tmpf >= now))  // no more than <aMaxLostBeacons> beacons missed
            {
                macEV << "During receiving a beacon, now is: " << now << ", last beacon was received at " << bcnRxTime << "\n";
                while (rxCAP < now)
                {
                    rxCAP += rxBI;
                }
            }
            return (rxCAP >= now) ? rxCAP : oneDay;
        }
        else
        {
            return oneDay;
        }
    }
}

void IEEE802154Mac::csmaca_handle_RX_ON_confirm(phyState status)
{
    simtime_t now, wtime;

    if (status != phy_RX_ON)
    {
        if (status == phy_BUSY_TX)
        {
            macEV << "[CSMA]: Radio is busy Txing now, RX_ON will be set later \n";
            taskP.taskStatus(TP_RX_ON_CSMACA) = true;
            macEV << "[CSMA]: [MAC_TASK] TP_RX_ON_CSMACA = true \n";
        }
        else
        {
            macEV << "[CSMA]: RX_ON request did not succeed, try again \n";
            handleBackoffTimer();
        }
        return;
    }

    // phy_RX_ON
    // locate backoff boundary if needed
    macEV << "[CSMA]: PHY_RX_ON --> Ok, radio is set to RX_ON \n";
    now = simTime();
    if (beaconEnabled)
    {
        macEV << "[CSMA]: Locate backoff boundary before sending CCA request \n";
        wtime = csmacaLocateBoundary(toParent(tmpCsmaca), 0.0);
    }
    else
    {
        wtime = 0.0;
    }

    if (wtime == 0.0)
    {
        taskP.taskStatus(TP_CCA_CSMACA) = true;
        macEV << "[CSMA]: [MAC-TASK]: TP_CCA_CSMACA = true \n";

        genCCARequest();
    }
    else
    {
        startDeferCCATimer(wtime);
    }
}

// To be called each time that a beacon received or transmitted if backoffStatus == 99
void IEEE802154Mac::csmacaTrxBeacon(char trx)
{
    simtime_t wtime;
    if (!txAck)
        genSetTrxState(phy_RX_ON);

    //update values
    beaconEnabled = ((mpib.getMacBeaconOrder() != 15) || (rxBO != 15));
    csmacaReset(beaconEnabled);

    if (csmacaWaitNextBeacon)
        if (tmpCsmaca && (!backoffTimer->isScheduled()))
        {
            macEV << "[CSMA]: Triggered again by the starting of the new CAP \n";
            ASSERT(bPeriodsLeft >= 0);
            if (bPeriodsLeft == 0)  // backoff again
            {
                macEV << "[CSMA]: bPeriodsLeft == 0, need to re-choose random number of backoffs \n";
                csmacaStart(false, txPkt, csmacaAckReq);
            }
            else  // resume backoff: this is the second entry to CSMA-CA without calling <csmacaStart()>
            {
                /*wtime = csmacaAdjustTime(0.0);
                 macEV << "[CSMA]: wtime = " << wtime << "\n";*/
                macEV << "[CSMA]: bPeriodsLeft > 0, continue with the number of backoffs left from last time \n";
                wtime += bPeriodsLeft * bPeriod;
                if (csmacaCanProceed(wtime, true))  // Should be true since we just made CSMA-CA start
                {
                    startBackoffTimer(wtime);
                }
            }

            return;  // debug here
        }
    csmacaWaitNextBeacon = false;  // FIXME really necessary??
}

//-------------------------------------------------------------------------------/
/***************************** <Timer starter> **********************************/
//-------------------------------------------------------------------------------/
// TODO need to be changed to accept symbols instead of simtime_t
void IEEE802154Mac::startBackoffTimer(simtime_t wtime)
{
    macEV << "[CSMA]: #" << (unsigned int) tmpCsmaca->getSqnr() << ": starting backoff for " << wtime << " sec \n";
    if (backoffTimer->isScheduled())
    {
        cancelEvent(backoffTimer);
    }
    scheduleAt(simTime() + wtime, backoffTimer);
}

void IEEE802154Mac::startRxOnDurationTimer(simtime_t wtime)
{
    if (RxOnDurationTimer->isScheduled())
    {
        cancelEvent(RxOnDurationTimer);
    }
    scheduleAt(simTime() + wtime, RxOnDurationTimer);
    macEV << "Delay " << wtime << " for RxOnDuration \n";
    return;
}

void IEEE802154Mac::startdeferRxEnableTimer(simtime_t wtime)
{
    if (deferRxEnableTimer->isScheduled())
    {
        cancelEvent(deferRxEnableTimer);
    }
    scheduleAt(simTime() + wtime, deferRxEnableTimer);
    macEV << "Delay " << wtime << " before starting to work deferred RX-Enable \n";
    return;
}

void IEEE802154Mac::startDeferCCATimer(simtime_t wtime)
{
    if (deferCCATimer->isScheduled())
    {
        cancelEvent(deferCCATimer);
    }
    scheduleAt(simTime() + wtime, deferCCATimer);
    macEV << "Delay " << wtime << " s before performing CCA \n";
}

void IEEE802154Mac::startBcnRxTimer()
{
    simtime_t now, wtime;
    now = simTime();

    // usual time for next Beacon
    wtime = (aBaseSuperframeDuration * (pow(2, rxBO))) / phy_symbolrate;

    // we want to receive right before the beacon is transmitted
    wtime -= 0.01;

    lastTime_bcnRxTimer = now + wtime;  // XXX parameter needed anymore??
    if (bcnRxTimer->isScheduled())
    {
        cancelEvent(bcnRxTimer);
    }
    scheduleAt(now + wtime, bcnRxTimer);
}

void IEEE802154Mac::startBcnTxTimer(bool txFirstBcn, simtime_t startTime)
{
    if (mpib.getMacBeaconOrder() == 15)
    {
        return;
    }

    simtime_t wtime, now, tmpf;
    now = simTime();

    if (txFirstBcn)  // tx my first beacon
    {
        if (startTime == 0.0 || isCoordinator == true)  // transmit beacon right now for both cases
        {
            txNow_bcnTxTimer = true;
            beaconWaitingTx = true;
            handleBcnTxTimer();  // no delay
            return;
        }
        else
        {
            txNow_bcnTxTimer = false;
            wtime = (aBaseSuperframeDuration * (pow(2, mpib.getMacBeaconOrder()))) / phy_symbolrate;
            macEV << "Scheduling next beacon transmission in " << wtime << endl;
            ASSERT(wtime >= 0.0);

            if (bcnTxTimer->isScheduled())
            {
                cancelEvent(bcnTxTimer);
            }
            scheduleAt(now + wtime, bcnTxTimer);
            return;
        }
    }

    if (!txNow_bcnTxTimer)
    {
        if (bcnTxTimer->isScheduled())
        {
            cancelEvent(bcnTxTimer);
        }
        scheduleAt(now + aTurnaroundTime / phy_symbolrate, bcnTxTimer);
    }
    else if (mpib.getMacBeaconOrder() != 15)
    {
        wtime = (aBaseSuperframeDuration * (1 << mpib.getMacBeaconOrder())) / phy_symbolrate;
        if (bcnTxTimer->isScheduled())
        {
            cancelEvent(bcnTxTimer);
        }
        macEV << "Scheduling next beacon transmission starts in " << wtime << endl;
        scheduleAt(now + wtime, bcnTxTimer);
    }

    txNow_bcnTxTimer = (!txNow_bcnTxTimer);
}

void IEEE802154Mac::startAckTimeoutTimer()
{
    if (ackTimeoutTimer->isScheduled())
    {
        cancelEvent(ackTimeoutTimer);
    }
    scheduleAt(simTime() + mpib.getMacAckWaitDuration() / phy_symbolrate, ackTimeoutTimer);
}

void IEEE802154Mac::startTxAckBoundTimer(simtime_t wtime)
{
    if (txAckBoundTimer->isScheduled())
    {
        cancelEvent(txAckBoundTimer);
    }
    scheduleAt(simTime() + wtime, txAckBoundTimer);
}

void IEEE802154Mac::startTxCmdDataBoundTimer(simtime_t wtime)
{
    if (txCmdDataBoundTimer->isScheduled())
    {
        cancelEvent(txCmdDataBoundTimer);
    }
    scheduleAt(simTime() + wtime, txCmdDataBoundTimer);
}

void IEEE802154Mac::startIfsTimer(bool sifs)
{
    simtime_t wtime;
    if (sifs)  // SIFS
    {
        wtime = aMinSIFSPeriod / phy_symbolrate;
    }
    else
    {
        // LIFS
        wtime = aMinLIFSPeriod / phy_symbolrate;
    }

    if (ifsTimer->isScheduled())
    {
        cancelEvent(ifsTimer);
    }
    scheduleAt(simTime() + wtime, ifsTimer);
}

// this function should be called when TXing a new beacon is received during <handleBcnTxTimer()>
void IEEE802154Mac::startTxSDTimer()
{
    ASSERT(!inTxSD_txSDTimer);
    simtime_t wtime;
    // calculate CAP duration
    wtime = aNumSuperframeSlots * txSfSlotDuration / phy_symbolrate;
    if (txSDTimer->isScheduled())
    {
        cancelEvent(txSDTimer);
    }
    scheduleAt(simTime() + wtime, txSDTimer);
    inTxSD_txSDTimer = true;
}

// this function should be called when a new beacon is received in <handleBeacon()>
void IEEE802154Mac::startRxSDTimer()
{
    ASSERT(!inRxSD_rxSDTimer);
    simtime_t wtime;
    // calculate the remaining time in current CAP
    wtime = aNumSuperframeSlots * rxSfSlotDuration / phy_symbolrate;
    wtime = wtime + bcnRxTime - simTime();
    ASSERT(wtime > 0.0);

    if (rxSDTimer->isScheduled())
    {
        cancelEvent(rxSDTimer);
    }
    scheduleAt(simTime() + wtime, rxSDTimer);
    //macEV << "The current SD will end at " << simTime() + wtime << endl;
    inRxSD_rxSDTimer = true;
}

void IEEE802154Mac::startScanTimer(simtime_t wtime)
{
    ASSERT(scanning);
    if (scanTimer->isScheduled())
    {
        cancelEvent(scanTimer);
    }
    scheduleAt(simTime() + wtime, scanTimer);
}

int IEEE802154Mac::calFrmByteLength(mpdu* frame)
{
    macEV << "Calculating size of " << frame->getName() << endl;
    frameType frmType = (frameType) ((frame->getFcf() & ftMask) >> ftShift);
    int byteLength;  // MHR + MAC payload + MFR
    int MHRLength = calMHRByteLength(((frame->getFcf() & 48) >> damShift) + (frame->getFcf() & 3), (bool) ((frame->getFcf() && 4096) >> secShift));

    if (frmType == Beacon)  // 802.15.4-2006 Specs Fig 44
    {
        byteLength = MHRLength + 6;
    }
    else if (frmType == Data)  // 802.15.4-2006 Specs Fig 52, MAC payload not included here, will be added automatically while encapsulation later on
    {
        //byteLength = MHRLength + 2;
        byteLength = MHRLength + frame->getByteLength();  // constant header length for we always use short address
    }
    else if (frmType == Ack)
    {
        byteLength = SIZE_OF_802154_ACK;
    }
    else if (frmType == Command)
    {
        CmdFrame* cmdFrm;
        if (dynamic_cast<CmdFrame*>(frame))
        {
            cmdFrm = check_and_cast<CmdFrame *>(frame);
        }
        else
        {
            // unpack, Command must be inside
            cmdFrm = check_and_cast<CmdFrame *>(frame->decapsulate());
            frame->encapsulate(cmdFrm);
        }

        switch (cmdFrm->getCmdType())
        {
            case Ieee802154_ASSOCIATION_REQUEST:  // 802.15.4-2006 Specs Fig 55: MHR (17/23) + Payload (2) + FCS (2)
            {
                byteLength = MHRLength + 4;
                break;
            }

            case Ieee802154_ASSOCIATION_RESPONSE: {
                byteLength = SIZE_OF_802154_ASSOCIATION_RESPONSE;
                break;
            }

            case Ieee802154_DISASSOCIATION_NOTIFICATION: {
                byteLength = SIZE_OF_802154_DISASSOCIATION_NOTIFICATION;
                break;
            }

            case Ieee802154_DATA_REQUEST:  // 802.15.4-2006 Specs Fig 59: MHR (7/11/13/17) + Payload (1) + FCS (2)
            {
                byteLength = MHRLength + 3;
                break;
            }

            case Ieee802154_PANID_CONFLICT_NOTIFICATION: {
                byteLength = SIZE_OF_802154_PANID_CONFLICT_NOTIFICATION;
                break;
            }

            case Ieee802154_ORPHAN_NOTIFICATION: {
                byteLength = SIZE_OF_802154_ORPHAN_NOTIFICATION;
                break;
            }

            case Ieee802154_BEACON_REQUEST: {
                byteLength = SIZE_OF_802154_BEACON_REQUEST;
                break;
            }

            case Ieee802154_COORDINATOR_REALIGNMENT:  // 802.15.4-2006 Specs Fig 63: MHR (17/23) + Payload (8) + FCS (2)
            {
                byteLength = MHRLength + 10;
                break;
            }

            case Ieee802154_GTS_REQUEST: {
                byteLength = SIZE_OF_802154_GTS_REQUEST;
                break;
            }

            default: {
                error("[MAC]: cannot calculate the size of a MAC command frame with unknown type!");
                break;
            }
        }  // switch
    }
    else
    {
        error("[MAC]: cannot calculate the size of a MAC frame with unknown type!");
    }

    macEV << "Header size is " << MHRLength << " Bytes \n";
    macEV << "MAC frame size is " << byteLength << " Bytes \n";
    return byteLength;
}

int IEEE802154Mac::calMHRByteLength(unsigned char addrModeSum, bool secu)
{
    int ashSize = 0;
    if (secu)
    {
        ashSize = 14;
    }

    switch (addrModeSum)
    {
        case 0:
            return (3 + ashSize);  // + ASH
        case 2:
            return (7 + ashSize);  // + ASH
        case 3:
            return (13 + ashSize);  //+ ASH
        case 4:
            return (11 + ashSize);  //+ ASH
        case 5:
            return (17 + ashSize);  //+ ASH
        case 6:
            return (23 + ashSize);  //+ ASH
        default: {
            error("[MAC]: impossible address mode sum!");
            break;
        }
    }  // switch

    return 0;
}

simtime_t IEEE802154Mac::calDuration(mpdu* frame)
{
    return (phyHeaderLength * 8 + frame->getByteLength() * 8) / phy_bitrate;
}

// Check if this Frame is for our Coordinator or not
bool IEEE802154Mac::toParent(mpdu* frame)
{
    frame->getSrc();
    if ((frame->getDest().getInt() == mpib.getMacCoordExtendedAddress().getInt()))
    {
        return true;
    }
    else
    {
        return false;
    }
}

//--------------------------------------------------------------------------------/
/******************************* <Timer Handler> ********************************/
//--------------------------------------------------------------------------------/
void IEEE802154Mac::handleBackoffTimer()
{
    macEV << "[CSMA]: Before sending CCA primitive, tell PHY to turn on the receiver \n";
    taskP.taskStatus(TP_RX_ON_CSMACA) = true;
    genSetTrxState(phy_RX_ON);
    return;
}

void IEEE802154Mac::handledeferRxEnableTimer()
{
    macEV << "Defer RX-Enable Timer ran out turning RX ON for RxOnDuration \n";
    genSetTrxState(phy_RX_ON);
    mlmeRxEnable = true;
    startRxOnDurationTimer(deferRxEnable->getRxOnDuration());
    return;
}

void IEEE802154Mac::handleRxOnDurationTimer()
{
    genSetTrxState(phy_TRX_OFF);
    return;
}

void IEEE802154Mac::handleDeferCCATimer()
{
    taskP.taskStatus(TP_CCA_CSMACA) = true;
    macEV << "[MAC-TASK]: TP_CCA_CSMACA = true \n";
    genCCARequest();
    return;
}

void IEEE802154Mac::handleBcnRxTimer()
{
    if (rxBO != 15)  // beacon enabled (do nothing if beacon not enabled)
    {
        if (txAck)
        {
            delete txAck;
            txAck = NULL;
        }
        // enable the receiver
        genSetTrxState(phy_RX_ON);

        if (bcnLossCounter != 0)
        {
            numCollision++;
        }
        bcnLossCounter++;
        startBcnRxTimer();
    }
    return;
}

void IEEE802154Mac::handleBcnTxTimer()
{
    if (mpib.getMacBeaconOrder() < 15)  // for Beacon-enabled PANs (beacon order < 0x0F)
    {
        if (!txNow_bcnTxTimer)  // enable the transmitter
        {
            beaconWaitingTx = true;
            if (txAck)  // transmitting beacon has highest priority
            {
                delete txAck;
                txAck = NULL;
            }
            genSetTrxState(phy_TX_ON);
        }
        else  // it's time to transmit beacon
        {
            ASSERT(txBeacon == NULL);
            // construct a beacon
            beaconFrame* tmpBcn = new beaconFrame();
            tmpBcn->setName("Ieee802154BEACONTimer");

            // construct frame control field
            tmpBcn->setFcf(fcf->genFCF(Beacon, mpib.getMacSecurityEnabled(), false, false, false, addrLong, 1, addrLong));

            tmpBcn->setSqnr((mpib.getMacBSN() + 1));
            tmpBcn->setSrc(myMacAddr);
            tmpBcn->setDestPANid(0xffff);  // ignored upon reception
            tmpBcn->setDest(MACAddressExt::BROADCAST_ADDRESS);  // ignored upon reception
            tmpBcn->setSrcPANid(mpib.getMacPANId());

            // construct superframe specification
            txSfSpec.BO = mpib.getMacBeaconOrder();
            txSfSpec.BI = aBaseSuperframeDuration * (1 << mpib.getMacBeaconOrder());
            txSfSpec.SO = mpib.getMacSuperframeOrder();
            txSfSpec.SD = aBaseSuperframeDuration * (1 << mpib.getMacSuperframeOrder());
            txSfSpec.battLifeExt = mpib.getMacBattLifeExt();
            txSfSpec.panCoor = isCoordinator;
            txSfSpec.assoPmt = mpib.getMacAssociationPermit();

            // this parameter may vary each time when new GTS slots were allocated in last superframe
            txSfSpec.finalCap = tmp_finalCap;
            tmpBcn->setSfSpec(txSfSpec);

            // populate the GTS fields -- TODO more TBD when considering GTS
            for (int i = 0; i < 7; i++)
            {
                tmpBcn->setGtsList(0, gtsList[i]);
            }
            //pendingAddrFields
            //tmpBcn->setPaFields(txPaFields);

            tmpBcn->setByteLength(calFrmByteLength(tmpBcn));

            txBeacon = tmpBcn;  // released in taskSuccess or in PD_DATA_confirm (if TX failure)
            txPkt = tmpBcn;
            mpib.setMacBeaconTxTime(simTime());  // no delay
            inTransmission = true;  // cleared by PD_DATA_confirm
            send(txBeacon->dup(), "outPD");  // send a duplication
            macEV << "Sending frame " << txBeacon->getName() << " (" << txBeacon->getByteLength() << " Bytes) to PHY layer \n";
            macEV << "The estimated transmission time is " << calDuration(txBeacon) << " sec \n";

            startTxSDTimer();

            // schedule for GTS if they exist
            if (gtsCount > 0)
            {
                index_gtsTimer = gtsCount - 1;  // index of GTS in the list that will come first
                gtsScheduler();
            }
        }
    }
    startBcnTxTimer(false, simTime());  // don't disable this even if beacon is not enabled (beacon may be temporarily disabled like in channel scan, but it will be enabled again)
}

void IEEE802154Mac::handleAckTimeoutTimer()
{
    ASSERT(txBcnCmd || txBcnCmdUpper || txData);
    dispatch(phy_BUSY, __FUNCTION__);  // the status p_BUSY will be ignore
}

void IEEE802154Mac::handleTxAckBoundTimer()
{
    if (!beaconWaitingTx)
        if (txAck)  // <txAck> may have been canceled by <macBeaconRxTimer>
        {
            send(txAck->dup(), "outPD");
        }
}

// Called when txCmdDataBoundTimer expires or directly by <handle_PLME_SET_TRX_STATE_confirm()>
// Data or Command is sent to PHY layer here
void IEEE802154Mac::handleTxCmdDataBoundTimer()
{
    if (txBcnCmd == txCsmaca)
    {
        txPkt = txBcnCmd;
        sendDown(check_and_cast<mpdu *>(txBcnCmd->dup()));
        return;
    }
    else if (txBcnCmdUpper == txCsmaca)
    {
        txPkt = txBcnCmdUpper;
        sendDown(check_and_cast<mpdu *>(txBcnCmdUpper->dup()));
        return;
    }
    else if (txData == txCsmaca)
    {
        txPkt = txData;
        sendDown(check_and_cast<mpdu *>(txData->dup()));
        return;
    }
}

// this function is called after delay of IFS following sending out the ACK requested by reception of a data or command packet
// or following the reception of a data packet requiring no ACK
// other command packets requiring no ACK are processed in <handleCommand()>
void IEEE802154Mac::handleIfsTimer()
{
    //MACenum status;
    //int i;

    ASSERT(rxData != NULL || rxCmd != NULL || txGTS != NULL);

    if (rxCmd)  // MAC command, all commands requiring ACK are handled here
    {
    }
    else if (rxData)
    {
        sendMCPSDataIndication(rxData);
        rxData = NULL;
    }
    else if (txGTS)
    {
        ASSERT(taskP.taskStatus(TP_MCPS_DATA_REQUEST) && (taskP.taskStep(TP_MCPS_DATA_REQUEST) == 4));
        FSM_MCPS_DATA_request();
    }
}

void IEEE802154Mac::handleSDTimer()  // we must make sure that outgoing CAP and incoming CAP never overlap
{
    inTxSD_txSDTimer = false;
    inRxSD_rxSDTimer = false;

    // If PAN coordinator uses GTS, this is the end of CFP, reset indexCurrGts
    if (isCoordinator)
    {
        indexCurrGts = 99;
        macEV << "Entering inactive period, turn off radio and go to sleep \n";
        if (!txAck)
        {
            genSetTrxState(phy_TRX_OFF);
        }
        else
            numTxAckInactive++;
    }
    else if ((!waitBcnCmdAck) && (!waitBcnCmdUpperAck) && (!waitDataAck))  // not waiting for ACK
    {
        macEV << "Entering inactive period, turn off radio and go to sleep \n";
        genSetTrxState(phy_TRX_OFF);
    }
    else
    {
        macEV << "Entering inactive period, but cannot go to sleep \n";
    }
}

void IEEE802154Mac::handleScanTimer()
{
    doScan();
}

//-------------------------------------------------------------------------------/
//******************************* <GTS Functions> *******************************/
//+++++++++++ taken and adjusted from Feng Chen's IEEE 802.15.4 Model +++++++++++/
//+++++++ https://www7.informatik.uni-erlangen.de/~fengchen/omnet/802154/ +++++++/
//-------------------------------------------------------------------------------/
/**
 * PAN Coordinator uses this function to schedule for starting of each GTS in the GTS list
 */
void IEEE802154Mac::gtsScheduler()
{
    //unsigned char t_SO;
    simtime_t w_time, tmpf;

    ASSERT(isCoordinator);
    tmpf = mpib.getMacBeaconTxTime() + gtsList[index_gtsTimer].startSlot * txSfSlotDuration / phy_symbolrate;
    w_time = tmpf - simTime();

    // should turn on radio receiver aTurnaroundTime symbols before GTS starts, if it is a transmit GTS relative to device
    if (!gtsList[index_gtsTimer].isRecvGTS)
    {
        w_time = w_time - (aTurnaroundTime / phy_symbolrate);
    }

    macEV << "[GTS]: Schedule to start GTS index: " << index_gtsTimer << ", slot:#" << (int) gtsList[index_gtsTimer].startSlot << " with " << w_time << " sec \n";
    startGtsTimer(w_time);
}

void IEEE802154Mac::startGtsTimer(simtime_t w_time)
{
    if (gtsTimer->isScheduled())
    {
        cancelEvent(gtsTimer);
    }
    scheduleAt(simTime() + w_time, gtsTimer);
}

void IEEE802154Mac::handleGtsTimer()
{
    simtime_t w_time;

    // for PAN coordinator
    if (isCoordinator)
    {
        indexCurrGts = index_gtsTimer;
        macEV << "[GTS]: GTS with index = " << (int) indexCurrGts << " in my GTS list starts now! \n";
        macEV
        << "allocated for MAC address = " << (int) gtsList[indexCurrGts].devShortAddr << ", startSlot = " << (int) gtsList[indexCurrGts].startSlot << ", length = "
                << (int) gtsList[indexCurrGts].length << ", isRecvGTS = " << gtsList[indexCurrGts].isRecvGTS << ", isTxPending = " << gtsList[indexCurrGts].isTxPending << endl;

        // is transmit GTS relative to device , turn on radio receiver
        if (!gtsList[indexCurrGts].isRecvGTS)
        {
            macEV << "[GTS]: tell PHY to turn on radio receiver and prepare for receiving packets from device \n";
            genSetTrxState(phy_RX_ON);
        }
        // my time to transmit packets to a certain device
        else
        {
            // if there is a data buffered for transmission in current GTS
            if (txGTS && gtsList[indexCurrGts].isTxPending)
            {
                ASSERT(taskP.taskStatus(TP_MCPS_DATA_REQUEST) && taskP.taskStep(TP_MCPS_DATA_REQUEST) == 1);
                // hand over to FSM, which will go to next step
                // no need to call gtsCanProceed() at this time, timing is already guaranteed when allocating GTS
                macEV << "[GTS]: Data pending for this GTS found in the buffer, starting GTS transmission now \n";
                FSM_MCPS_DATA_request();         // state parameters are ignored
            }
            // turn off radio
            else
            {
                macEV << "[GTS]: no data pending for current transmit GTS, turn off radio now \n";
                genSetTrxState(phy_TRX_OFF);
            }
        }

        if (index_gtsTimer > 0)
        {
            index_gtsTimer--;
            gtsScheduler();
        }
        // index_gtsTimer == 0, now is the starting of the last GTS in current CFP. need to do nothing here
        // at the end of CFP, indexCurrGts will be reset in handleSDTimer()
    }

    // for devices
    else
    {
        // my GTS starts
        if (index_gtsTimer != 99)
        {
            macEV << "[GTS]: my GTS starts now, isRecvGTS = " << isRecvGTS << endl;

            // is receive GTS, turn on radio receiver
            if (isRecvGTS)
            {
                macEV << "[GTS]: tell PHY to turn on radio receiver and prepare for receiving packets from PAN coordinator \n";
                genSetTrxState(phy_RX_ON);
            }
            // my GTS to transmit packets to PAN coordinator
            else
            {
                // each device can have at most one GTS
                if (txGTS)
                {
                    ASSERT(taskP.taskStatus(TP_MCPS_DATA_REQUEST) && taskP.taskStep(TP_MCPS_DATA_REQUEST) == 1);
                    // hand over to FSM, which will go to next step
                    // no need to call gtsCanProceed() at this time, timing is already guaranteed when applying for GTS
                    macEV << "[GTS]: a data is pending for this GTS in the buffer, starting GTS transmission now \n";
                    FSM_MCPS_DATA_request();     // state parameters are ignored
                }
                else
                {
                    macEV << "[GTS]: no data pending for GTS transmission, turn off radio now \n";
                    // to avoid several consecutive genSetTRXState are called at the same time, which may lead to error operation,
                    // use phy_FORCE_TRX_OFF to turn off radio, because PHY will not send back a confirm from it
                    genSetTrxState(phy_FORCE_TRX_OFF);
                    // if data from upper layer arrives during this GTS, radio may be turned on again
                }
            }

            // schedule for the end of my GTS slot in order to put radio into sleep when GTS of current CFP expires
            index_gtsTimer = 99;
            // calculate the duration of my GTS
            w_time = gtsLength * rxSfSlotDuration / phy_symbolrate;
            if (isRecvGTS)
            {
                w_time += aTurnaroundTime / phy_symbolrate;
            }
            macEV << "[GTS]: scheduling for the end of my GTS slot \n";
            startGtsTimer(w_time);
        }
        // my GTS expired, turn off radio
        else
        {
            index_gtsTimer = 0;
            macEV << "[GTS]: now is the end of my GTS, turn off radio now \n";
            genSetTrxState(phy_TRX_OFF);
        }
    }
}

/**
 *  This function accepts GTS request from devices and allocates GTS slots
 *  The return value indicates the GTS start slot for corresponding device
 *  Note: devices are allowed to call this function only in CAP
 */
unsigned char IEEE802154Mac::gts_request_cmd(unsigned short macShortAddr, unsigned char length, bool isReceive)
{
    Enter_Method_Silent
    ();
    unsigned char startSlot;
    unsigned int t_cap;
    ASSERT(isCoordinator);
    macEV << "[GTS]: the PAN coordinator is processing GTS request from MAC address " << macShortAddr << endl;
    // check whether this device is device list
    if (deviceList.find(macShortAddr) == deviceList.end())
    {
        macEV << "[GTS]: the address " << macShortAddr << " not found in the device list, no GTS allocated \n";
        return 0;
    }
    else if (gtsCount >= 7)
    {
        macEV << "[GTS]: max number of GTSs reached, no GTS available for the MAC address " << macShortAddr << endl;
        return 0;
    }

    // check if the minCAP length can still be satisfied after this allocation
    t_cap = txSfSlotDuration * (tmp_finalCap - length + 1);
    if (t_cap < aMinCAPLength)
    {
        macEV << "[GTS]: violation of the min CAP length, no GTS allocated for the MAC address " << macShortAddr << endl;
        return 0;
    }

    // update final CAP and calculate start slot for this GTS
    tmp_finalCap = tmp_finalCap - length;
    startSlot = tmp_finalCap + 1;
    // add new GTS descriptor
    gtsList[gtsCount].devShortAddr = macShortAddr;
    gtsList[gtsCount].startSlot = startSlot;
    gtsList[gtsCount].length = length;
    gtsList[gtsCount].isRecvGTS = isReceive;
    macEV
    << "[GTS]: add a new GTS descriptor with index = " << (int) gtsCount << " for MAC address = " << macShortAddr << ", startSlot = " << (int) startSlot << ", length = " << (int) length
            << ", isRecvGTS = " << isReceive << endl;
    gtsCount++;
    // new parameters put into effect when transmitting next beacon
    //isGtsUpdate = true;
    return startSlot;
}

void IEEE802154Mac::startFinalCapTimer(simtime_t w_time)
{
    if (finalCAPTimer->isScheduled())
    {
        cancelEvent(finalCAPTimer);
    }
    scheduleAt(simTime() + w_time, finalCAPTimer);
}

void IEEE802154Mac::handleFinalCapTimer()
{
    // only be called when my GTS is not the first one in the CFP
    ASSERT(gtsStartSlot > rxSfSpec.finalCap + 1);
    macEV << "[GTS]: it's now the end of CAP, turn off radio and wait for my GTS to start \n";
    genSetTrxState(phy_TRX_OFF);
}

/**
 *  Devices should check if the requested GTS transaction can be finished  before the end of corresponding GTS
 *  no need for PAN coordinator to do this, because it only transmits at the starting of the GTS,
 *  of which the timing is already guaranteed by device side when requesting for receive GTS
 */
bool IEEE802154Mac::gtsCanProceed()
{
    simtime_t t_left;
    macEV << "[GTS]: checking if the data transaction can be finished before the end of current GTS ... ";
    // to be called only by the device in its GTS
    ASSERT(index_gtsTimer == 99 && gtsTimer->isScheduled());
    // calculate left time in current GTS
    t_left = gtsTimer->getArrivalTime() - simTime();

    if (t_left > gtsTransDuration)
    {
        macEV << "YES \n";
        return true;
    }
    else
    {
        macEV << "NO \n";
        return false;
    }
}
//-------------------------------------------------------------------------------/
/********************** <FSM and Pending Task Processing> ***********************/
//+++++++++++ taken and adjusted from Feng Chen's IEEE 802.15.4 Model +++++++++++/
//+++++++ https://www7.informatik.uni-erlangen.de/~fengchen/omnet/802154/ +++++++/
//-------------------------------------------------------------------------------/
void IEEE802154Mac::taskSuccess(char type, bool csmacaRes)
{
    if (type == 'b')  //beacon
    {
        double tmpf;            // XXX fix to reduce variable scope
        unsigned char ifs;      // XXX fix to reduce variable scope
        unsigned short t_CAP;   // XXX fix to reduce variable scope

        macEV << "taskSuccess for sending beacon \n";
        if (!txBeacon)
        {
            ASSERT(txBcnCmd);
            txBcnCmd = 0;
            return;
        }
        //--- calculate CAP ---
        if (txBeacon->getByteLength() <= aMaxSIFSFrameSize)
        {
            ifs = aMinSIFSPeriod;
        }
        else
        {
            ifs = aMinLIFSPeriod;
        }

        // calculate <txBcnDuration>
        tmpf = (calDuration(txBeacon) * phy_symbolrate).dbl();
        tmpf += ifs;
        txBcnDuration = (unsigned char) (tmpf / aUnitBackoffPeriod);
        if (fmod(tmpf, aUnitBackoffPeriod) > 0.0)
        {
            txBcnDuration++;
        }
        macEV << "Calculating txBcnDuration = " << (int) txBcnDuration << endl;

        t_CAP = (txSfSpec.finalCap + 1) * txSfSlotDuration / aUnitBackoffPeriod - txBcnDuration;

        if (t_CAP == 0)  // it's possible that there is no time left in current CAP after TXing the beacon
        {
            csmacaRes = false;
            macEV << "No time left in current CAP after transmitting the beacon, trying to turn off radio \n";
            genSetTrxState(phy_TRX_OFF);
        }
        else
        {
            macEV << "Turn on radio receiver after transmitting the beacon \n";
            genSetTrxState(phy_RX_ON);
        }
        // CSMA-CA may be waiting for the new beacon
        if (backoffStatus == 99)
        {
            csmacaTrxBeacon('t');
        }
        //----------------------
        beaconWaitingTx = false;
        delete txBeacon;
        txBeacon = NULL;
        /*
         // send out delayed ACK
         if (txAck)
         {
         csmacaRes = false;
         genSetTRXState(p_TX_ON);
         }
         */
        numTxBcnPkt++;
    }
    else if (type == 'a')  // ACK
    {
        ASSERT(txAck);
        delete txAck;
        txAck = NULL;
    }
    else if (type == 'c')  // Command
    {
        ASSERT(txBcnCmd);
        // if it is a pending packet, delete it from the pending list
        delete txBcnCmd;
        txBcnCmd = NULL;
    }
    else if (type == 'C')  //command
    {
        ASSERT(txBcnCmdUpper);
        delete txBcnCmdUpper;
        txBcnCmdUpper = NULL;
    }
    else if (type == 'd')  //data
    {
        ASSERT(txData);
        macEV << "taskSuccess for sending : " << txData->getName() << ":#" << (unsigned int) txData->getSqnr() << endl;
        //Packet *p = txData;
        sendMCPSDataConf(mac_SUCCESS, txData->getId());
        delete txData;
        txData = NULL;
        numTxDataSucc++;
        // request another Msg from IFQ (if it exists), only when MAC is idle for data transmission
        if (!taskP.taskStatus(TP_MCPS_DATA_REQUEST))
        {
            if (txBuffSlot > 0)
            {
                Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                taskP.taskStatus(task) = true;
                switch (dataTransMode)
                {
                    case DIRECT_TRANS:
                    case INDIRECT_TRANS: {
                        taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                        taskP.taskStep(task)++;
                        strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        csmacaEntry('d');
                        break;
                    }

                    case GTS_TRANS: {
                        taskP.mcps_data_request_TxOption = GTS_TRANS;
                        taskP.taskStep(task)++;
                        // waiting for GTS arriving, callback from handleGtsTimer()
                        strcpy(taskP.taskFrFunc(task), "handleGtsTimer");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        numGTSRetry = 0;

                        // If I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                        // If I'm the device, should try to transmit if now is in my GTS
                        // refer to 802.15.4-2006 Spec. Section 7.5.7.3
                        if (index_gtsTimer == 99)
                        {
                            ASSERT(gtsTimer->isScheduled());
                            // first check if the requested transaction can be completed before the end of current GTS
                            if (gtsCanProceed())
                            {
                                // directly hand over to FSM, which will go to next step, state parameters are ignored
                                FSM_MCPS_DATA_request();
                            }
                        }
                        break;
                    }

                    default: {
                        error("[MAC]: undefined txOption: %d!", dataTransMode);
                        break;
                    }
                }
            }
            else
            {
                reqMsgBuffer();
            }
        }
        // if it is a pending packet, delete it from the pending list
        //updateTransacLinkByPktOrHandle(tr_oper_del,&transacLink1,&transacLink2,p);
    }
    else if (type == 'g')  // GTS
    {
        csmacaRes = false;
        ASSERT(txGTS);
        macEV << "[GTS]: taskSuccess for sending : " << txGTS->getName() << ":#" << (unsigned int) txGTS->getSqnr() << endl;
        // if PAN coordinator, need to clear isTxPending in corresponding GTS descriptor
        if (isCoordinator)
        {
            ASSERT(gtsList[indexCurrGts].isTxPending);
            gtsList[indexCurrGts].isTxPending = false;
        }
        sendMCPSDataConf(mac_SUCCESS, txGTS->getId());
        delete txGTS;
        txGTS = NULL;
        numTxGTSSucc++;

        // request another Msg from IFQ (if it exists), only when MAC is idle for data transmission
        if (!taskP.taskStatus(TP_MCPS_DATA_REQUEST))
        {
            if (txBuffSlot > 0)
            {
                Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                taskP.taskStatus(task) = true;
                switch (dataTransMode)
                {
                    case DIRECT_TRANS:
                    case INDIRECT_TRANS: {
                        taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                        taskP.taskStep(task)++;
                        strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        csmacaEntry('d');
                        break;
                    }

                    case GTS_TRANS: {
                        taskP.mcps_data_request_TxOption = GTS_TRANS;
                        taskP.taskStep(task)++;
                        // waiting for GTS arriving, callback from handleGtsTimer()
                        strcpy(taskP.taskFrFunc(task), "handleGtsTimer");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        numGTSRetry = 0;

                        // If I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                        // If I'm the device, should try to transmit if now is in my GTS
                        // refer to 802.15.4-2006 Spec. Section 7.5.7.3
                        if (index_gtsTimer == 99)
                        {
                            ASSERT(gtsTimer->isScheduled());
                            // first check if the requested transaction can be completed before the end of current GTS
                            if (gtsCanProceed())
                            {
                                // directly hand over to FSM, which will go to next step, state parameters are ignored
                                FSM_MCPS_DATA_request();
                            }
                        }
                        break;
                    }

                    default: {
                        error("[MAC]: undefined txOption: %d!", dataTransMode);
                        break;
                    }
                }
            }
            else
            {
                reqMsgBuffer();
            }
        }
    }
    else
    {
        ASSERT(0);
    }

    if (csmacaRes)
    {
        csmacaResume();
    }
}

void IEEE802154Mac::taskFailed(char type, MACenum status, bool csmacaRes)
{
    if ((type == 'b')  // Beacon
    || (type == 'a')  // ACK
            || (type == 'c')) // Command
    {
        ASSERT(0);  // We don't handle the above failures here
    }
    else if (type == 'C')  // Command from Upper
    {
        delete txBcnCmdUpper;
        txBcnCmdUpper = NULL;
    }
    else if (type == 'd')  // Data
    {
        ASSERT(txData);
        macEV << "taskFailed for sending : " << txData->getName() << ":#" << (unsigned int) txData->getSqnr() << endl;
        sendMCPSDataConf(status, txData->getId());
        delete txData;
        txData = NULL;
        numTxDataFail++;
        if (csmacaRes)
        {
            csmacaResume();
        }
        // request another Msg from IFQ (if it exists), only when MAC is idle for data transmission
        if (!taskP.taskStatus(TP_MCPS_DATA_REQUEST))
        {
            if (txBuffSlot > 0)
            {
                Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                taskP.taskStatus(task) = true;
                switch (dataTransMode)
                {
                    case DIRECT_TRANS:
                    case INDIRECT_TRANS: {
                        taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                        taskP.taskStep(task)++;
                        strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        csmacaEntry('d');
                        break;
                    }

                    case GTS_TRANS: {
                        taskP.mcps_data_request_TxOption = GTS_TRANS;
                        taskP.taskStep(task)++;
                        // waiting for GTS arriving, callback from handleGtsTimer()
                        strcpy(taskP.taskFrFunc(task), "handleGtsTimer");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        numGTSRetry = 0;

                        // if I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                        // if I'm the device, should try to transmit if now is in my GTS
                        // refer to 802.15.4-2006 Spec. Section 7.5.7.3
                        if (index_gtsTimer == 99)
                        {
                            ASSERT(gtsTimer->isScheduled());
                            // first check if the requested transaction can be completed before the end of current GTS
                            if (gtsCanProceed())
                            {
                                // directly hand over to FSM, which will go to next step, state parameters are ignored
                                FSM_MCPS_DATA_request();
                            }
                        }
                        break;
                    }

                    default: {
                        error("[MAC]: undefined txOption: %d!", dataTransMode);
                        break;
                    }
                }
            }
            else
            {
                reqMsgBuffer();
            }
        }
    }
    else if (type == 'g')  // GTS
    {
        ASSERT(txGTS);
        macEV << "taskFailed for sending : " << txGTS->getName() << ":#" << (unsigned int) txGTS->getSqnr() << endl;
        // if PAN coordinator, need to clear isTxPending in corresponding GTS descriptor
        if (isCoordinator)
        {
            ASSERT(gtsList[indexCurrGts].isTxPending);
            gtsList[indexCurrGts].isTxPending = false;
        }
        sendMCPSDataConf(status, txGTS->getId());
        delete txGTS;
        txGTS = NULL;
        numTxGTSFail++;

        // request another Msg from IFQ (if it exists), only when MAC is idle for data transmission
        if (!taskP.taskStatus(TP_MCPS_DATA_REQUEST))
        {
            if (txBuffSlot > 0)
            {
                Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
                taskP.taskStatus(task) = true;
                switch (dataTransMode)
                {
                    case DIRECT_TRANS:
                    case INDIRECT_TRANS: {
                        taskP.mcps_data_request_TxOption = DIRECT_TRANS;
                        taskP.taskStep(task)++;
                        strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_request");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        csmacaEntry('d');
                        break;
                    }

                    case GTS_TRANS: {
                        taskP.mcps_data_request_TxOption = GTS_TRANS;
                        taskP.taskStep(task)++;
                        // waiting for GTS arriving, callback from handleGtsTimer()
                        strcpy(taskP.taskFrFunc(task), "handleGtsTimer");
                        ASSERT(txData == NULL);
                        txData = getTxMsg();
                        numGTSRetry = 0;

                        // if I'm the PAN coordinator, should defer the transmission until the start of the receive GTS
                        // if I'm the device, should try to transmit if now is in my GTS
                        // refer to 802.15.4-2006 Spec. Section 7.5.7.3
                        if (index_gtsTimer == 99)
                        {
                            ASSERT(gtsTimer->isScheduled());
                            // first check if the requested transaction can be completed before the end of current GTS
                            if (gtsCanProceed())
                            {
                                // directly hand over to FSM, which will go to next step, state parameters are ignored
                                FSM_MCPS_DATA_request();
                            }
                        }
                        break;
                    }

                    default: {
                        error("[MAC]: undefined txOption: %d!", dataTransMode);
                        break;
                    }
                }  // switch
            }
            else
            {
                reqMsgBuffer();
            }
        }
    }
}

// check if there is a task of the specified type is now pending
void IEEE802154Mac::checkTaskOverflow(Ieee802154MacTaskType task)
{
    if (taskP.taskStatus(task))
    {
        error("[MAC-TASK]: task overflow: %d!", task);
    }
    else
    {
        taskP.taskStep(task) = 0;
        (taskP.taskFrFunc(task))[0] = 0;
    }
}

void IEEE802154Mac::FSM_MCPS_DATA_request(phyState pStatus, MACenum mStatus)
{
    Ieee802154MacTaskType task = TP_MCPS_DATA_REQUEST;
    Ieee802154TxOption txOption = taskP.mcps_data_request_TxOption;

    if (txOption == DIRECT_TRANS)
    {
        switch (taskP.taskStep(task))
        {
            case 0: {
                // something impossible happened here
                break;
            }

            case 1: {
                if (pStatus == phy_IDLE)  // CSMA/CA succeeds
                {
                    taskP.taskStep(task)++;
                    strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_confirm");
                    // enable the transmitter
                    genSetTrxState(phy_TX_ON);
                }
                else  // CSMA-CA reports channel access failure, should report to SSCS through MCPS_DATA_confirm in Spec
                      // here simply retry
                {
                    csmacaResume();
                }
                break;
            }  // case 1

            case 2: {
                if ((txData->getFcf() && 128) >> 7)  // ACK required
                {
                    taskP.taskStep(task)++;
                    strcpy(taskP.taskFrFunc(task), "handleAck");
                    macEV << "[MAC-TASK-handleAck]: Data transmitted and timer for ACK is started now \n";
                    genSetTrxState(phy_RX_ON);
                    startAckTimeoutTimer();
                    waitDataAck = true;
                }
                else  // assume success if ACK not required
                {
                    taskP.taskStatus(task) = false;  // task ends successfully
                    macEV << "[MAC-TASK-SUCCESS]: reset TP_MCPS_DATA_REQUEST \n";
                    resetTRX();
                    taskSuccess('d');
                }
                break;
            }  // case 2

            case 3: {
                if (pStatus == phy_SUCCESS)  // ACK received
                {
                    macEV << "ACK for " << txData->getName() << ":#" << (unsigned int) txData->getSqnr() << " received successfully \n";
                    taskP.taskStatus(task) = false;  // task ends successfully
                    macEV << "[MAC-TASK-SUCCESS]: reset TP_MCPS_DATA_REQUEST \n";
                    waitDataAck = false;  // debug
                    resetTRX();
                    taskSuccess('d');
                }
                else  // time out when waiting for ACK
                {
                    macEV << "ACK timeout for " << txData->getName() << ":#" << (unsigned int) txData->getSqnr() << endl;
                    numDataRetry++;
                    if (numDataRetry <= aMaxFrameRetries)
                    {
                        macEV << "Starting " << numDataRetry << "th retry for " << txData->getName() << ":#" << (unsigned int) txData->getSqnr() << endl;
                        taskP.taskStep(task) = 1;  // go back to step 1
                        strcpy(taskP.taskFrFunc(task), "csmacaCallBack");
                        waitDataAck = false;
                        csmacaResume();
                    }
                    else
                    {
                        macEV << "The max #num of retries reached \n";
                        taskP.taskStatus(task) = false;  // task fails
                        macEV << "[MAC-TASK-FAIL]: reset TP_MCPS_DATA_REQUEST \n";
                        waitDataAck = false;  // debug
                        resetTRX();
                        taskFailed('d', mac_NO_ACK);
                    }
                }
                break;
            }  // case 3

            default: {
                break;
            }
        }  // switch
    }  // if (txOption == DIRECT_TRANS)
    else if (txOption == GTS_TRANS)
    {
        switch (taskP.taskStep(task))
        {
            case 0: {
                // something impossible happened here
                break;
            }

            case 1: {
                // two possible callbacks, one from handleGtsTimer() at the starting of one GTS
                // the other directly from MCPS_DATA_request(), only possible for devices when receiving a data from upper layer during the GTS
                taskP.taskStep(task)++;
                strcpy(taskP.taskFrFunc(task), "handle_PD_DATA_confirm");
                // should transmit right now, since the timing is very strictly controlled in GTS,
                // we can simply use phy_FORCE_TRX_OFF and then phy_TX_ON to turn on transmitter immediately
                genSetTrxState(phy_FORCE_TRX_OFF);
                genSetTrxState(phy_TX_ON);
                // data will be sent to PHY in handle_PLME_SET_TRX_STATE_confirm()
                break;
            }  // case 1

            case 2:  // data successfully transmitted
            {
                if ((txGTS->getFcf() && 128) >> 7)  // ACK required
                {
                    taskP.taskStep(task)++;
                    strcpy(taskP.taskFrFunc(task), "handleAck");
                    // enable the receiver
                    macEV << "[GTS]: data successfully transmitted, turn on radio and wait for ACK \n";
                    genSetTrxState(phy_RX_ON);
                    startAckTimeoutTimer();
                    waitGTSAck = true;
                }
                else  // assume success if ACK not required
                {
                    macEV << "[GTS]: data successfully transmitted, no ACK required, turn off radio now \n";
                    genSetTrxState(phy_FORCE_TRX_OFF);
                    macEV << "[GTS]: need to delay IFS before next GTS transmission can proceed \n";
                    taskP.taskStep(task) = 4;
                    strcpy(taskP.taskFrFunc(task), "handleIfsTimer");
                    if (txGTS->getByteLength() <= aMaxSIFSFrameSize)
                    {
                        startIfsTimer(true);
                    }
                    else
                    {
                        startIfsTimer(false);
                    }
                }
                break;
            }  // case 2

            case 3: {
                if (pStatus == phy_SUCCESS)  // ACK received
                {
                    waitGTSAck = false;
                    macEV << "[GTS]: ACK for " << txGTS->getName() << ":#" << (unsigned int) txGTS->getSqnr() << " received successfully, turn off radio now \n";
                    genSetTrxState(phy_FORCE_TRX_OFF);
                    macEV << "[GTS]: need to delay IFS before next GTS transmission can proceed \n";
                    taskP.taskStep(task)++;
                    strcpy(taskP.taskFrFunc(task), "handleIfsTimer");
                    if (txGTS->getByteLength() <= aMaxSIFSFrameSize)
                    {
                        startIfsTimer(true);
                    }
                    else
                    {
                        startIfsTimer(false);
                    }
                }
                else  // time out when waiting for ACK, normally impossible in GTS
                {
                    macEV << "[MAC]: ACK timeout for " << txGTS->getName() << ":#" << (unsigned int) txGTS->getSqnr() << endl;
                    numGTSRetry++;
                    if (numGTSRetry <= aMaxFrameRetries)
                    {
                        // retry in next GTS
                        macEV << "[GTS]: retry in this GTS of next superframe, turn off radio now \n";
                        taskP.taskStep(task) = 1;  // go back to step 1
                        strcpy(taskP.taskFrFunc(task), "handleGtsTimer");
                        waitGTSAck = false;
                        // to avoid several consecutive genSetTRXState are called at the same time, which may lead to error operation,
                        // use phy_FORCE_TRX_OFF to turn off radio, because PHY will not send back a confirm from it
                        genSetTrxState(phy_FORCE_TRX_OFF);
                    }
                    else
                    {
                        macEV << "[GTS]: the max num of retries reached, task failed \n";
                        taskP.taskStatus(task) = false;  //task fails
                        macEV << "[MAC-TASK-FAIL]: reset TP_MCPS_DATA_REQUEST \n";
                        waitGTSAck = false;
                        // to avoid several consecutive genSetTRXState are called at the same time, which may lead to error operation,
                        // use phy_FORCE_TRX_OFF to turn off radio, because PHY will not send back a confirm from it
                        genSetTrxState(phy_FORCE_TRX_OFF);
                        taskFailed('g', mac_NO_ACK);
                    }
                }
                break;
            }  // case 3

            case 4: {
                taskP.taskStatus(task) = false;  // task ends successfully
                macEV << "[GTS]: GTS transmission completes successfully, prepared for next GTS request \n";
                taskSuccess('g');
                break;
            }

            default:
                break;
        }
    }  // else if (txOption == GTS_TRANS)
    else
    {
        error("[MAC]: undefined txOption: %d!", txOption);
    }
}

void IEEE802154Mac::dispatch(phyState pStatus, const char *frFunc, phyState req_state, MACenum mStatus)
{
    bool isSIFS = false;
    /*****************************************/
    /************<csmacaCallBack()>**********/
    /***************************************/
    if (strcmp(frFunc, "csmacaCallBack") == 0)
    {
        if (txCsmaca == txBcnCmd)
        {
            FSM_MCPS_DATA_request(pStatus);
        }
        else if (txCsmaca == txData)
        {
            ASSERT(taskP.taskStatus(TP_MCPS_DATA_REQUEST) && (strcmp(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), frFunc) == 0));

            FSM_MCPS_DATA_request(pStatus);  // mStatus ignored
        }
    }
    /*****************************************/
    /*******<handle_PD_DATA_confirm()>*******/
    /***************************************/
    else if (strcmp(frFunc, "handle_PD_DATA_confirm") == 0)  // only process phy_SUCCESS here
    {
        if (txPkt == txBeacon)
        {
            resetTRX();
            taskSuccess('b');  // beacon transmitted successfully
        }
        else if (txBcnCmd != NULL)
        {
            taskSuccess('c');

            if (!scanning)
            {
                resetTRX();
            }

        }
        else if (txAck != 0)
        {
            if (rxCmd != NULL)  // ACK for CMD
            {
                if (rxCmd->getByteLength() <= aMaxSIFSFrameSize)
                {
                    isSIFS = true;
                }
                startIfsTimer(isSIFS);
                resetTRX();
                taskSuccess('a');
                txAck = 0;
            }
            else if (rxData != NULL)  // default handling (virtually the only handling needed) for <rxData>
            {
                if (rxData->getByteLength() <= aMaxSIFSFrameSize)
                {
                    isSIFS = true;
                }
                startIfsTimer(isSIFS);
                if (rxData->getIsGTS())  // received in GTS
                {
                    if (isCoordinator)
                    {
                        // the device may transmit more packets in this GTS, turn on radio
                        genSetTrxState(phy_RX_ON);
                    }
                    else
                    {
                        // PAN coordinator can transmit only one packet to me in my GTS, turn off radio now
                        genSetTrxState(phy_TRX_OFF);
                    }
                }
                else
                {
                    resetTRX();
                }
                taskSuccess('a');
            }
            else  // ACK for duplicated packet
            {
                resetTRX();
                taskSuccess('a');
            }
        }
        else if (txPkt == txData)
        {
            ASSERT((taskP.taskStatus(TP_MCPS_DATA_REQUEST)) && (strcmp(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), frFunc) == 0));

            //frmCtrl = txData->getFcf();
            if (taskP.taskStatus(TP_MCPS_DATA_REQUEST))
            {
                FSM_MCPS_DATA_request(pStatus);  // mStatus ignored
            }
        }
        else if (txPkt == txGTS)
        {
            ASSERT((taskP.taskStatus(TP_MCPS_DATA_REQUEST)) && (strcmp(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), frFunc) == 0));
            ASSERT(taskP.mcps_data_request_TxOption == GTS_TRANS);
            // hand over back to FSM
            FSM_MCPS_DATA_request(pStatus);  // mStatus ignored
        }
        //else      //may be purged from pending list
    }

    /*****************************************/
    /**************<handleAck()>*************/
    /***************************************/
    else if (strcmp(frFunc, "handleAck") == 0)  // always check the task status if the dispatch comes from recvAck()
    {
        if (txPkt == txData)
        {
            if ((taskP.taskStatus(TP_MCPS_DATA_REQUEST)) && (strcmp(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), frFunc) == 0))
            {
                FSM_MCPS_DATA_request(pStatus);  // mStatus ignored
            }
            else  // default handling for <txData>
            {
                if (taskP.taskStatus(TP_MCPS_DATA_REQUEST))  //seems late ACK received
                {
                    taskP.taskStatus(TP_MCPS_DATA_REQUEST) = false;
                }
                resetTRX();
                taskSuccess('d');
            }
        }
        else if (txPkt == txGTS)
        {
            if ((taskP.taskStatus(TP_MCPS_DATA_REQUEST)) && (strcmp(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), frFunc) == 0))
            {
                FSM_MCPS_DATA_request(pStatus);  // mStatus ignored
            }
        }
    }
    /*****************************************/
    /*******<handleAckTimeoutTimer()>********/
    /***************************************/
    else if (strcmp(frFunc, "handleAckTimeoutTimer") == 0)  // always check the task status if the dispatch comes from a timer handler
    {
        if (txPkt == txData)
        {
            if ((!taskP.taskStatus(TP_MCPS_DATA_REQUEST)) || (strcmp(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), "handleAck") != 0))
            {
                return;
            }

            if (taskP.taskStatus(TP_MCPS_DATA_REQUEST))
            {
                FSM_MCPS_DATA_request(phy_BUSY);  // mStatus ignored, pStatus can be anything but phy_SUCCESS
            }
        }
        else if (txPkt == txGTS)
        {
            ASSERT((taskP.taskStatus(TP_MCPS_DATA_REQUEST)) && (strcmp(taskP.taskFrFunc(TP_MCPS_DATA_REQUEST), "handleAck") == 0));
            ASSERT(taskP.mcps_data_request_TxOption == GTS_TRANS);
            FSM_MCPS_DATA_request(phy_BUSY);  // mStatus ignored, pStatus can be anything but phy_SUCCESS
        }
    }
}

void IEEE802154Mac::resetTRX()
{
    phyState t_state;
    macEV << "Reset radio state after a task has completed \n";
    if ((mpib.getMacBeaconOrder() != 15) || (rxBO != 15))  // beacon-enabled
    {
        if ((!inTxSD_txSDTimer) && (!inRxSD_rxSDTimer))  // in inactive portion, go to sleep
        {
            macEV << "Radio is now in inactive period, should turn off radio and go to sleep \n";
            t_state = phy_TRX_OFF;
        }
        else if (inTxSD_txSDTimer)  // should not go to sleep
        {
            macEV << "Radio is now in outgoing active period (as a coordinator), should stay awake and turn on receiver \n";
            t_state = phy_RX_ON;
        }
        else  // in RX SD, according to macRxOnWhenIdle
        {
            macEV << "Radio is now in incoming active period (as a device), whether go to sleep depending on parameter macRxOnWhenIdle \n";
            t_state = mpib.getMacRxOnWhenIdle() ? phy_RX_ON : phy_TRX_OFF;
        }
    }
    else  // non-beacon
    {
        macEV << "Non-beacon, whether go to sleep depends on parameter macRxOnWhenIdle \n";
        t_state = mpib.getMacRxOnWhenIdle() ? phy_RX_ON : phy_TRX_OFF;
    }
    genSetTrxState(t_state);
}

void IEEE802154Mac::finish()
{
    double currentTime = simTime().dbl();
    if (currentTime == 0)
    {
        return;
    }

    recordScalar("Total simulation time", currentTime);
    recordScalar("Total num of upper pkts received", numUpperPkt);
    recordScalar("Num of upper pkts dropped", numUpperPktLost);
    recordScalar("Num of BEACON pkts sent", numTxBcnPkt);
    recordScalar("Num of DATA pkts sent successfully", numTxDataSucc);
    recordScalar("Num of DATA pkts failed", numTxDataFail);
    recordScalar("Num of DATA pkts sent successfully in GTS", numTxGTSSucc);
    recordScalar("Num of DATA pkts failed in GTS", numTxGTSFail);
    recordScalar("Num of ACK pkts sent", numTxAckPkt);
    recordScalar("Num of BEACON pkts received", numRxBcnPkt);
    recordScalar("Num of BEACON pkts lost", numLostBcn);
    recordScalar("Num of DATA pkts received", numRxDataPkt);
    recordScalar("Num of DATA pkts received in GTS", numRxGTSPkt);
    recordScalar("Num of ACK pkts received", numRxAckPkt);
    recordScalar("Num of collisions", numCollision);

    return;
}

IEEE802154Mac::IEEE802154Mac()
{
    txPkt = NULL;
    txBeacon = NULL;
    txBcnCmdUpper = NULL;
    txBcnCmd = NULL;
    txData = NULL;
    txGTS = NULL;
    txAck = NULL;
    txCsmaca = NULL;
    tmpCsmaca = NULL;
    rxBeacon = NULL;
    txGTSReq = NULL;
    rxData = NULL;
    rxCmd = NULL;

    // timers
    backoffTimer = NULL;
    deferCCATimer = NULL;
    bcnRxTimer = NULL;
    bcnTxTimer = NULL;
    ackTimeoutTimer = NULL;
    txAckBoundTimer = NULL;
    txCmdDataBoundTimer = NULL;
    ifsTimer = NULL;
    txSDTimer = NULL;
    rxSDTimer = NULL;
    finalCAPTimer = NULL;
    gtsTimer = NULL;
    scanTimer = NULL;
}

IEEE802154Mac::~IEEE802154Mac()
{
    // delete all possibly running Timers
    cancelAndDelete(backoffTimer);
    cancelAndDelete(deferCCATimer);
    cancelAndDelete(bcnRxTimer);
    cancelAndDelete(bcnTxTimer);
    cancelAndDelete(ackTimeoutTimer);
    cancelAndDelete(txAckBoundTimer);
    cancelAndDelete(txCmdDataBoundTimer);
    cancelAndDelete(ifsTimer);
    cancelAndDelete(txSDTimer);
    cancelAndDelete(rxSDTimer);
    cancelAndDelete(finalCAPTimer);
    cancelAndDelete(gtsTimer);
    cancelAndDelete(scanTimer);

    rxBuff.clear();
    txBuff.clear();
}
