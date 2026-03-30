/*! \file httpclient.cpp
 * \brief HttpClient Module — outbound HTTP/HTTPS requests from softcode.
 *
 * See httpclient.h for the full interface description.
 * Requires libcurl at link time.
 * \author Diablerie@COR (2026)
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "httpclient.h"

#include <curl/curl.h>

#include <cstring>
#include <string>
#include <vector>

// CA_WIZARD (from command.h) — reproduced here to avoid heavy include chain
#ifndef CA_WIZARD
#define CA_WIZARD 0x00000002
#endif

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;
static mux_IHttpClientSinkControl *g_pISinkControl = nullptr;

static MUX_CLASS_INFO httpclient_classes[] = { {CID_HttpClient} };
#define NUM_CLASSES (sizeof(httpclient_classes)/sizeof(httpclient_classes[0]))

// ---------------------------------------------------------------------------
// dlopen exports
// ---------------------------------------------------------------------------

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow()
{
    return (0==g_cComponents && 0==g_cServerLocks) ? MUX_S_OK : MUX_S_FALSE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    if (CID_HttpClient != cid) return MUX_E_CLASSNOTAVAILABLE;
    CHttpClientFactory *p = nullptr;
    try { p = new CHttpClientFactory; } catch (...) {}
    if (!p) return MUX_E_OUTOFMEMORY;
    const MUX_RESULT mr = p->QueryInterface(iid, ppv);
    p->Release();
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register()
{
    if (nullptr != g_pISinkControl) return MUX_S_OK;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_CLASSES, httpclient_classes, nullptr);
    if (MUX_FAILED(mr)) return mr;
    mux_IHttpClientSinkControl *pSink = nullptr;
    mr = mux_CreateInstance(CID_HttpClient, nullptr, UseSameProcess,
                            mux_IID_IUnknown, reinterpret_cast<void **>(&pSink));
    if (MUX_SUCCEEDED(mr)) { g_pISinkControl = pSink; }
    else { mux_RevokeClassObjects(NUM_CLASSES, httpclient_classes); }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister()
{
    if (nullptr != g_pISinkControl)
    {
        g_pISinkControl->Unregistering();
        g_pISinkControl->Release();
        g_pISinkControl = nullptr;
    }
    const MUX_RESULT mr = mux_RevokeClassObjects(NUM_CLASSES, httpclient_classes);
    curl_global_cleanup();
    return mr;
}

// ===========================================================================
// HTTP execution helpers
// ===========================================================================

#define HTTP_LBUF_SIZE  8000
#define HTTP_TIMEOUT_S    30L
#define HTTP_CONNECT_S    10L
#define HTTP_MAX_REDIRS   10L

// libcurl write callback — appends received bytes to a std::string
static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    const size_t total = size * nmemb;
    auto *buf = static_cast<std::string *>(userdata);
    // Cap accumulation at 2× LBUF to avoid unbounded memory use
    if (buf->size() + total <= HTTP_LBUF_SIZE * 2)
        buf->append(ptr, total);
    return total;  // must return total even if we discarded some
}

struct HttpResult
{
    bool        ok     = false;
    long        status = 0;
    std::string body;
    std::string errmsg;
};

// Build a curl_slist from an array of "Name: Value" strings.
// Returns nullptr if headers is empty.
static curl_slist *build_headers(char **headers, int n)
{
    curl_slist *list = nullptr;
    for (int i = 0; i < n; ++i)
        if (headers[i] && headers[i][0])
            list = curl_slist_append(list, headers[i]);
    return list;
}

static HttpResult do_get(const char *url, char **extra_headers, int n_extra)
{
    HttpResult res;
    CURL *curl = curl_easy_init();
    if (!curl) { res.errmsg = "curl_easy_init failed"; return res; }

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        HTTP_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_S);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      HTTP_MAX_REDIRS);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "TinyMUX-HttpClient/1.0");

    curl_slist *hdrs = build_headers(extra_headers, n_extra);
    if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    const CURLcode rc = curl_easy_perform(curl);
    if (hdrs) curl_slist_free_all(hdrs);

    if (rc != CURLE_OK)
    {
        res.errmsg = curl_easy_strerror(rc);
        curl_easy_cleanup(curl);
        return res;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
    curl_easy_cleanup(curl);

    res.ok   = true;
    res.body = std::move(body);
    return res;
}

static HttpResult do_post(const char *url,
                          const char *post_body,
                          const char *content_type,
                          char **extra_headers, int n_extra)
{
    HttpResult res;
    CURL *curl = curl_easy_init();
    if (!curl) { res.errmsg = "curl_easy_init failed"; return res; }

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     post_body ? post_body : "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        HTTP_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_S);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      HTTP_MAX_REDIRS);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "TinyMUX-HttpClient/1.0");

    // Build header list: Content-Type first, then any extra headers
    const char *ct = (content_type && content_type[0])
                     ? content_type : "application/json";
    std::string ct_hdr = std::string("Content-Type: ") + ct;

    curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, ct_hdr.c_str());
    for (int i = 0; i < n_extra; ++i)
        if (extra_headers[i] && extra_headers[i][0])
            hdrs = curl_slist_append(hdrs, extra_headers[i]);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    const CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);

    if (rc != CURLE_OK)
    {
        res.errmsg = curl_easy_strerror(rc);
        curl_easy_cleanup(curl);
        return res;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
    curl_easy_cleanup(curl);

    res.ok   = true;
    res.body = std::move(body);
    return res;
}

static void emit_result(const HttpResult &res, UTF8 *buff, UTF8 **bufc)
{
    auto tp  = *bufc;
    const auto end = buff + HTTP_LBUF_SIZE - 1;

    auto emit = [&](const char *s) {
        while (tp < end && *s) *tp++ = static_cast<UTF8>(*s++);
    };

    if (!res.ok)
    {
        emit("#-1 CURL ");
        emit(res.errmsg.c_str());
    }
    else if (res.status < 200 || res.status >= 300)
    {
        char status_buf[32];
        snprintf(status_buf, sizeof(status_buf), "#-1 HTTP %ld", res.status);
        emit(status_buf);
    }
    else
    {
        const char *src = res.body.c_str();
        while (tp < end && *src) *tp++ = static_cast<UTF8>(*src++);
    }

    *bufc = tp;
}

// ===========================================================================
// CHttpClient
// ===========================================================================

CHttpClient::CHttpClient() : m_cRef(1), m_pILog(nullptr), m_pIFunctionsControl(nullptr)
{ g_cComponents++; }

MUX_RESULT CHttpClient::FinalConstruct()
{
    MUX_RESULT mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess, IID_ILog,
                                       reinterpret_cast<void **>(&m_pILog));
    if (MUX_SUCCEEDED(mr))
    {
        bool fStarted;
        mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        { m_pILog->log_text(T("CHttpClient: loaded")); m_pILog->end_log(); }
    }

    mr = mux_CreateInstance(CID_Functions, nullptr, UseSameProcess, IID_IFunctionsControl,
                            reinterpret_cast<void **>(&m_pIFunctionsControl));
    if (MUX_FAILED(mr)) return mr;

    mux_IFunction *pIFn = nullptr;
    mr = QueryInterface(IID_IFunction, reinterpret_cast<void **>(&pIFn));
    if (MUX_SUCCEEDED(mr))
    {
        // httpget(url[, header1, header2, ...])  — wizard only
        m_pIFunctionsControl->Add(HTTPKEY_GET,  T("HTTPGET"),  pIFn, MAX_ARG, 1, MAX_ARG, 0, CA_WIZARD);
        // httppost(url, body[, content_type[, header1, ...]])  — wizard only
        // minArgs=1 (not 2) so that httppost() with zero/one args reaches our handler
        // and can return a descriptive error instead of the generic MUX arg-count message.
        m_pIFunctionsControl->Add(HTTPKEY_POST, T("HTTPPOST"), pIFn, MAX_ARG, 0, MAX_ARG, 0, CA_WIZARD);
        pIFn->Release();
    }
    return mr;
}

CHttpClient::~CHttpClient()
{
    if (m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (fStarted) { m_pILog->log_text(T("CHttpClient: unloaded")); m_pILog->end_log(); }
        m_pILog->Release(); m_pILog = nullptr;
    }
    if (m_pIFunctionsControl) { m_pIFunctionsControl->Release(); m_pIFunctionsControl = nullptr; }
    g_cComponents--;
}

MUX_RESULT CHttpClient::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
        *ppv = static_cast<mux_IHttpClientSinkControl *>(this);
    else if (IID_IFunction == iid)
        *ppv = static_cast<mux_IFunction *>(this);
    else { *ppv = nullptr; return MUX_E_NOINTERFACE; }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CHttpClient::AddRef()  { return ++m_cRef; }
UINT32 CHttpClient::Release() { if(--m_cRef==0){delete this;return 0;} return m_cRef; }

void CHttpClient::Unregistering()
{
    if (m_pIFunctionsControl) { m_pIFunctionsControl->Release(); m_pIFunctionsControl = nullptr; }
}

// ---------------------------------------------------------------------------
// mux_IFunction::Call
// ---------------------------------------------------------------------------

MUX_RESULT CHttpClient::Call(unsigned int nKey,
                              UTF8 *buff, UTF8 **bufc,
                              dbref, dbref, dbref, int,
                              UTF8 *fargs[], int nfargs,
                              const UTF8 *[], int)
{
    if (nfargs < 1 || !fargs[0] || !fargs[0][0])
    {
        const char *e = (nKey == HTTPKEY_GET)
            ? "#-1 HTTPGET REQUIRES URL"
            : "#-1 HTTPPOST REQUIRES URL AND BODY";
        auto tp = *bufc; const auto end = buff + HTTP_LBUF_SIZE - 1;
        for (const char *p = e; tp < end && *p; ) *tp++ = static_cast<UTF8>(*p++);
        *bufc = tp;
        return MUX_S_OK;
    }

    const char *url = reinterpret_cast<const char *>(fargs[0]);

    if (nKey == HTTPKEY_GET)
    {
        // fargs[1..] are extra headers
        std::vector<char *> extra;
        for (int i = 1; i < nfargs; ++i)
            extra.push_back(reinterpret_cast<char *>(fargs[i]));

        const HttpResult res = do_get(url,
            extra.empty() ? nullptr : extra.data(),
            static_cast<int>(extra.size()));
        emit_result(res, buff, bufc);
    }
    else // HTTPKEY_POST
    {
        const char *body = (nfargs >= 2 && fargs[1]) ? reinterpret_cast<const char *>(fargs[1]) : "";
        const char *ct   = (nfargs >= 3 && fargs[2] && fargs[2][0])
                           ? reinterpret_cast<const char *>(fargs[2]) : nullptr;

        // fargs[3..] are extra headers
        std::vector<char *> extra;
        for (int i = 3; i < nfargs; ++i)
            extra.push_back(reinterpret_cast<char *>(fargs[i]));

        const HttpResult res = do_post(url, body, ct,
            extra.empty() ? nullptr : extra.data(),
            static_cast<int>(extra.size()));
        emit_result(res, buff, bufc);
    }

    return MUX_S_OK;
}

// ===========================================================================
// CHttpClientFactory
// ===========================================================================

CHttpClientFactory::CHttpClientFactory() : m_cRef(1) {}
CHttpClientFactory::~CHttpClientFactory() = default;

MUX_RESULT CHttpClientFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || mux_IID_IClassFactory == iid)
        *ppv = static_cast<mux_IClassFactory *>(this);
    else { *ppv = nullptr; return MUX_E_NOINTERFACE; }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CHttpClientFactory::AddRef()  { return ++m_cRef; }
UINT32 CHttpClientFactory::Release() { if(--m_cRef==0){delete this;return 0;} return m_cRef; }

MUX_RESULT CHttpClientFactory::CreateInstance(mux_IUnknown *pOuter, MUX_IID iid, void **ppv)
{
    if (pOuter) return MUX_E_NOAGGREGATION;
    CHttpClient *p = nullptr;
    try { p = new CHttpClient; } catch (...) {}
    if (!p) return MUX_E_OUTOFMEMORY;
    const MUX_RESULT mr_init = p->FinalConstruct();
    if (MUX_FAILED(mr_init)) { p->Release(); return mr_init; }
    const MUX_RESULT mr = p->QueryInterface(iid, ppv);
    p->Release();
    return mr;
}

MUX_RESULT CHttpClientFactory::LockServer(bool bLock)
{ if (bLock) g_cServerLocks++; else g_cServerLocks--; return MUX_S_OK; }
