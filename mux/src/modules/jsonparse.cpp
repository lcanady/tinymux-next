/*! \file jsonparse.cpp
 * \brief JsonParse Module — JSON manipulation functions for softcode.
 * \author Diablerie@COR (2026)
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "jsonparse.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;
static mux_IJsonParseSinkControl *g_pISinkControl = nullptr;

static MUX_CLASS_INFO jsonparse_classes[] = { {CID_JsonParse} };
#define NUM_CLASSES (sizeof(jsonparse_classes)/sizeof(jsonparse_classes[0]))

// ---------------------------------------------------------------------------
// dlopen exports
// ---------------------------------------------------------------------------

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow()
{
    return (0 == g_cComponents && 0 == g_cServerLocks) ? MUX_S_OK : MUX_S_FALSE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    if (CID_JsonParse != cid) return MUX_E_CLASSNOTAVAILABLE;
    CJsonParseFactory *p = nullptr;
    try { p = new CJsonParseFactory; } catch (...) {}
    if (!p) return MUX_E_OUTOFMEMORY;
    const MUX_RESULT mr = p->QueryInterface(iid, ppv);
    p->Release();
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register()
{
    if (nullptr != g_pISinkControl) return MUX_S_OK;
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_CLASSES, jsonparse_classes, nullptr);
    if (MUX_FAILED(mr)) return mr;
    mux_IJsonParseSinkControl *pSink = nullptr;
    mr = mux_CreateInstance(CID_JsonParse, nullptr, UseSameProcess,
                            mux_IID_IUnknown, reinterpret_cast<void **>(&pSink));
    if (MUX_SUCCEEDED(mr)) { g_pISinkControl = pSink; }
    else { mux_RevokeClassObjects(NUM_CLASSES, jsonparse_classes); }
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
    return mux_RevokeClassObjects(NUM_CLASSES, jsonparse_classes);
}

// ===========================================================================
// JSON parser, path navigator, and serializer — see json_logic.h
// ===========================================================================

#include "json_logic.h"

// ---- softcode output helper ------------------------------------------------

#define JSON_LBUF_SIZE 8000

static void out_str(const char *src, UTF8 *buff, UTF8 **bufc)
{
    if (!src) return;
    auto tp = *bufc;
    const auto end = buff + JSON_LBUF_SIZE - 1;
    while (tp < end && *src) *tp++ = static_cast<UTF8>(*src++);
    *bufc = tp;
}

// ===========================================================================
// CJsonParse
// ===========================================================================

CJsonParse::CJsonParse() : m_cRef(1), m_pILog(nullptr), m_pIFunctionsControl(nullptr)
{ g_cComponents++; }

MUX_RESULT CJsonParse::FinalConstruct()
{
    MUX_RESULT mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess, IID_ILog,
                                       reinterpret_cast<void **>(&m_pILog));
    if (MUX_SUCCEEDED(mr))
    {
        bool fStarted;
        mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        { m_pILog->log_text(T("CJsonParse: loaded")); m_pILog->end_log(); }
    }

    mr = mux_CreateInstance(CID_Functions, nullptr, UseSameProcess, IID_IFunctionsControl,
                            reinterpret_cast<void **>(&m_pIFunctionsControl));
    if (MUX_FAILED(mr)) return mr;

    mux_IFunction *pIFn = nullptr;
    mr = QueryInterface(IID_IFunction, reinterpret_cast<void **>(&pIFn));
    if (MUX_SUCCEEDED(mr))
    {
        // Register all six softcode functions (perms=0 → anyone can call)
        m_pIFunctionsControl->Add(JSONKEY_GET,       T("JSON_GET"),       pIFn, MAX_ARG, 2, 2, 0, 0);
        m_pIFunctionsControl->Add(JSONKEY_KEYS,      T("JSON_KEYS"),      pIFn, MAX_ARG, 1, 2, 0, 0);
        m_pIFunctionsControl->Add(JSONKEY_ARRAY_LEN, T("JSON_ARRAY_LEN"), pIFn, MAX_ARG, 1, 2, 0, 0);
        m_pIFunctionsControl->Add(JSONKEY_TYPE,      T("JSON_TYPE"),      pIFn, MAX_ARG, 1, 2, 0, 0);
        m_pIFunctionsControl->Add(JSONKEY_ENCODE,    T("JSON_ENCODE"),    pIFn, MAX_ARG, 1, 1, 0, 0);
        m_pIFunctionsControl->Add(JSONKEY_DECODE,    T("JSON_DECODE"),    pIFn, MAX_ARG, 1, 1, 0, 0);
        pIFn->Release();
    }
    return mr;
}

CJsonParse::~CJsonParse()
{
    if (m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (fStarted) { m_pILog->log_text(T("CJsonParse: unloaded")); m_pILog->end_log(); }
        m_pILog->Release(); m_pILog = nullptr;
    }
    if (m_pIFunctionsControl) { m_pIFunctionsControl->Release(); m_pIFunctionsControl = nullptr; }
    g_cComponents--;
}

MUX_RESULT CJsonParse::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
        *ppv = static_cast<mux_IJsonParseSinkControl *>(this);
    else if (IID_IFunction == iid)
        *ppv = static_cast<mux_IFunction *>(this);
    else { *ppv = nullptr; return MUX_E_NOINTERFACE; }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CJsonParse::AddRef()  { return ++m_cRef; }
UINT32 CJsonParse::Release() { if (--m_cRef==0){delete this;return 0;} return m_cRef; }

void CJsonParse::Unregistering()
{
    if (m_pIFunctionsControl) { m_pIFunctionsControl->Release(); m_pIFunctionsControl = nullptr; }
}

// ---------------------------------------------------------------------------
// mux_IFunction::Call
// ---------------------------------------------------------------------------

MUX_RESULT CJsonParse::Call(unsigned int nKey,
                             UTF8 *buff, UTF8 **bufc,
                             dbref, dbref, dbref, int,
                             UTF8 *fargs[], int nfargs,
                             const UTF8 *[], int)
{
    // json_encode / json_decode need no parsing
    if (nKey == JSONKEY_ENCODE)
    {
        std::string enc;
        json_escape_str(reinterpret_cast<const char *>(fargs[0]), enc);
        out_str(enc.c_str(), buff, bufc);
        return MUX_S_OK;
    }
    if (nKey == JSONKEY_DECODE)
    {
        // Strip surrounding quotes if present, unescape
        std::string_view sv(reinterpret_cast<const char *>(fargs[0]));
        if (sv.size() >= 2 && sv.front()=='"' && sv.back()=='"')
        {
            size_t i = 0;
            auto decoded = json_parse_str(sv, i);
            if (decoded) { out_str(decoded->c_str(), buff, bufc); return MUX_S_OK; }
        }
        // Already unquoted — return as-is
        out_str(reinterpret_cast<const char *>(fargs[0]), buff, bufc);
        return MUX_S_OK;
    }

    // All other functions need a parsed JSON root
    auto root_opt = parse_json(reinterpret_cast<const char *>(fargs[0]));
    if (!root_opt)
    {
        out_str("#-1 JSON PARSE ERROR", buff, bufc);
        return MUX_S_OK;
    }

    // Optional path argument (fargs[1] if nfargs >= 2)
    std::string_view path;
    if (nfargs >= 2 && fargs[1] && fargs[1][0])
        path = reinterpret_cast<const char *>(fargs[1]);

    const JVal *v = navigate(*root_opt, path);
    if (!v)
    {
        out_str("#-1 PATH NOT FOUND", buff, bufc);
        return MUX_S_OK;
    }

    switch (nKey)
    {
        case JSONKEY_GET:
            // Return unwrapped value: string content for Str, serialized for others
            if (v->t == JVal::T::Str)
                out_str(v->s.c_str(), buff, bufc);
            else if (v->t == JVal::T::Num || v->t == JVal::T::Bool || v->t == JVal::T::Null)
                out_str(serialize(*v).c_str(), buff, bufc);
            else
                out_str(serialize(*v).c_str(), buff, bufc);
            break;

        case JSONKEY_KEYS:
            if (v->t != JVal::T::Obj) { out_str("#-1 NOT AN OBJECT", buff, bufc); break; }
            for (size_t i = 0; i < v->o.size(); ++i)
            {
                if (i) out_str(" ", buff, bufc);
                out_str(v->o[i].first.c_str(), buff, bufc);
            }
            break;

        case JSONKEY_ARRAY_LEN:
            if (v->t != JVal::T::Arr) { out_str("#-1 NOT AN ARRAY", buff, bufc); break; }
            {
                const std::string len = std::to_string(v->a.size());
                out_str(len.c_str(), buff, bufc);
            }
            break;

        case JSONKEY_TYPE:
            switch (v->t)
            {
                case JVal::T::Null: out_str("null",   buff, bufc); break;
                case JVal::T::Bool: out_str("bool",   buff, bufc); break;
                case JVal::T::Num:  out_str("number", buff, bufc); break;
                case JVal::T::Str:  out_str("string", buff, bufc); break;
                case JVal::T::Arr:  out_str("array",  buff, bufc); break;
                case JVal::T::Obj:  out_str("object", buff, bufc); break;
            }
            break;

        default:
            out_str("#-1 UNKNOWN FUNCTION", buff, bufc);
            break;
    }
    return MUX_S_OK;
}

// ===========================================================================
// CJsonParseFactory
// ===========================================================================

CJsonParseFactory::CJsonParseFactory() : m_cRef(1) {}
CJsonParseFactory::~CJsonParseFactory() = default;

MUX_RESULT CJsonParseFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || mux_IID_IClassFactory == iid)
        *ppv = static_cast<mux_IClassFactory *>(this);
    else { *ppv = nullptr; return MUX_E_NOINTERFACE; }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CJsonParseFactory::AddRef()  { return ++m_cRef; }
UINT32 CJsonParseFactory::Release() { if(--m_cRef==0){delete this;return 0;} return m_cRef; }

MUX_RESULT CJsonParseFactory::CreateInstance(mux_IUnknown *pOuter, MUX_IID iid, void **ppv)
{
    if (pOuter) return MUX_E_NOAGGREGATION;
    CJsonParse *p = nullptr;
    try { p = new CJsonParse; } catch (...) {}
    if (!p) return MUX_E_OUTOFMEMORY;
    const MUX_RESULT mr_init = p->FinalConstruct();
    if (MUX_FAILED(mr_init)) { p->Release(); return mr_init; }
    const MUX_RESULT mr = p->QueryInterface(iid, ppv);
    p->Release();
    return mr;
}

MUX_RESULT CJsonParseFactory::LockServer(bool bLock)
{ if (bLock) g_cServerLocks++; else g_cServerLocks--; return MUX_S_OK; }
