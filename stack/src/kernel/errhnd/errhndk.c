/**
********************************************************************************
\file   errhndk.c

\brief  Implementation of kernel error handler module

This module implements the kernel part of the error handler module.
It is responsible for handling errors and incrementing the appropriate
error counters.

\ingroup module_errhndk
*******************************************************************************/

/*------------------------------------------------------------------------------
Copyright (c) 2012, SYSTEC electronic GmbH
Copyright (c) 2012, Bernecker+Rainer Industrie-Elektronik Ges.m.b.H. (B&R)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holders nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
------------------------------------------------------------------------------*/

//------------------------------------------------------------------------------
// includes
//------------------------------------------------------------------------------
#include <nmt.h>
#include <Benchmark.h>
#include <EplObd.h>
#include <kernel/eventk.h>
#include <kernel/EplDllk.h>

#include <errhnd.h>
#include <kernel/errhndk.h>

#include "errhndkcal.h"

//============================================================================//
//            G L O B A L   D E F I N I T I O N S                             //
//============================================================================//

//------------------------------------------------------------------------------
// const defines
//------------------------------------------------------------------------------
#define ERRORHANDLERK_CN_LOSS_PRES_EVENT_NONE   0   // error not occurred
#define ERRORHANDLERK_CN_LOSS_PRES_EVENT_OCC    1   // occurred
#define ERRORHANDLERK_CN_LOSS_PRES_EVENT_THR    2   // threshold exceeded

//------------------------------------------------------------------------------
// module global vars
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// global function prototypes
//------------------------------------------------------------------------------

//============================================================================//
//          P R I V A T E   D E F I N I T I O N S                             //
//============================================================================//

//------------------------------------------------------------------------------
// const defines
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// local types
//------------------------------------------------------------------------------

/**
\brief  instance of kernel error handler

The structure defines the instance variables of the kernel error handler.
*/
typedef struct
{
    ULONG               dllErrorEvents;                                 ///< Variable stores detected error events
    BYTE                aMnCnLossPresEvent[NUM_DLL_MNCN_LOSSPRES_OBJS]; ///< Variable stores detected error events from CNs
    tErrHndObjects      errorObjects;                                   ///< Error objects (counters and thresholds)
} tErrHndkInstance;

//------------------------------------------------------------------------------
// module local vars
//------------------------------------------------------------------------------
static tErrHndkInstance instance_l;

//------------------------------------------------------------------------------
// local function prototypes
//------------------------------------------------------------------------------
static tEplKernel postNmtEvent(tNmtEvent nmtEvent_p);
static tEplKernel generateHistoryEntry(UINT16 errorCode_p, tEplNetTime netTime_p);
static tEplKernel generateHistoryEntryNodeId(UINT16 errorCode_p, tEplNetTime netTime_p, UINT nodeId_p);
static void       decrementCnCounters(void);
static tEplKernel postHistoryEntryEvent(tEplErrHistoryEntry* pHistoryEntry_p);
static tEplKernel handleDllErrors(tEplEvent *pEvent_p);

#ifdef CONFIG_INCLUDE_NMT_MN
static tEplKernel decrementMnCounters(void);
static tEplKernel postHeartbeatEvent(UINT nodeId_p, tNmtState state_p, UINT16 errorCode_p);
static tEplKernel generateHistoryEntryWithError(UINT16 errorCode_p, tEplNetTime netTime_p, UINT16 eplError_p);
#endif

//============================================================================//
//            P U B L I C   F U N C T I O N S                                 //
//============================================================================//


//------------------------------------------------------------------------------
/**
\brief    Initialize kernel error handler module

The function initializes the kernel error handler module.

\return Returns a tEplKernel error code.

\ingroup module_errhndk
*/
//------------------------------------------------------------------------------
tEplKernel errhndk_init(void)
{
    tEplKernel      ret;

    ret = kEplSuccessful;
    instance_l.dllErrorEvents = 0;

    ret = errhndkcal_init();
    return ret;
}

//------------------------------------------------------------------------------
/**
\brief    Shutdown kernel error handler module

The function shuts down the kernel error handler module.

\return Returns always kEplSuccessful

\ingroup module_errhndk
*/
//------------------------------------------------------------------------------
tEplKernel errhndk_exit()
{
    errhndkcal_exit();
    return kEplSuccessful;
}

//------------------------------------------------------------------------------
/**
\brief    Process error events

The function processes error events and modifies the appropriate error counters.
It will be called by the DLL.

\param  pEvent_p            Pointer to error event which should be processed

\return Returns a tEplKernel error code
\retval kEplSuccessful      Event was successfully handled
\retval kEplInvalidEvent    An invalid event was supplied

\ingroup module_errhndk
*/
//------------------------------------------------------------------------------
tEplKernel errhndk_process(tEplEvent* pEvent_p)
{
    tEplKernel              ret;

    ret = kEplSuccessful;

    switch(pEvent_p->m_EventType)
    {
        case kEplEventTypeDllError:
            ret = handleDllErrors(pEvent_p);
            break;

        // unknown type
        default:
            ret = kEplInvalidEvent;
            break;
    }
    return ret;
}

//------------------------------------------------------------------------------
/**
\brief    Decrement error counters

The function decrements the error counters. It should be called at the end
of each cycle.

\param  fMN_p               Flag determines if node is running as MN

\return Returns always kEplSuccessful

\ingroup module_errhndk
*/
//------------------------------------------------------------------------------
tEplKernel errhndk_decrementCounters(BOOL fMN_p)
{
#ifdef CONFIG_INCLUDE_NMT_MN
    if (fMN_p != FALSE)
    {   // local node is MN -> decrement MN threshold counters
        decrementMnCounters();
    }
    else
    {
        decrementCnCounters();
    }
#else
        UNUSED_PARAMETER(fMN_p);
        decrementCnCounters();
#endif

    // reset error events
    instance_l.dllErrorEvents = 0L;

    return kEplSuccessful;
}

//------------------------------------------------------------------------------
/**
\brief    Post error event

The function posts an error event to the error handler module. It is provided
to other modules which need to post error events to the error handler.

\param  pErrEvent_p         Pointer to error event which should be posted

\return Returns error code provided by eventk_postEvent()

\ingroup module_errhndk
*/
//------------------------------------------------------------------------------
tEplKernel errhndk_postError(tErrHndkEvent* pErrEvent_p)
{
    tEplKernel              Ret;
    tEplEvent               Event;

    Event.m_EventSink = kEplEventSinkErrk;
    Event.m_EventType = kEplEventTypeDllError;
    Event.m_uiSize = sizeof (tErrHndkEvent);
    Event.m_pArg = pErrEvent_p;
    Ret = eventk_postEvent(&Event);

    return Ret;
}


#ifdef CONFIG_INCLUDE_NMT_MN
//------------------------------------------------------------------------------
/**
\brief    Reset error flag for specified CN

The function resets the error flag for the specified CN.

\param  nodeId_p            Node ID of CN for which error flag will be reseted

\return Returns always kEplSuccessful

\ingroup module_errhndk
*/
//------------------------------------------------------------------------------

tEplKernel errhndk_resetCnError(UINT nodeId_p)
{
    UINT                    nodeIdx;

    nodeIdx = nodeId_p - 1;

    if (nodeIdx >= NUM_DLL_MNCN_LOSSPRES_OBJS)
        return kEplInvalidNodeId;

    instance_l.aMnCnLossPresEvent[nodeIdx] = ERRORHANDLERK_CN_LOSS_PRES_EVENT_NONE;
    return kEplSuccessful;
}
#endif

//============================================================================//
//            P R I V A T E   F U N C T I O N S                               //
//============================================================================//
/// \name Private Functions
/// \{


#ifdef CONFIG_INCLUDE_NMT_MN
//------------------------------------------------------------------------------
/**
\brief    Decrement MN error counters

The function decrements the error counters used by a MN node.

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel decrementMnCounters(void)
{
    tEplKernel      ret;
    BYTE*           pCnNodeId;
    UINT            nodeIdx;
    UINT32          thresholdCnt;

    ret = EplDllkGetCurrentCnNodeIdList(&pCnNodeId);
    if (ret != kEplSuccessful)
    {
        return ret;
    }

    // iterate through node info structure list
    while (*pCnNodeId != EPL_C_ADR_INVALID)
    {
        nodeIdx = *pCnNodeId - 1;
        if (nodeIdx < NUM_DLL_MNCN_LOSSPRES_OBJS)
        {
            if  (instance_l.aMnCnLossPresEvent[nodeIdx] ==
                 ERRORHANDLERK_CN_LOSS_PRES_EVENT_NONE)
            {
                errhndkcal_getMnCnLossPresThresholdCnt(nodeIdx, &thresholdCnt);
                if (thresholdCnt > 0)
                {
                    thresholdCnt--;
                    errhndkcal_setMnCnLossPresThresholdCnt(nodeIdx, thresholdCnt);
                }
            }
            else
            {
                if (instance_l.aMnCnLossPresEvent[nodeIdx] ==
                     ERRORHANDLERK_CN_LOSS_PRES_EVENT_OCC)
                {
                    instance_l.aMnCnLossPresEvent[nodeIdx] =
                                          ERRORHANDLERK_CN_LOSS_PRES_EVENT_NONE;
                }
            }
        }
        pCnNodeId++;
    }

    if ((instance_l.dllErrorEvents & EPL_DLL_ERR_MN_CRC) == 0)
    {   // decrement CRC threshold counter, because it didn't occur last cycle
        errhndkcal_getMnCrcThresholdCnt(&thresholdCnt);
        if (thresholdCnt > 0)
        {
            thresholdCnt--;
            errhndkcal_setMnCrcThresholdCnt(thresholdCnt);
        }
    }

    if ((instance_l.dllErrorEvents & EPL_DLL_ERR_MN_CYCTIMEEXCEED) == 0)
    {   // decrement cycle exceed threshold counter, because it didn't occur last cycle
        errhndkcal_getMnCycTimeExceedThresholdCnt(&thresholdCnt);
        if (thresholdCnt > 0)
        {
            thresholdCnt--;
            errhndkcal_setMnCrcThresholdCnt(thresholdCnt);
        }
    }
    return kEplSuccessful;
}
#endif

//------------------------------------------------------------------------------
/**
\brief    Decrement CN error counters

The function decrements the error counters used by a CN node.

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static void decrementCnCounters(void)
{
    UINT32           thresholdCnt;

    if ((instance_l.dllErrorEvents & EPL_DLL_ERR_CN_LOSS_SOC) == 0)
    {   // decrement loss of SoC threshold counter, because it didn't occur last cycle
        errhndkcal_getLossSocThresholdCnt(&thresholdCnt);
        if (thresholdCnt > 0)
        {
            thresholdCnt--;
            errhndkcal_setLossSocThresholdCnt(thresholdCnt);
        }
    }

    if ((instance_l.dllErrorEvents & EPL_DLL_ERR_CN_CRC) == 0)
    {   // decrement CRC threshold counter, because it didn't occur last cycle
        errhndkcal_getCnCrcThresholdCnt(&thresholdCnt);
        if (thresholdCnt > 0)
        {
            thresholdCnt--;
            errhndkcal_setLossSocThresholdCnt(thresholdCnt);
        }
    }
}

//------------------------------------------------------------------------------
/**
\brief    Handle a CN LossOfSoc error

The function checks if a CN Loss of SoC error occured. It updates the
appropriate error counters, generates a history entry and posts the error event
to the NMT.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleCnLossSoc(tEplEvent *pEvent_p)
{
    tEplKernel              ret = kEplSuccessful;
    tErrHndkEvent*          pErrorHandlerEvent = (tErrHndkEvent*)pEvent_p->m_pArg;
    UINT32                   threshold, thresholdCnt, cumulativeCnt;

    // Check if loss of SoC event occurred
    if ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_CN_LOSS_SOC) == 0)
        return kEplSuccessful;

    errhndkcal_getCnLossSocError(&cumulativeCnt, &thresholdCnt, &threshold);

    cumulativeCnt++;
    // According to spec threshold counting is disabled by setting threshold to 0
    if (threshold > 0)
    {
        thresholdCnt += 8;

        if (thresholdCnt >= threshold)
        {
            generateHistoryEntry(EPL_E_DLL_LOSS_SOC_TH, pEvent_p->m_NetTime);
            if (ret != kEplSuccessful)
            {
                errhndkcal_setCnLossSocCounters(cumulativeCnt, thresholdCnt);
                return ret;
            }

            BENCHMARK_MOD_02_TOGGLE(7);

            postNmtEvent(kNmtEventNmtCycleError);
        }
        instance_l.dllErrorEvents |= EPL_DLL_ERR_CN_LOSS_SOC;
    }

    errhndkcal_setCnLossSocCounters(cumulativeCnt, thresholdCnt);
    return ret;
}

//------------------------------------------------------------------------------
/**
\brief    Handle a CN LossOfPReq error

The function checks if a CN Loss of PReq error occured. It updates the
appropriate error counters, generates a history entry and posts the error event
to the NMT.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleCnLossPreq(tEplEvent *pEvent_p)
{
    tEplKernel              ret;
    tErrHndkEvent*          pErrorHandlerEvent = (tErrHndkEvent*)pEvent_p->m_pArg;
    UINT32                  threshold, thresholdCnt, cumulativeCnt;

    // check if loss of PReq event occurred
    if ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_CN_LOSS_PREQ) == 0)
        return kEplSuccessful;

    errhndkcal_getCnLossPreqError(&cumulativeCnt, &thresholdCnt, &threshold);

    cumulativeCnt++;
    // According to spec threshold counting is disabled by setting threshold to 0
    if (threshold > 0)
    {
        thresholdCnt += 8;

        if (thresholdCnt >= threshold)
        {
            ret = generateHistoryEntry(EPL_E_DLL_LOSS_PREQ_TH, pEvent_p->m_NetTime);
            if (ret != kEplSuccessful)
            {
                errhndkcal_setCnLossPreqCounters(cumulativeCnt, thresholdCnt);
                return ret;
            }

            BENCHMARK_MOD_02_TOGGLE(7);

            postNmtEvent(kNmtEventNmtCycleError);
        }
    }
    errhndkcal_setCnLossPreqCounters(cumulativeCnt, thresholdCnt);
    return kEplSuccessful;
}

//------------------------------------------------------------------------------
/**
\brief    Handle a correct PReq

The function checks if a PReq was successfully received. If it is, the
appropriate error counter will be decremented.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static void handleCorrectPreq(tEplEvent *pEvent_p)
{
    tErrHndkEvent*          pErrorHandlerEvent = (tErrHndkEvent*)pEvent_p->m_pArg;
    UINT32                  thresholdCnt;

    errhndkcal_getLossPreqThresholdCnt(&thresholdCnt);

    if ((thresholdCnt == 0) ||
        ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_CN_RECVD_PREQ) == 0))
        return;

    // PReq correctly received
    thresholdCnt--;
    errhndkcal_setLossPreqThresholdCnt(thresholdCnt);
}

//------------------------------------------------------------------------------
/**
\brief    Handle a CN CRC error

The function checks if a CN CRC error occured. It updates the
appropriate error counters, generates a history entry and posts the error event
to the NMT.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleCnCrc(tEplEvent *pEvent_p)
{
    tEplKernel              ret;
    tErrHndkEvent*          pErrorHandlerEvent = (tErrHndkEvent*)pEvent_p->m_pArg;
    UINT32                  threshold, thresholdCnt, cumulativeCnt;

    // Check if CRC error event occurred
    if ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_CN_CRC) == 0)
        return kEplSuccessful;

    errhndkcal_getCnCrcError(&cumulativeCnt, &thresholdCnt, &threshold);

    cumulativeCnt++;
    // According to spec threshold counting is disabled by setting threshold to 0
    if (threshold > 0)
    {
        thresholdCnt += 8;

        if (thresholdCnt >= threshold)
        {
            ret = generateHistoryEntry(EPL_E_DLL_CRC_TH, pEvent_p->m_NetTime);
            if (ret != kEplSuccessful)
            {
                errhndkcal_setCnLossPreqCounters(cumulativeCnt, thresholdCnt);
                return ret;
            }

            BENCHMARK_MOD_02_TOGGLE(7);

            postNmtEvent(kNmtEventNmtCycleError);
        }
        instance_l.dllErrorEvents |= EPL_DLL_ERR_CN_CRC;
    }

    errhndkcal_setCnLossPreqCounters(cumulativeCnt, thresholdCnt);
    return kEplSuccessful;
}

//------------------------------------------------------------------------------
/**
\brief    Handle a invalid format error

The function checks if a invalid format error occured. A appropriate histroy
entry will be generated. If the node is acting as MN the CN causing the error
is removed from the isochronous phase.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleInvalidFormat(tEplEvent *pEvent_p)
{
    tEplKernel              ret;
    tErrHndkEvent*          pErrorHandlerEvent = (tErrHndkEvent*)pEvent_p->m_pArg;

    // check if invalid format error occurred (only direct reaction)
    if ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_INVALID_FORMAT) == 0)
        return kEplSuccessful;

    ret = generateHistoryEntryNodeId(EPL_E_DLL_INVALID_FORMAT,
                                     pEvent_p->m_NetTime,
                                     pErrorHandlerEvent->m_uiNodeId);
    if (ret != kEplSuccessful)
        return ret;

    BENCHMARK_MOD_02_TOGGLE(7);

#ifdef CONFIG_INCLUDE_NMT_MN
    if (pErrorHandlerEvent->m_NmtState >= kNmtMsNotActive)
    {   // MN is active
        if (pErrorHandlerEvent->m_uiNodeId != 0)
        {
            tEplDllNodeOpParam  NodeOpParam;

            NodeOpParam.m_OpNodeType = kEplDllNodeOpTypeIsochronous;
            NodeOpParam.m_uiNodeId =pErrorHandlerEvent->m_uiNodeId;
            // remove node from isochronous phase
            EplDllkDeleteNode(&NodeOpParam);

            // inform NmtMnu module about state change, which shall send
            // NMT command ResetNode to this CN
            postHeartbeatEvent(pErrorHandlerEvent->m_uiNodeId,
                               kNmtCsNotActive,
                               EPL_E_DLL_INVALID_FORMAT);
        }
        // $$$ and else should lead to InternComError
    }
    else
    {
        postNmtEvent(kNmtEventInternComError);
    }
#else
    // CN is active
    postNmtEvent(kNmtEventInternComError);
#endif

    return kEplSuccessful;
}

#ifdef CONFIG_INCLUDE_NMT_MN
//------------------------------------------------------------------------------
/**
\brief    Handle a MN CRC error

The function checks if a MN CRC error occured. It updates the
appropriate error counters, generates a history entry and posts the error event
to the NMT.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleMnCrc(tEplEvent *pEvent_p)
{
    tEplKernel              ret;
    tErrHndkEvent*          pErrorHandlerEvent = (tErrHndkEvent*)pEvent_p->m_pArg;
    UINT32                  threshold, thresholdCnt, cumulativeCnt;

    // check if CRC error event occurred
    if ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_MN_CRC) == 0)
        return kEplSuccessful;

    errhndkcal_getMnCrcError(&cumulativeCnt, &thresholdCnt, &threshold);

    cumulativeCnt++;
    // According to spec threshold counting is disabled by setting threshold to 0
    if (threshold > 0)
    {
        thresholdCnt += 8;
        if (thresholdCnt >= threshold)
        {
            ret = generateHistoryEntry(EPL_E_DLL_CRC_TH, pEvent_p->m_NetTime);
            if (ret != kEplSuccessful)
            {
                errhndkcal_setMnCrcCounters(cumulativeCnt, thresholdCnt);
                return ret;
            }
            postNmtEvent(kNmtEventNmtCycleError);
        }
        instance_l.dllErrorEvents |= EPL_DLL_ERR_MN_CRC;
    }
    errhndkcal_setMnCrcCounters(cumulativeCnt, thresholdCnt);
    return kEplSuccessful;
}

//------------------------------------------------------------------------------
/**
\brief    Handle a MN cycle time exceeded error

The function checks if a MN cylce time exceeded error occured. It updates the
appropriate error counters and generates a history entry.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleMnCycTimeExceed(tEplEvent *pEvent_p)
{
    tEplKernel              ret = kEplSuccessful;
    tErrHndkEvent*          pErrorHandlerEvent =
                            (tErrHndkEvent*)pEvent_p->m_pArg;
    UINT32                  threshold, thresholdCnt, cumulativeCnt;

    // check if cycle time exceeded event occurred
    if ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_MN_CYCTIMEEXCEED) == 0)
        return kEplSuccessful;

    errhndkcal_getMnCycTimeExceedError(&cumulativeCnt, &thresholdCnt,
                                       &threshold);

    cumulativeCnt++;

    // According to spec threshold counting is disabled by setting threshold to 0
    if (threshold > 0)
    {
        thresholdCnt += 8;
        if (thresholdCnt >= threshold)
        {
            ret = generateHistoryEntryWithError(EPL_E_DLL_CYCLE_EXCEED_TH,
                                               pEvent_p->m_NetTime,
                                               pErrorHandlerEvent->m_EplError);
            if (ret != kEplSuccessful)
            {
                errhndkcal_setMnCycTimeExceedCounters(thresholdCnt, cumulativeCnt);
                return ret;
            }
            postNmtEvent(kNmtEventNmtCycleError);
        }
        else
        {
            ret = generateHistoryEntryWithError(EPL_E_DLL_CYCLE_EXCEED,
                                               pEvent_p->m_NetTime,
                                               pErrorHandlerEvent->m_EplError);
            if (ret != kEplSuccessful)
            {
                errhndkcal_setMnCycTimeExceedCounters(cumulativeCnt, thresholdCnt);
                return ret;
            }
        }
        instance_l.dllErrorEvents |= EPL_DLL_ERR_MN_CYCTIMEEXCEED;
    }
    errhndkcal_setMnCycTimeExceedCounters(cumulativeCnt, thresholdCnt);
    return ret;
}

//------------------------------------------------------------------------------
/**
\brief    Handle a Loss of PRes  error

The function checks if a Loss of PRes error occured for a CN. It updates the
appropriate error counters and generates a history entry. If the threshold
count is reached the CN will be removed from the isochronous phase.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleMnCnLossPres(tEplEvent *pEvent_p)
{
    tEplKernel              ret;
    UINT                    nodeIdx;
    tEplDllNodeOpParam      nodeOpParam;
    tErrHndkEvent*          pErrorHandlerEvent = (tErrHndkEvent*)pEvent_p->m_pArg;
    UINT32                  threshold, thresholdCnt, cumulativeCnt;

    if ((pErrorHandlerEvent->m_ulDllErrorEvents & EPL_DLL_ERR_MN_CN_LOSS_PRES) == 0)
        return kEplSuccessful;

    nodeIdx = pErrorHandlerEvent->m_uiNodeId - 1;

    //if (nodeIdx >= tabentries(pErrorObjects_p->m_adwMnCnLossPresCumCnt))
    //    return kEplSuccessful;

    errhndkcal_getMnCnLossPresError(nodeIdx, &cumulativeCnt,
                                    &thresholdCnt, &threshold);

    if  (instance_l.aMnCnLossPresEvent[nodeIdx] !=
                                  ERRORHANDLERK_CN_LOSS_PRES_EVENT_NONE)
        return kEplSuccessful;

    cumulativeCnt++;

    // According to spec threshold counting is disabled by setting threshold to 0
    if (threshold > 0)
    {
        thresholdCnt += 8;

        if (thresholdCnt >= threshold)
        {
            instance_l.aMnCnLossPresEvent[nodeIdx] =
                            ERRORHANDLERK_CN_LOSS_PRES_EVENT_THR;

            ret = generateHistoryEntryNodeId(EPL_E_DLL_LOSS_PRES_TH,
                                             pEvent_p->m_NetTime,
                                             pErrorHandlerEvent->m_uiNodeId);
            if (ret != kEplSuccessful)
            {
                errhndkcal_setMnCnLossPresCounters(nodeIdx, cumulativeCnt,
                                                   thresholdCnt);
                return ret;
            }

            // remove node from isochronous phase
            nodeOpParam.m_OpNodeType = kEplDllNodeOpTypeIsochronous;
            nodeOpParam.m_uiNodeId = pErrorHandlerEvent->m_uiNodeId;
            ret = EplDllkDeleteNode(&nodeOpParam);

            // inform NmtMnu module about state change, which shall send
            // NMT command ResetNode to this CN
            postHeartbeatEvent(pErrorHandlerEvent->m_uiNodeId, kNmtCsNotActive,
                               EPL_E_DLL_LOSS_PRES_TH);
        }
        else
        {
            instance_l.aMnCnLossPresEvent[nodeIdx] =
                            ERRORHANDLERK_CN_LOSS_PRES_EVENT_OCC;
        }
    }
    errhndkcal_setMnCnLossPresCounters(nodeIdx, cumulativeCnt, thresholdCnt);
    return kEplSuccessful;
}

#endif

//------------------------------------------------------------------------------
/**
\brief    Handle a DLL errors

The function is called by the error handler's proccess function and calls the
different error handling functions to update the error counters.

\param  pEvent_p        Pointer to error event provided by DLL

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel handleDllErrors(tEplEvent *pEvent_p)
{
    tEplKernel              ret = kEplSuccessful;

    // check the different error events
    ret = handleCnLossSoc(pEvent_p);
    if (ret != kEplSuccessful)
        return ret;

    ret = handleCnLossPreq(pEvent_p);
    if (ret != kEplSuccessful)
        return ret;

    handleCorrectPreq(pEvent_p);

    ret = handleCnCrc(pEvent_p);
    if (ret != kEplSuccessful)
        return ret;

    ret = handleInvalidFormat(pEvent_p);
    if (ret != kEplSuccessful)
        return ret;

#ifdef CONFIG_INCLUDE_NMT_MN
    ret = handleMnCrc(pEvent_p);
    if (ret != kEplSuccessful)
        return ret;

    ret = handleMnCycTimeExceed(pEvent_p);
    if (ret != kEplSuccessful)
        return ret;

    ret = handleMnCnLossPres(pEvent_p);
    if (ret != kEplSuccessful)
        return ret;
#endif

    return ret;
}

#ifdef CONFIG_INCLUDE_NMT_MN
//------------------------------------------------------------------------------
/**
\brief    Post a heartbeat event

The function is used to post a heartbeat event to the NMT.

\param  nodeId_p          Node Id of CN to be posted
\param  state_p             State to be posted
\param  errorCode_p        Error Code which occured

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel postHeartbeatEvent(UINT nodeId_p, tNmtState state_p,
                                    UINT16 errorCode_p)
{
    tEplKernel              ret;
    tHeartbeatEvent         heartbeatEvent;
    tEplEvent               event;

    heartbeatEvent.nodeId = nodeId_p;
    heartbeatEvent.nmtState = state_p;
    heartbeatEvent.errorCode = errorCode_p;
    event.m_EventSink = kEplEventSinkNmtMnu;
    event.m_EventType = kEplEventTypeHeartbeat;
    event.m_uiSize = sizeof (heartbeatEvent);
    event.m_pArg = &heartbeatEvent;
    ret = eventk_postEvent(&event);
    return ret;
}
#endif

//------------------------------------------------------------------------------
/**
\brief    Post a history entry event

The function is used to post a history entry event to the API.

\param  pHistoryEntry_p     Pointer to event which should be posted.

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel postHistoryEntryEvent(tEplErrHistoryEntry* pHistoryEntry_p)
{
    tEplKernel              ret;
    tEplEvent               event;

    event.m_EventSink = kEplEventSinkApi;
    event.m_EventType = kEplEventTypeHistoryEntry;
    event.m_uiSize = sizeof (*pHistoryEntry_p);
    event.m_pArg = pHistoryEntry_p;
    ret = eventk_postEvent(&event);

    return ret;
}

//------------------------------------------------------------------------------
/**
\brief    Generate a history entry

The function generates a history entry by setting up a history entry event and
posting it to the API.

\param  errorCode_p            Error which occured
\param  netTime_p               Timestamp at which error occured

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel generateHistoryEntry(UINT16 errorCode_p, tEplNetTime netTime_p)
{
    tEplKernel                  ret;
    tEplErrHistoryEntry         historyEntry;

    historyEntry.m_wEntryType = EPL_ERR_ENTRYTYPE_MODE_OCCURRED |
                                EPL_ERR_ENTRYTYPE_PROF_EPL |
                                EPL_ERR_ENTRYTYPE_HISTORY;

    historyEntry.m_wErrorCode = errorCode_p;
    historyEntry.m_TimeStamp = netTime_p;
    memset (historyEntry.m_abAddInfo, 0, sizeof(historyEntry.m_abAddInfo));

    ret = postHistoryEntryEvent(&historyEntry);
    return ret;
}

//------------------------------------------------------------------------------
/**
\brief    Generate a history entry for a specific node ID

The function generates a history entry for a specific node ID. This is done
by setting up a history entry event and posting it to the API.

\param  errorCode_p             Error which occured
\param  netTime_p               Timestamp at which error occured
\param  nodeId_p              Node ID for which to generate history entry

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel generateHistoryEntryNodeId(UINT16 errorCode_p,
                                tEplNetTime netTime_p, UINT nodeId_p)
{
    tEplKernel                  ret;
    tEplErrHistoryEntry         historyEntry;

    historyEntry.m_wEntryType = EPL_ERR_ENTRYTYPE_MODE_OCCURRED |
                                EPL_ERR_ENTRYTYPE_PROF_EPL |
                                EPL_ERR_ENTRYTYPE_HISTORY;

    historyEntry.m_wErrorCode = errorCode_p;
    historyEntry.m_TimeStamp = netTime_p;
    AmiSetByteToLe(&historyEntry.m_abAddInfo[0], (BYTE)nodeId_p);

    ret = postHistoryEntryEvent(&historyEntry);
    return ret;
}

#ifdef CONFIG_INCLUDE_NMT_MN
//------------------------------------------------------------------------------
/**
\brief    Generate a history entry containing an error flag

The function generates a history entry which contains an additional error
flag. This is done by setting up a history entry event and posting it to the
API.

\param  errorCode_p             Error which occured
\param  netTime_p               Timestamp at which error occured
\param  eplError_p              Error flag to be included in history entry

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel generateHistoryEntryWithError(UINT16 errorCode_p,
                                tEplNetTime netTime_p, UINT16 eplError_p)
{
    tEplKernel                  ret;
    tEplErrHistoryEntry         historyEntry;

    historyEntry.m_wEntryType = EPL_ERR_ENTRYTYPE_MODE_OCCURRED |
                                EPL_ERR_ENTRYTYPE_PROF_EPL |
                                EPL_ERR_ENTRYTYPE_HISTORY;

    historyEntry.m_wErrorCode = errorCode_p;
    historyEntry.m_TimeStamp = netTime_p;
    AmiSetWordToLe(&historyEntry.m_abAddInfo[0], (UINT16)eplError_p);

    ret = postHistoryEntryEvent(&historyEntry);
    return ret;
}
#endif

//------------------------------------------------------------------------------
/**
\brief    Post a NMT event

The function posts a NMT event to the NMT.

\param  nmtEvent_p              NMT event to post

\return Returns kEplSuccessful or error code
*/
//------------------------------------------------------------------------------
static tEplKernel postNmtEvent(tNmtEvent nmtEvent_p)
{
    tEplKernel                  ret;
    tNmtEvent                   nmtEvent;
    tEplEvent                   event;

    nmtEvent = nmtEvent_p;
    event.m_EventSink = kEplEventSinkNmtk;
    event.m_EventType = kEplEventTypeNmtEvent;
    event.m_pArg = &nmtEvent;
    event.m_uiSize = sizeof (nmtEvent);
    ret = eventk_postEvent(&event);
    return ret;
}

/// \}
