/* nsdsel_gtls.c
 *
 * An implementation of the nsd select() interface for GnuTLS.
 *
 * Copyright (C) 2008-2016 Adiscon GmbH.
 *
 * This file is part of the rsyslog runtime library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *       -or-
 *       see COPYING.ASL20 in the source distribution
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <gnutls/gnutls.h>

#include "rsyslog.h"
#include "module-template.h"
#include "obj.h"
#include "errmsg.h"
#include "nsd.h"
#include "nsd_gtls.h"
#include "nsd_ptcp.h"
#include "nsdsel_ptcp.h"
#include "nsdsel_gtls.h"

/* static data */
DEFobjStaticHelpers
DEFobjCurrIf(glbl)
DEFobjCurrIf(nsdsel_ptcp)

static rsRetVal
gtlsHasRcvInBuffer(nsd_gtls_t *pThis)
{
	/* we have a valid receive buffer one such is allocated and
	 * NOT exhausted!
	 */
	DBGPRINTF("hasRcvInBuffer on nsd %p: pszRcvBuf %p, lenRcvBuf %d\n", pThis,
		pThis->pszRcvBuf, pThis->lenRcvBuf);
	return(pThis->pszRcvBuf != NULL && pThis->lenRcvBuf != -1);
	}


/* Standard-Constructor
 */
BEGINobjConstruct(nsdsel_gtls) /* be sure to specify the object type also in END macro! */
	iRet = nsdsel_ptcp.Construct(&pThis->pTcp);
ENDobjConstruct(nsdsel_gtls)


/* destructor for the nsdsel_gtls object */
BEGINobjDestruct(nsdsel_gtls) /* be sure to specify the object type also in END and CODESTART macros! */
CODESTARTobjDestruct(nsdsel_gtls)
	if(pThis->pTcp != NULL)
		nsdsel_ptcp.Destruct(&pThis->pTcp);
ENDobjDestruct(nsdsel_gtls)


/* Add a socket to the select set */
static rsRetVal
Add(nsdsel_t *pNsdsel, nsd_t *pNsd, nsdsel_waitOp_t waitOp)
{
	DEFiRet;
	nsdsel_gtls_t *pThis = (nsdsel_gtls_t*) pNsdsel;
	nsd_gtls_t *pNsdGTLS = (nsd_gtls_t*) pNsd;

	ISOBJ_TYPE_assert(pThis, nsdsel_gtls);
	ISOBJ_TYPE_assert(pNsdGTLS, nsd_gtls);
	DBGPRINTF("Add on nsd %p:\n", pNsdGTLS);
	if(pNsdGTLS->iMode == 1) {
		if(waitOp == NSDSEL_RD && gtlsHasRcvInBuffer(pNsdGTLS)) {
			++pThis->iBufferRcvReady;
			dbgprintf("nsdsel_gtls: data already present in buffer, initiating "
				  "dummy select %p->iBufferRcvReady=%d\n",
				  pThis, pThis->iBufferRcvReady);
			FINALIZE;
		}
		if(pNsdGTLS->rtryCall != gtlsRtry_None) {
			if(gnutls_record_get_direction(pNsdGTLS->sess) == 0) {
				CHKiRet(nsdsel_ptcp.Add(pThis->pTcp, pNsdGTLS->pTcp, NSDSEL_RD));
			} else {
				CHKiRet(nsdsel_ptcp.Add(pThis->pTcp, pNsdGTLS->pTcp, NSDSEL_WR));
			}
			FINALIZE;
		}
	}

	dbgprintf("nsdsel_gtls: reached end on nsd %p, calling nsdsel_ptcp.Add with waitOp %d... \n", pNsdGTLS, waitOp);
	/* if we reach this point, we need no special handling */
	CHKiRet(nsdsel_ptcp.Add(pThis->pTcp, pNsdGTLS->pTcp, waitOp));

finalize_it:
	RETiRet;
}


/* perform the select()  piNumReady returns how many descriptors are ready for IO
 * TODO: add timeout!
 */
static rsRetVal
Select(nsdsel_t *pNsdsel, int *piNumReady)
{
	DEFiRet;
	nsdsel_gtls_t *pThis = (nsdsel_gtls_t*) pNsdsel;

	ISOBJ_TYPE_assert(pThis, nsdsel_gtls);
	if(pThis->iBufferRcvReady > 0) {
		/* we still have data ready! */
		*piNumReady = pThis->iBufferRcvReady;
		dbgprintf("nsdsel_gtls: doing dummy select for %p->iBufferRcvReady=%d, data present\n",
			pThis, pThis->iBufferRcvReady);
	} else {
		iRet = nsdsel_ptcp.Select(pThis->pTcp, piNumReady);
	}

	RETiRet;
}


/* check if a socket is ready for IO */
static rsRetVal
IsReady(nsdsel_t *pNsdsel, nsd_t *pNsd, nsdsel_waitOp_t waitOp, int *pbIsReady)
{
	DEFiRet;
	nsdsel_gtls_t *pThis = (nsdsel_gtls_t*) pNsdsel;
	nsd_gtls_t *pNsdGTLS = (nsd_gtls_t*) pNsd;

	ISOBJ_TYPE_assert(pThis, nsdsel_gtls);
	ISOBJ_TYPE_assert(pNsdGTLS, nsd_gtls);
	if(pNsdGTLS->iMode == 1) {
		if(waitOp == NSDSEL_RD && gtlsHasRcvInBuffer(pNsdGTLS)) {
			*pbIsReady = 1;
			--pThis->iBufferRcvReady; /* one "pseudo-read" less */
			dbgprintf("nsdl_gtls: dummy read, decermenting %p->iBufRcvReady, now %d\n",
				   pThis, pThis->iBufferRcvReady);
			FINALIZE;
		}

		/* now we must ensure that we do not fall back to PTCP if we have
		 * done a "dummy" select. In that case, we know when the predicate
		 * is not matched here, we do not have data available for this
		 * socket. -- rgerhards, 2010-11-20
		 */
		if(pThis->iBufferRcvReady) {
			dbgprintf("nsd_gtls: dummy read, %p->buffer not available for this FD\n", pThis);
			*pbIsReady = 0;
			FINALIZE;
		}
	}

	CHKiRet(nsdsel_ptcp.IsReady(pThis->pTcp, pNsdGTLS->pTcp, waitOp, pbIsReady));

finalize_it:
	RETiRet;
}


/* ------------------------------ end support for the select() interface ------------------------------ */


/* queryInterface function */
BEGINobjQueryInterface(nsdsel_gtls)
CODESTARTobjQueryInterface(nsdsel_gtls)
	if(pIf->ifVersion != nsdCURR_IF_VERSION) {/* check for current version, increment on each change */
		ABORT_FINALIZE(RS_RET_INTERFACE_NOT_SUPPORTED);
	}

	/* ok, we have the right interface, so let's fill it
	 * Please note that we may also do some backwards-compatibility
	 * work here (if we can support an older interface version - that,
	 * of course, also affects the "if" above).
	 */
	pIf->Construct = (rsRetVal(*)(nsdsel_t**)) nsdsel_gtlsConstruct;
	pIf->Destruct = (rsRetVal(*)(nsdsel_t**)) nsdsel_gtlsDestruct;
	pIf->Add = Add;
	pIf->Select = Select;
	pIf->IsReady = IsReady;
finalize_it:
ENDobjQueryInterface(nsdsel_gtls)


/* exit our class
 */
BEGINObjClassExit(nsdsel_gtls, OBJ_IS_CORE_MODULE) /* CHANGE class also in END MACRO! */
CODESTARTObjClassExit(nsdsel_gtls)
	/* release objects we no longer need */
	objRelease(glbl, CORE_COMPONENT);
	objRelease(nsdsel_ptcp, LM_NSD_PTCP_FILENAME);
ENDObjClassExit(nsdsel_gtls)


/* Initialize the nsdsel_gtls class. Must be called as the very first method
 * before anything else is called inside this class.
 * rgerhards, 2008-02-19
 */
BEGINObjClassInit(nsdsel_gtls, 1, OBJ_IS_CORE_MODULE) /* class, version */
	/* request objects we use */
	CHKiRet(objUse(glbl, CORE_COMPONENT));
	CHKiRet(objUse(nsdsel_ptcp, LM_NSD_PTCP_FILENAME));

	/* set our own handlers */
ENDObjClassInit(nsdsel_gtls)
