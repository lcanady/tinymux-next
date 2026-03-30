/*! \file httpclient.h
 * \brief HttpClient Module — outbound HTTP/HTTPS requests from softcode.
 *
 * Registers the following softcode functions (Wizard only):
 *
 *   httpget(url[, header1[, header2[, ...]]])
 *       — Perform an HTTP GET.  Extra arguments are request headers in
 *         "Name: Value" format.  Returns the response body on 2xx, or
 *         "#-1 HTTP <status>" / "#-1 CURL <error>" on failure.
 *
 *   httppost(url, body[, content_type[, header1[, ...]]])
 *       — Perform an HTTP POST with the given body.  content_type defaults
 *         to "application/json" when omitted or empty.  Extra arguments
 *         beyond the third are additional "Name: Value" headers.
 *
 * Requires libcurl at link time (-lcurl).
 * Connect timeout: 10 s.  Transfer timeout: 30 s.
 * Redirects are followed automatically (up to 10 hops).
 * TLS certificate verification is ON by default.
 */

#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

const MUX_CID CID_HttpClient = UINT64_C(0x000000036552D001);

enum {
    HTTPKEY_GET  = 0,
    HTTPKEY_POST = 1
};

interface mux_IHttpClientSinkControl : public mux_IUnknown
{
public:
    virtual void Unregistering(void) = 0;
};

class CHttpClient : public mux_IHttpClientSinkControl, mux_IFunction
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    UINT32     AddRef(void) override;
    UINT32     Release(void) override;

    void       Unregistering(void) override;

    MUX_RESULT Call(unsigned int nKey, UTF8 *buff, UTF8 **bufc,
                    dbref executor, dbref caller, dbref enactor, int eval,
                    UTF8 *fargs[], int nfargs,
                    const UTF8 *cargs[], int ncargs) override;

    CHttpClient(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CHttpClient();

private:
    UINT32                 m_cRef;
    mux_ILog              *m_pILog;
    mux_IFunctionsControl *m_pIFunctionsControl;
};

class CHttpClientFactory : public mux_IClassFactory
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    UINT32     AddRef(void) override;
    UINT32     Release(void) override;
    MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CHttpClientFactory(void);
    virtual ~CHttpClientFactory();

private:
    UINT32 m_cRef;
};

#endif // HTTPCLIENT_H
