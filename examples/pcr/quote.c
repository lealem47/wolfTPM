/* quote.c
 *
 * Copyright (C) 2006-2020 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfTPM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* This example shows how to generate a TPM2.0 Quote that holds a signed
 * PCR measurement. PCR values are used as basis for system integrity.
 */

#include <wolftpm/tpm2_wrap.h>

#include <examples/pcr/quote.h>
#include <examples/tpm_io.h>
#include <examples/tpm_test.h>

#include <stdio.h>


#define SET_PCR_SELECT_BIT( pcrSelection, pcr ) \
                                                (pcrSelection).pcrSelect[( (pcr)/8 )] |= ( 1 << ( (pcr) % 8) );

/******************************************************************************/
/* --- BEGIN TPM2.0 Quote Test -- */
/******************************************************************************/

int TPM2_Quote_Test(void* userCtx)
{
    int rc;
    /* UINT32 i; */
    WOLFTPM2_DEV dev;
    TPMS_ATTEST attestedData;

    union {
        Quote_In quoteAsk;
        /* For managing TPM session */
        StartAuthSession_In authSes;
        PolicySecret_In policySecret;
        /* For removing keys after use */
        FlushContext_In flushCtx;
        byte maxInput[MAX_COMMAND_SIZE];
    } cmdIn;
    union {
        Quote_Out quoteResult;
        /* Output from session operations */
        StartAuthSession_Out authSes;
        PolicySecret_Out policySecret;
        byte maxOutput[MAX_RESPONSE_SIZE];
    } cmdOut;

    TPM_HANDLE sessionHandle = TPM_RH_NULL;

    WOLFTPM2_KEY endorse; /* EK  */
    WOLFTPM2_KEY storage; /* SRK */
    WOLFTPM2_KEY rsaKey;  /* AIK */

    const byte storagePwd[] = "WolfTPMpassword";
    const byte usageAuth[] = "ThisIsASecretUsageAuth";

    TPMS_AUTH_COMMAND session[MAX_SESSION_NUM];

    XMEMSET(&endorse, 0, sizeof(endorse));
    XMEMSET(&storage, 0, sizeof(storage));
    XMEMSET(&rsaKey, 0, sizeof(rsaKey));

    printf("Demo of generating signed PCR measurement (TPM2.0 Quote)\n");
    rc = wolfTPM2_Init(&dev, TPM2_IoCb, userCtx);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_Init failed 0x%x: %s\n", rc, TPM2_GetRCString(rc));
        goto exit;
    }
    printf("wolfTPM2_Init: success\n");


    /* Define the default session auth that has NULL password */
    XMEMSET(session, 0, sizeof(session));
    session[0].sessionHandle = TPM_RS_PW;
    session[0].auth.size = 0; /* NULL Password */
    TPM2_SetSessionAuth(session);


    /* Create Endorsement Key, also called EK */
    rc = wolfTPM2_CreateEK(&dev, &endorse, TPM_ALG_RSA);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_CreateEK: Endorsement failed 0x%x: %s\n",
            rc, TPM2_GetRCString(rc));
        goto exit;
    }
    printf("wolfTPM2_CreateEK: Endorsement 0x%x (%d bytes)\n",
        (word32)endorse.handle.hndl, endorse.pub.size);


    /* Create Storage Key, also called SRK */
    rc = wolfTPM2_CreateSRK(&dev, &storage, TPM_ALG_RSA, storagePwd,
        sizeof(storagePwd)-1);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_CreateSRK: Storage failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }
    printf("wolfTPM2_CreateSRK: Storage 0x%x (%d bytes)\n",
        (word32)storage.handle.hndl, storage.pub.size);


    /* Start Auth Session */
    XMEMSET(&cmdIn.authSes, 0, sizeof(cmdIn.authSes));
    cmdIn.authSes.sessionType = TPM_SE_POLICY;
    cmdIn.authSes.tpmKey = TPM_RH_NULL;
    cmdIn.authSes.bind = TPM_RH_NULL;
    cmdIn.authSes.symmetric.algorithm = TPM_ALG_NULL;
    cmdIn.authSes.authHash = TPM_ALG_SHA256;
    cmdIn.authSes.nonceCaller.size = TPM_SHA256_DIGEST_SIZE;
    rc = TPM2_GetNonce(cmdIn.authSes.nonceCaller.buffer,
                       cmdIn.authSes.nonceCaller.size);
    if (rc < 0) {
        printf("TPM2_GetNonce failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }
    rc = TPM2_StartAuthSession(&cmdIn.authSes, &cmdOut.authSes);
    if (rc != TPM_RC_SUCCESS) {
        printf("TPM2_StartAuthSession failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }
    sessionHandle = cmdOut.authSes.sessionHandle;
    printf("TPM2_StartAuthSession: sessionHandle 0x%x\n", (word32)sessionHandle);


    /* set session auth for storage key */
    session[0].auth.size = sizeof(storagePwd)-1;
    XMEMCPY(session[0].auth.buffer, storagePwd, session[0].auth.size);


    /* Create an RSA key for Attestation purposes */
    rc = wolfTPM2_CreateAndLoadAIK(&dev, &rsaKey, TPM_ALG_RSA, &storage,
        usageAuth, sizeof(usageAuth)-1);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_CreateAndLoadAIK failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }
    printf("wolfTPM2_CreateAndLoadAIK: AIK 0x%x (%d bytes)\n",
        (word32)rsaKey.handle.hndl, rsaKey.pub.size);


    /* set auth for using the AIK */
    session[0].auth.size = sizeof(usageAuth)-1;
    XMEMCPY(session[0].auth.buffer, usageAuth, session[0].auth.size);

    /* Prepare Quote request */
    XMEMSET(&cmdIn.quoteAsk, 0, sizeof(cmdIn.quoteAsk));
    XMEMSET(&cmdOut.quoteResult, 0, sizeof(cmdOut.quoteResult));
    cmdIn.quoteAsk.signHandle = rsaKey.handle.hndl;
    cmdIn.quoteAsk.inScheme.scheme = TPM_ALG_RSASSA;
    cmdIn.quoteAsk.inScheme.details.rsassa.hashAlg = TPM_ALG_SHA256;
    cmdIn.quoteAsk.qualifyingData.size = 0; /* optional */
    /* Set the PCR for signing */
    cmdIn.quoteAsk.PCRselect.count = 1;
    cmdIn.quoteAsk.PCRselect.pcrSelections[0].hash = TPM_ALG_SHA256;
    cmdIn.quoteAsk.PCRselect.pcrSelections[0].sizeofSelect = 3;
    /* PCR16 is for DEBUG purposes, therefore safe to use for a demo */
    SET_PCR_SELECT_BIT(cmdIn.quoteAsk.PCRselect.pcrSelections[0], 16);


    /* Get PCR measurement signed by the TPM using the AIK key */
    rc = TPM2_Quote(&cmdIn.quoteAsk, &cmdOut.quoteResult);
    if (rc != TPM_RC_SUCCESS) {
        printf("TPM2_Quote failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }
    printf("TPM2_Quote: success\n");

    rc = TPM2_ParseAttest(&cmdOut.quoteResult.quoted, &attestedData);
    if (rc != TPM_RC_SUCCESS) {
        printf("TPM2_Packet_ParseAttest failed 0x%x: %s\n", rc,
            TPM2_GetRCString(rc));
        goto exit;
    }
    if (attestedData.magic != TPM_GENERATED_VALUE) {
        printf("\tError, attested data not generated by the TPM = 0x%X\n",
            attestedData.magic);
    }

    printf("TPM with signature attests (type 0x%x):\n", attestedData.type);
    printf("\tTPM signed %lu PCR\n",
        (unsigned long)attestedData.attested.quote.pcrSelect.count);

exit:

    /* Close session */
    if (sessionHandle != TPM_RH_NULL) {
        cmdIn.flushCtx.flushHandle = sessionHandle;
        TPM2_FlushContext(&cmdIn.flushCtx);
    }

    /* Close key handles */
    wolfTPM2_UnloadHandle(&dev, &rsaKey.handle);
    wolfTPM2_UnloadHandle(&dev, &storage.handle);
    wolfTPM2_UnloadHandle(&dev, &endorse.handle);

    wolfTPM2_Cleanup(&dev);
    return rc;
}

/******************************************************************************/
/* --- END TPM2.0 Quote Test -- */
/******************************************************************************/


#ifndef NO_MAIN_DRIVER
int main(void)
{
    int rc;

    rc = TPM2_Quote_Test(NULL);

    return rc;
}
#endif
