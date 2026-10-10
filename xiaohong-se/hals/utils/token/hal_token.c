/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "hal_token.h"
#include "ohos_errno.h"
#include "ohos_types.h"

static int OEMReadToken(char *token, unsigned int len)
{
    // OEM need add here, read token from device
    (void)(token);
    (void)(len);
    return EC_SUCCESS;
}

static int OEMWriteToken(const char *token, unsigned int len)
{
    // OEM need add here, write token to device
    (void)(token);
    (void)(len);
    return EC_SUCCESS;
}

static int OEMGetAcKey(char *acKey, unsigned int len)
{
    // OEM need add here, get AcKey
    (void)(acKey);
    (void)(len);
    return EC_SUCCESS;
}

static int OEMGetProdId(char *productId, unsigned int len)
{
    // OEM need add here, get ProdId
    (void)(productId);
    (void)(len);
    return EC_SUCCESS;
}

static int OEMGetProdKey(char *productKey, unsigned int len)
{
    // OEM need add here, get ProdKey
    (void)(productKey);
    (void)(len);
    return EC_SUCCESS;
}


int HalReadToken(char *token, unsigned int len)
{
    if (token == NULL) {
        return EC_FAILURE;
    }

    return OEMReadToken(token, len);
}

int HalWriteToken(const char *token, unsigned int len)
{
    if (token == NULL) {
        return EC_FAILURE;
    }

    return OEMWriteToken(token, len);
}

int HalGetAcKey(char *acKey, unsigned int len)
{
    if (acKey == NULL) {
        return EC_FAILURE;
    }

    return OEMGetAcKey(acKey, len);
}

int HalGetProdId(char *productId, unsigned int len)
{
    if (productId == NULL) {
        return EC_FAILURE;
    }

    return OEMGetProdId(productId, len);
}

int HalGetProdKey(char *productKey, unsigned int len)
{
    if (productKey == NULL) {
        return EC_FAILURE;
    }

    return OEMGetProdKey(productKey, len);
}